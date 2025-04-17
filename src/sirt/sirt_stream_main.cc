#include <iomanip>
#include "mpi.h"
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
#include <mofka_stream.h>
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

// Define an alias for the instantiated template class
using DISPEngineReductionSIRT = DISPEngineReduction<SIRTReconSpace, float>;
using DISPEngineBaseSIRT = DISPEngineBase<SIRTReconSpace, float>;
using AReductionSpaceBaseSIRT = AReductionSpaceBase<SIRTReconSpace, float>;

// Register the derived class
BOOST_CLASS_EXPORT(AReductionSpaceBaseSIRT)
BOOST_CLASS_EXPORT(DISPEngineBaseSIRT)
BOOST_CLASS_EXPORT(DISPEngineReductionSIRT)

class TraceRuntimeConfig {
  public:
    std::string kReconOutputPath;
    std::string kReconDatasetPath;
    std::string kReconOutputDir;
    std::string protocol;
    std::string group_file;
    size_t batchsize;
    int thread_count;
    int window_len;
    int window_step;
    int window_iter;
    int write_freq = 0;
    int center;
    std::string dest_host;
    int dest_port;
    std::string pub_addr;
    int pub_freq = 0;
    std::string task_id;
    int task_index;
    int num_tasks;
    int num_passes;
    int ckpt_freq = 1;
    std::string ckpt_config;
    std::string ckpt_name;
    std::string logdir = ".";

    TraceRuntimeConfig(int argc, char **argv){
      try
      {
        TCLAP::CmdLine cmd("SIRT Iterative Image Reconstruction", ' ', "0.01");
        TCLAP::ValueArg<std::string> argTaskId(
          "", "id", "The Task Id", false, "0", "string");
          TCLAP::ValueArg<int> argNumTasks(
          "", "np", "Number of Reconstruction Task/Process", false, 1, "int");
        TCLAP::ValueArg<std::string> argMofkaProtocol(
          "", "protocol", "Mofka protocol", false, "na+sm", "string");
        TCLAP::ValueArg<std::string> argGroupFile(
          "", "group-file", "Mofka group file", false, "mofka.json", "string");
        TCLAP::ValueArg<size_t> argBatchSize(
          "", "batchsize", "Mofka batchsize", false, 1, "size_t");
        TCLAP::ValueArg<std::string> argReconOutputPath(
          "o", "reconOutputPath", "Output file path for reconstructed image (hdf5)",
          false, "./output.h5", "string");
        TCLAP::ValueArg<std::string> argReconOutputDir(
          "", "recon-output-dir", "Output directory for the streaming outputs",
          false, ".", "string");
        TCLAP::ValueArg<std::string> argReconDatasetPath(
          "r", "reconDatasetPath", "Reconstruction dataset path in hdf5 file",
          false, "/data", "string");
        TCLAP::ValueArg<float> argPubFreq(
          "", "pub-freq", "Publish frequency", false, 10000, "int");
        TCLAP::ValueArg<float> argCenter(
          "c", "center", "Center value", false, 0., "float");
        TCLAP::ValueArg<int> argThreadCount(
          "t", "thread", "Number of threads per process", false, 1, "int");
        TCLAP::ValueArg<float> argWriteFreq(
          "", "write-freq", "Write frequency", false, 10000, "int");
        TCLAP::ValueArg<float> argWindowLen(
          "", "window-length", "Number of projections that will be stored in the window",
          false, 32, "int");
        TCLAP::ValueArg<float> argWindowStep(
          "", "window-step", "Number of projections that will be received in each request",
          false, 1, "int");
        TCLAP::ValueArg<float> argWindowIter(
          "", "window-iter", "Number of iterations on received window",
          false, 1, "int");
        TCLAP::ValueArg<int> argNumPasses(
          "", "num-passes", "Number of passes on data streams",
          // false, 201, "int");
          false, 4001, "int");
          // false, 21, "int");
          // false, 41, "int");
        TCLAP::ValueArg<std::string> argLogDir(
          "", "logdir", "Log directory", false, ".", "string");
        TCLAP::ValueArg<int> argCkptFreq(
          "", "ckpt-freq", "Checkpoint frequency", false, 1, "int");
        TCLAP::ValueArg<std::string> argCkptConfig(
          "", "ckpt-config", "Checkpoint Configuration (VeloC)", false, "veloc.cfg", "string");
        TCLAP::ValueArg<std::string> argCkptName(
          "", "ckpt-name", "Checkpoint Name (VeloC)", false, "sirt-ckpt", "string");

        cmd.add(argTaskId);
        cmd.add(argNumTasks);

        cmd.add(argMofkaProtocol);
        cmd.add(argGroupFile);
        cmd.add(argBatchSize);
        cmd.add(argReconOutputPath);
        cmd.add(argReconOutputDir);
        cmd.add(argReconDatasetPath);

        cmd.add(argPubFreq);

        cmd.add(argCenter);
        cmd.add(argThreadCount);
        cmd.add(argWriteFreq);
        cmd.add(argWindowLen);
        cmd.add(argWindowStep);
        cmd.add(argWindowIter);
        cmd.add(argNumPasses);

        cmd.add(argLogDir);

        cmd.add(argCkptFreq);
        cmd.add(argCkptConfig);
        cmd.add(argCkptName);

        cmd.parse(argc, argv);

        task_id = argTaskId.getValue();
        std::from_chars(task_id.data(), task_id.data() + task_id.size(), task_index);
        num_tasks = argNumTasks.getValue();
        kReconOutputPath = argReconOutputPath.getValue();
        kReconOutputDir = argReconOutputDir.getValue();
        kReconDatasetPath = argReconDatasetPath.getValue();
        center = argCenter.getValue();
        thread_count = argThreadCount.getValue();
        write_freq= argWriteFreq.getValue();
        window_len= argWindowLen.getValue();
        window_step= argWindowStep.getValue();
        window_iter= argWindowIter.getValue();
        num_passes = argNumPasses.getValue();

        protocol = argMofkaProtocol.getValue();
        batchsize = argBatchSize.getValue();
        group_file = argGroupFile.getValue();
        ckpt_freq = argCkptFreq.getValue();
        ckpt_config = argCkptConfig.getValue();
        ckpt_name = argCkptName.getValue();
        logdir = argLogDir.getValue();

        // std::cout << "MPI rank:"<< rank << "; MPI size:" << size << "; PID:" << getpid() << std::endl;
        std::cout << "Task ID: " << task_id << " Task Index: " << task_index << "; Number of Tasks: " << num_tasks << "; PID: " << getpid() << std::endl;
        if(task_index==0)
        {
          std::cout << "Output file path=" << kReconOutputPath << std::endl;
          std::cout << "Output dir path=" << kReconOutputDir << std::endl;
          std::cout << "Recon. dataset path=" << kReconDatasetPath << std::endl;
          std::cout << "Center value=" << center << std::endl;
          std::cout << "Number of threads per process=" << thread_count << std::endl;
          std::cout << "Write frequency=" << write_freq << std::endl;
          std::cout << "Window length=" << window_len << std::endl;
          std::cout << "Window step=" << window_step << std::endl;
          std::cout << "Window iter=" << window_iter << std::endl;
          std::cout << "Publish frequency=" << pub_freq << std::endl;
          std::cout << "Mofka Protocol=" << protocol << std::endl;
          std::cout << "Mofka batchsize=" << batchsize << std::endl;
          std::cout << "Group file=" << group_file << std::endl;
        }
      }
      catch (TCLAP::ArgException &e)
      {
        std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
      }
    }
};

volatile std::sig_atomic_t sigterm_captured = 0;
void handle_sigterm(int signum) {
    std::cerr << "Received SIGTERM, cleaning up resources..." << std::endl;
    sigterm_captured = signum;
}

int main(int argc, char **argv)
{
  std::signal(SIGTERM, handle_sigterm);

  /* Initiate middleware's communication layer */
  TraceRuntimeConfig config(argc, argv);
  MofkaStream ms = MofkaStream{ config.group_file,
                                config.batchsize,
                                static_cast<uint32_t>(config.window_len),
                                config.task_index,
                                config.num_tasks,
                                0}; // Add the missing progress argument

  ms.handshake(config.task_index, config.num_tasks);

  // Prepare consumer and producer
  std::string consuming_topic = "dist_sirt";
  std::string producing_topic = "sirt_den";
  std::vector<size_t> targets = {static_cast<size_t>(config.task_index)};

  mofka::Producer  producer = ms.getProducer(producing_topic, "sirt");
  mofka::Consumer consumer = ms.getConsumer(consuming_topic, "sirt", targets);
  /* Get metadata structure */
  json tmetadata = ms.getInfo();
  auto n_blocks = tmetadata["n_sinograms"].get<int64_t>();
  auto num_cols = tmetadata["n_rays_per_proj_row"].get<int64_t>();

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

  // Configure the VeloC checkpointing
  veloc::client_t *ckpt_client = veloc::get_client((unsigned int)config.task_index, config.ckpt_config);
  // Protect reconstruction memory regions
  int progress = 0; // Reconstruction progress marked by the projection requence ids
  ckpt_client->mem_protect(0, veloc::boost::serializer(recon_image), veloc::boost::deserializer(recon_image));
  ckpt_client->mem_protect(1, &progress, 1, sizeof(int));

  int passes = ckpt_client->restart_test(config.ckpt_name, 0, config.task_index);
  // Checkpoint restart if any
  if(passes>0){
    std::cout << "Checkpoint found at " << passes << ". Restarting from checkpoint" << std::endl;
    ckpt_client->restart(config.ckpt_name, passes);
    ms.updateProgress(progress);
    std::cout << "Restarted from checkpoint at iteration " << passes << ", progress = " << progress << std::endl;
  }else{
    std::cout << "No checkpoint found. Starting from scratch" << std::endl;
    passes = 0;
  }
  
  DataRegion2DBareBase<float> &main_recon_replica = main_recon_space->reduction_objects();
  float init_val=0.;
  if (progress == 0) {
    main_recon_replica.ResetAllItems(init_val);
  }

  /* Prepare processing engine and main reduction space for other threads */
  DISPEngineBase<SIRTReconSpace, float> *engine =
    // new DISPEngineReduction<SIRTReconSpace, float>(
    new DISPEngineReductionSIRT(
        // comm,
        main_recon_space,
        config.thread_count);
        /// # threads (0 for auto assign the number of threads)

  #ifdef TIMERON
  auto e2e_beg = std::chrono::system_clock::now();
  #endif

  std::cout << "[Task-" << config.task_id << "] Start reconstruction passes = " << passes << std::endl;

  for(; passes < config.num_passes; ++passes){

      if (sigterm_captured) {
          std::cerr << "Termination signal received. Exiting..." << std::endl;
          return sigterm_captured;
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
        std::cout << "[Task-" << config.task_id << "] End of stream. Exiting..." << std::endl;
        break;
      }
      if(curr_slices == nullptr) {
        std::cout << "[Task-" << config.task_id << "] passes = " << passes << " -- No new data in the sliding window. Skip processing" << std::endl;
        continue;
      }
      /// Iterate on window
      for(int i=0; i<config.window_iter; ++i){
        if (sigterm_captured) {
            std::cerr << "Termination signal received. Exiting..." << std::endl;
            return sigterm_captured;
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
      if(!(passes%config.ckpt_freq)){
        ckpt_client->checkpoint_wait();
        progress = ms.getProgress();
        std::cout << "[Task-" << config.task_id << "] Checkpointing at iteration " << passes << ", proogess = " << progress << std::endl;
        if (!ckpt_client->checkpoint(config.ckpt_name, passes)) {
          std::cout << "[Task-" << config.task_id << "] Cannot checkpoint. passes: " << passes << std::endl;
          throw std::runtime_error("Checkpointing failured");
        }
        ms.acknowledge();
        std::cout << "[task-" << config.task_id << "]: Checkpointed version " << passes << ", proogess = " << progress << std::endl;
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
        
        // std::string outputpath = config.kReconOutputDir + "/" +
        //   iteration_stream.str() + "-recon.h5";
        // trace_io::WriteRecon(
        //     curr_slices->metadata(), h5md,
        //     outputpath, config.kReconDatasetPath);

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
              {"rank", config.task_index},
              {"iteration_stream", iteration_stream.str()},
              {"rank_dims", rank_dims},
              {"app_dims", app_dims},
              {"recon_slice_data_index", recon_slice_data_index}};

          ms.publishImage(md, &recon[recon_slice_data_index], data_size, producer);

        } catch(const mofka::Exception& ex) {
          spdlog::critical("{}", ex.what());
          exit(-1);
        }
      // MPI_Barrier(MPI_COMM_WORLD);
      }
      #ifdef TIMERON
      write_tot += (std::chrono::system_clock::now()-write_beg);
      #endif
      //delete curr_slices->metadata(); //TODO Check for memory leak
      delete curr_slices;
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
  if(config.task_index==0){
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
  std::cout << "Deleting h5md.dimm" << std::endl;
  delete [] h5md.dims;
  std::cout << "Deleting main_recon_space" << std::endl;
  delete main_recon_space;
  //delete curr_slices;
  // std::cout << "Deleting comm" << std::endl;
  // delete comm;
  std::cout << "Exiting" << std::endl;
}