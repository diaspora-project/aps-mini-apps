#include <iomanip>
#include "sirt/recon_engine.h"
#include "trace_h5io.h"
#include "data_region_base.h"
#include "tclap/CmdLine.h"
#include "disp_comm_mpi.h"
#include "disp_engine_reduction.h"
#include "sirt.h" // Include SIRTReconSpace
#include <cassert>
#include <time.h>
#include <string>
#include <fstream>
#include <iostream>
#include "trace_data.h"
#include <vector>
#include <unistd.h>
#include <charconv>
#include <csignal>


#include <veloc.hpp>
#include <veloc/boost.hpp>
#include <boost/serialization/export.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include "mofka_stream.h"

// Define an alias for the instantiated template class
using DISPEngineReductionSIRT = DISPEngineReduction<SIRTReconSpace, float>;
using DISPEngineBaseSIRT = DISPEngineBase<SIRTReconSpace, float>;
using AReductionSpaceBaseSIRT = AReductionSpaceBase<SIRTReconSpace, float>;

// Register the derived class
BOOST_CLASS_EXPORT(AReductionSpaceBaseSIRT)
BOOST_CLASS_EXPORT(DISPEngineBaseSIRT)
BOOST_CLASS_EXPORT(DISPEngineReductionSIRT)

int saveAsHDF5(const char* fname, float* recon, hsize_t* output_dims) {
  hid_t output_file_id = H5Fcreate(fname, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
  if (output_file_id < 0) {
      return 1;
  }
  hid_t output_dataspace_id = H5Screate_simple(3, output_dims, NULL);
  hid_t output_dataset_id = H5Dcreate(output_file_id, "/data", H5T_NATIVE_FLOAT, output_dataspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  H5Dwrite(output_dataset_id, H5T_NATIVE_FLOAT, H5S_ALL, H5S_ALL, H5P_DEFAULT, recon);
  H5Dclose(output_dataset_id);
  H5Sclose(output_dataspace_id);
  H5Fclose(output_file_id);
  return 0;
}

int ReconTask::run() {

  // MofkaStream ms = MofkaStream{driver,
  //   config.batchsize,
  //   static_cast<uint32_t>(config.window_len),
  //   task_id,
  //   0
  // }; // Add the missing progress argument

  std::cout << "[Task-" << task_id << "] Handshaking with DIST..." << std::endl;
  ms.handshake(task_id);

  std::cout << "[Task-" << task_id << "] Handshake completed. Setting up consumer and producer" << std::endl;

  // Prepare consumer and producer
  std::string consuming_topic = "dist_sirt";
  std::string producing_topic = "sirt_den";
  std::vector<size_t> targets = {static_cast<size_t>(task_id)};

  mofka::Producer producer = ms.getProducer(producing_topic, "sirt");
  mofka::Consumer consumer = ms.getConsumer(consuming_topic, "sirt", targets);
  /* Get metadata structure */
  json tmetadata = ms.getInfo();
  auto n_blocks = tmetadata["n_sinograms"].get<int64_t>();
  auto num_cols = tmetadata["n_rays_per_proj_row"].get<int64_t>();

  std::cout << "[Task-" << task_id << "] Init reconstruction: n_blocks: " << n_blocks << " num_cols: " << num_cols << std::endl;

  /**********************/

  /**************************/
  /* Perform reconstruction */
  /* Define job size per thread request */
  #ifdef TIMERON
  std::chrono::duration<double> recon_tot(0.), inplace_tot(0.), update_tot(0.),
    datagen_tot(0.);
  std::chrono::duration<double> write_tot(0.);
  std::chrono::duration<double> e2e_tot(0.);
  std::chrono::duration<double> ckpt_tot(0.);
  #endif
  DataRegionBase<float, TraceMetadata> *curr_slices = nullptr;
  /// Reconstructed image
  DataRegionBareBase<float> recon_image(n_blocks*num_cols*num_cols);
  for(size_t i=0; i<recon_image.count(); ++i)
    recon_image[i]=0.; /// Initial values of the reconstructe image

  /// Number of requested ray-sum values by each thread poll
  int64_t req_number = num_cols;
  /// Required data structure for dumping image to h5 file
  trace_io::H5Metadata h5md;
  h5md.ndims=3;
  h5md.dims= new hsize_t[3];
  h5md.dims[1] = tmetadata["tn_sinograms"].get<int64_t>();
  h5md.dims[0] = 0;   /// Number of projections is unknown
  h5md.dims[2] = tmetadata["n_rays_per_proj_row"].get<int64_t>();
  size_t data_size = 0;

  /***********************/
  /* Initiate middleware */
  /* Prepare main reduction space and its objects */
  /* The size of the reconstruction object (in reconstruction space) is
  * twice the reconstruction object size, because of the length storage
  */
  auto main_recon_space = new SIRTReconSpace(
      n_blocks, 2*num_cols*num_cols);
  main_recon_space->Initialize(num_cols*num_cols);

  DataRegion2DBareBase<float> &main_recon_replica = main_recon_space->reduction_objects();
  float init_val=0.;
  // if (progress == 0) {
  //   main_recon_replica.ResetAllItems(init_val);
  // }

  /* Prepare processing engine and main reduction space for other threads */
  DISPEngineBase<SIRTReconSpace, float> *engine =
    new DISPEngineReductionSIRT(main_recon_space, config.thread_count);

  // Configure the VeloC checkpointing
  unsigned int ckpt_id = 0;
  ckpt_mutex->lock();
  // veloc::client_t *ckpt_client = veloc::get_client((unsigned int)task_id, config.ckpt_config);
  veloc::client_t *ckpt_client = veloc::get_client(ckpt_id, config.ckpt_config);
  std::string ckpt_name = config.ckpt_name + "_" + std::to_string(task_id);
  // Protect reconstruction memory regions
  int progress = 0; // Reconstruction progress marked by the projection requence ids
  ckpt_client->mem_protect(0, &progress, 1, sizeof(int), ckpt_name);
  ckpt_client->mem_protect(1, veloc::boost::serializer(recon_image), veloc::boost::deserializer(recon_image), ckpt_name);

  // int passes = ckpt_client->restart_test(config.ckpt_name, 0, task_id);
  int passes = ckpt_client->restart_test(ckpt_name, 0, ckpt_id);
  // Checkpoint restart if any
  if(passes>0){
    std::cout << "[Task-" << task_id << "] Checkpoint found at " << passes << ". Restarting from checkpoint" << std::endl;
    ckpt_client->restart(ckpt_name, passes);
    ms.updateProgress(progress);
    std::cout << "[Task-" << task_id << "] Restarted from checkpoint at iteration " << passes << ", progress = " << progress << std::endl;
  }else{
    std::cout << "[Task-" << task_id << "] No checkpoint found. Starting from scratch" << std::endl;
    passes = 0;
  }
  ckpt_mutex->unlock();

  #ifdef TIMERON
  auto e2e_beg = std::chrono::system_clock::now();
  #endif

  std::cout << "[Task-" << task_id << "] Start reconstruction passes = " << passes << std::endl;

  for(; passes < config.num_passes; ++passes){

    int killed = kill_signal.load();
    if (killed != 0) {
      std::cout << "[Task-" << task_id << "] Received kill signal: " << killed << ". Exiting..." << std::endl;
      return killed;
    }

    #ifdef TIMERON
    auto datagen_beg = std::chrono::system_clock::now();
    #endif
    curr_slices = ms.readSlidingWindow(recon_image, config.window_step, consumer);
    
    if(config.center!=0 && curr_slices!=nullptr)
      curr_slices->metadata().center(config.center);
    #ifdef TIMERON
    datagen_tot += (std::chrono::system_clock::now()-datagen_beg);
    #endif
    
    if (ms.isEndOfStream()) {
      std::cout << "[Task-" << task_id << "] End of stream. Exiting..." << std::endl;
      break;
    }
    if(curr_slices == nullptr) {
      std::cout << "[Task-" << task_id << "] passes = " << passes << " -- No new data in the sliding window. Skip processing" << std::endl;
      continue;
    }
    /// Iterate on window
    for(int i=0; i<config.window_iter; ++i){

      int killed = kill_signal.load();
      if (killed != 0) {
        std::cout << "[Task-" << task_id << "] Received kill signal: " << killed << ". Exiting..." << std::endl;
        return killed;
      }

      #ifdef TIMERON
      auto recon_beg = std::chrono::system_clock::now();
      #endif
      engine->RunParallelReduction(*curr_slices, req_number);  /// Reconstruction

      #ifdef TIMERON
      recon_tot += (std::chrono::system_clock::now()-recon_beg);
      auto inplace_beg = std::chrono::system_clock::now();
      #endif
      engine->ParInPlaceLocalSynchWrapper();              /// Local combination
      #ifdef TIMERON
      inplace_tot += (std::chrono::system_clock::now()-inplace_beg);

      /// Update reconstruction object
      auto update_beg = std::chrono::system_clock::now();
      #endif
      main_recon_space->UpdateRecon(recon_image, main_recon_replica);
      #ifdef TIMERON
      update_tot += (std::chrono::system_clock::now()-update_beg);
      #endif
      engine->ResetReductionSpaces(init_val);
      curr_slices->ResetMirroredRegionIter();
    }

    // Checkpoint
    #ifdef TIMERON
    auto ckpt_beg = std::chrono::system_clock::now();
    #endif
    if(!(passes%config.ckpt_freq) || stop_flag.load()){
      ckpt_mutex->lock();
      ckpt_client->checkpoint_wait();
      progress = ms.getProgress();
      std::cout << "[Task-" << task_id << "] Checkpointing at iteration " << passes << ", progress = " << progress << std::endl;
      // if (!ckpt_client->checkpoint(config.ckpt_name, passes)) {
      if (!ckpt_client->checkpoint(ckpt_name, passes)) {
        std::cout << "[Task-" << task_id << "] Cannot checkpoint. passes: " << passes << std::endl;
        throw std::runtime_error("Checkpointing failured");
      }

      // Clean reconstruction image before restart
      for(size_t i=0; i<recon_image.count(); ++i)
        recon_image[i]=0.;
      // reload checkpoint to ensure correctness
      ckpt_client->restart(config.ckpt_name, passes);

      ms.acknowledge();
      std::cout << "[task-" << task_id << "]: Checkpointed version " << passes << ", progress = " << progress << std::endl;
      ckpt_mutex->unlock();
    }
    #ifdef TIMERON
    ckpt_tot += (std::chrono::system_clock::now()-ckpt_beg);
    #endif


    /* Emit reconstructed data */
    #ifdef TIMERON
    auto write_beg = std::chrono::system_clock::now();
    #endif
    if(!(passes%config.write_freq)){
      std::stringstream iteration_stream;
      iteration_stream << std::setfill('0') << std::setw(6) << passes;
      
      try {
        TraceMetadata &rank_metadata = curr_slices->metadata();

        int recon_slice_data_index = rank_metadata.num_neighbor_recon_slices()* rank_metadata.num_grids() * rank_metadata.num_grids();
        ADataRegion<float> &recon = rank_metadata.recon();

        hsize_t ndims = static_cast<hsize_t>(h5md.ndims);

        hsize_t rank_dims[3] = {
          static_cast<hsize_t>(rank_metadata.num_slices()),
          static_cast<hsize_t>(rank_metadata.num_cols()),
          static_cast<hsize_t>(rank_metadata.num_cols())};

        data_size = rank_dims[0]*rank_dims[1]*rank_dims[2];
        hsize_t app_dims[3] = {
          static_cast<hsize_t>(h5md.dims[1]),
          static_cast<hsize_t>(h5md.dims[2]),
          static_cast<hsize_t>(h5md.dims[2])};

        json md = json{
            {"Type", "DATA"},
            {"rank", task_id},
            {"iteration_stream", iteration_stream.str()},
            {"rank_dims", rank_dims},
            {"app_dims", app_dims},
            {"recon_slice_data_index", recon_slice_data_index}};

        if (passes % 4 == 0) {
          std::stringstream iteration_stream;
          iteration_stream << ckpt_name << "-" << std::setfill('0') << std::setw(6) << passes;
          std::string outputpath = config.kReconOutputDir + "/" + 
            iteration_stream.str() + "-recon.h5";
          saveAsHDF5(outputpath.c_str(), 
              &recon[recon_slice_data_index], app_dims);
          
        }

        ms.publishImage(md, &recon[recon_slice_data_index], data_size, producer);

      } catch(const mofka::Exception& ex) {
        // spdlog::critical("{}", ex.what());
        std::cerr << "[Task-" << task_id << "] Error during publishing image: " << ex.what() << std::endl;
        exit(-1);
      }
    // MPI_Barrier(MPI_COMM_WORLD);
    }
    #ifdef TIMERON
    write_tot += (std::chrono::system_clock::now()-write_beg);
    #endif
    //delete curr_slices->metadata(); //TODO Check for memory leak
    delete curr_slices;

    if (stop_flag.load()) {
      std::cerr << "Stop flag set. Exiting reconstruction loop..." << std::endl;
      // Call the callback if it exists
      if (on_stop_callback) {
        on_stop_callback();
      }
      break; // Exit the loop if stop flag is set
    }

  }



  auto start = std::chrono::high_resolution_clock::now();
  producer.flush();
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed_t = end - start;
  ms.setProducerTimes("Flush", ms.getBufferSize()*data_size*sizeof(float), elapsed_t.count());
  std::cout << "Flush " << ms.getBatch() << " Time: " << elapsed_t.count() << " sec" << std::endl;
  ms.writeTimes(config.logdir, "producer");
  ms.writeTimes(config.logdir, "consumer");
  // MPI_Barrier(MPI_COMM_WORLD);
  json md = {{"Type", "FIN"}};
  // data part
  float d = 1;
  auto future = producer.push(mofka::Metadata{md}, mofka::Data{&d,sizeof(float)});
  future.wait();


  /**************************/
  #ifdef TIMERON
  if(task_id==0){
    e2e_tot += (std::chrono::system_clock::now()-e2e_beg);
    std::cout << "End-to-End Reconstruction time=" << e2e_tot.count() << std::endl;

    std::cout << "Reconstruction time=" << recon_tot.count() << std::endl;
    std::cout << "Local combination time=" << inplace_tot.count() << std::endl;
    std::cout << "Update time=" << update_tot.count() << std::endl;
    //std::cout << "Write time=" << write_tot.count() << std::endl;
    std::cout << "Data gen total time=" << datagen_tot.count() << std::endl;
    std::cout << "Total comp=" << recon_tot.count() + inplace_tot.count() + update_tot.count() << std::endl;
    std::cout << "Sustained proj/sec=" << ms.getCounter() /
                                          (recon_tot.count()+inplace_tot.count()+update_tot.count()) << std::endl;
  }
  #endif
  /* Clean-up the resources */
  std::cout << "[Task-" << task_id << "] Releasing local resources" << std::endl;
  delete [] h5md.dims;
  delete main_recon_space;
  //delete curr_slices;
  // std::cout << "Deleting comm" << std::endl;
  // delete comm;
  std::cout << "[Task-" << task_id << "] Complete" << std::endl;
  return 0;
}

void ReconTask::stop(std::function<void()> callback) {
  if (callback) {
    on_stop_callback = callback;
  }  
  stop_flag.store(true);
}

void ReconTask::kill(int signal) {
  kill_signal.store(signal);
  ms.interrupt(signal);
}

