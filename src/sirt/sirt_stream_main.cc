#include <iomanip>
#include "mpi.h"
#include "trace_h5io.h"
#include "data_region_base.h"
#include "tclap/CmdLine.h"
#include "disp_comm_mpi.h"
#include "disp_engine_reduction.h"
#include "sirt.h"
#include <cassert>
#include <spdlog/spdlog.h>
#include <time.h>
#include <string>
#include <fstream>
#include <iostream>
#include <mofka_stream.h>
#include "trace_data.h"
#include <vector>
#include <unistd.h>

#define TIMERON 1

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

    TraceRuntimeConfig(int argc, char **argv, int rank, int size){
      try
      {
        TCLAP::CmdLine cmd("SIRT Iterative Image Reconstruction", ' ', "0.01");
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

        cmd.parse(argc, argv);
        kReconOutputPath = argReconOutputPath.getValue();
        kReconOutputDir = argReconOutputDir.getValue();
        kReconDatasetPath = argReconDatasetPath.getValue();
        center = argCenter.getValue();
        thread_count = argThreadCount.getValue();
        write_freq= argWriteFreq.getValue();
        window_len= argWindowLen.getValue();
        window_step= argWindowStep.getValue();
        window_iter= argWindowIter.getValue();
        protocol = argMofkaProtocol.getValue();
        batchsize = argBatchSize.getValue();
        group_file = argGroupFile.getValue();

        std::cout << "MPI rank:"<< rank << "; MPI size:" << size << "; PID:" << getpid() << std::endl;
        if(rank==0)
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

int main(int argc, char **argv)
{
  /* Initiate middleware's communication layer */
  DISPCommBase<float> *comm =
        new DISPCommMPI<float>(&argc, &argv);
  TraceRuntimeConfig config(argc, argv, comm->rank(), comm->size());
  MofkaStream ms = MofkaStream{ config.group_file,
                                config.batchsize,
                                static_cast<uint32_t>(config.window_len),
                                comm->rank(),
                                comm->size()};

  ms.handshake(comm->rank(), comm->size());
  std::string consuming_topic = "dist_sirt";
  std::string producing_topic = "sirt_den";
  std::vector<size_t> targets = {static_cast<size_t>(comm->rank())};
  mofka::Producer  producer = ms.getProducer(producing_topic, "sirt");
  mofka::Consumer consumer = ms.getConsumer(consuming_topic, "sirt", targets);
  /* Get metadata structure */
  json tmetadata = ms.getInfo();
  auto n_blocks = tmetadata["n_sinograms"].get<int64_t>();
  auto num_cols = tmetadata["n_rays_per_proj_row"].get<int64_t>();
  /***********************/
  /* Initiate middleware */
  /* Prepare main reduction space and its objects */
  /* The size of the reconstruction object (in reconstruction space) is
   * twice the reconstruction object size, because of the length storage
   */
  auto main_recon_space = new SIRTReconSpace(
      n_blocks, 2*num_cols*num_cols);
  main_recon_space->Initialize(num_cols*num_cols);
  auto &main_recon_replica = main_recon_space->reduction_objects();
  float init_val=0.;
  main_recon_replica.ResetAllItems(init_val);
  /* Prepare processing engine and main reduction space for other threads */
  DISPEngineBase<SIRTReconSpace, float> *engine =
    new DISPEngineReduction<SIRTReconSpace, float>(
        comm,
        main_recon_space,
        config.thread_count);
        /// # threads (0 for auto assign the number of threads)

  /**********************/

  /**************************/
  /* Perform reconstruction */
  /* Define job size per thread request */
  #ifdef TIMERON
  std::chrono::duration<double> recon_tot(0.), inplace_tot(0.), update_tot(0.),
    datagen_tot(0.);
  std::chrono::duration<double> write_tot(0.);
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
  for(int passes=0; ; ++passes){
      #ifdef TIMERON
      auto datagen_beg = std::chrono::system_clock::now();
      #endif
      curr_slices = ms.readSlidingWindow(recon_image, config.window_step, consumer);
      if(config.center!=0 && curr_slices!=nullptr)
        curr_slices->metadata().center(config.center);
      #ifdef TIMERON
      datagen_tot += (std::chrono::system_clock::now()-datagen_beg);
      #endif

      if(curr_slices == nullptr) break; /// If nullptr, there is no more projections
      /// Iterate on window
      for(int i=0; i<config.window_iter; ++i){
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

      /* Emit reconstructed data */
      #ifdef TIMERON
      auto write_beg = std::chrono::system_clock::now();
      #endif
      if(!(passes%config.write_freq)){
        std::stringstream iteration_stream;
        iteration_stream << std::setfill('0') << std::setw(6) << passes;
        std::string outputpath = config.kReconOutputDir + "/" +
          iteration_stream.str() + "-recon.h5";

        trace_io::WriteRecon(
            curr_slices->metadata(), h5md,
            outputpath, config.kReconDatasetPath);

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

          json md = { {"Type", "DATA"},
                      {"rank", comm->rank()},
                      {"iteration_stream", iteration_stream.str()},
                      {"rank_dims", rank_dims},
                      {"app_dims", app_dims},
                      {"recon_slice_data_index", recon_slice_data_index}};

          ms.publishImage(md, &recon[recon_slice_data_index], data_size, producer);

        } catch(const mofka::Exception& ex) {
          spdlog::critical("{}", ex.what());
          exit(-1);
        }
      MPI_Barrier(MPI_COMM_WORLD);
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
  ms.writeTimes("producer");
  ms.writeTimes("consumer");
  MPI_Barrier(MPI_COMM_WORLD);
  json md = {{"Type", "FIN"}};
  // data part
  float d = 1;
  auto future = producer.push(mofka::Metadata{md}, mofka::Data{&d,sizeof(float)});
  future.wait();

  /**************************/
  #ifdef TIMERON
  if(comm->rank()==0){
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
  std::cout << "Deleting comm" << std::endl;
  delete comm;
  std::cout << "Exiting" << std::endl;
}