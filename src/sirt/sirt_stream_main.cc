#include <iomanip>
#include "mpi.h"
#include "trace_h5io.h"
#include "tclap/CmdLine.h"
#include "disp_comm_mpi.h"
#include "data_region_base.h"
#include "disp_engine_reduction.h"
#include "sirt.h"
#include "trace_stream.h"

class TraceRuntimeConfig {
  public:
    std::string kReconOutputPath;
    std::string kReconDatasetPath;
    std::string kReconOutputDir;
    int thread_count;
    int window_len;
    int window_step;
    int window_iter;
    int write_freq = 0;
    int center;
    std::string dest_host;
    int dest_port;

    TraceRuntimeConfig(int argc, char **argv, int rank, int size){
      try
      {
        TCLAP::CmdLine cmd("SIRT Iterative Image Reconstruction", ' ', "0.01");
        TCLAP::ValueArg<std::string> argReconOutputPath(
          "o", "reconOutputPath", "Output file path for reconstructed image (hdf5)",
          false, "./output.h5", "string");
        TCLAP::ValueArg<std::string> argReconOutputDir(
          "", "recon-output-dir", "Output directory for the streaming outputs",
          false, ".", "string");
        TCLAP::ValueArg<std::string> argReconDatasetPath(
          "r", "reconDatasetPath", "Reconstruction dataset path in hdf5 file",
          false, "/data", "string");
        TCLAP::ValueArg<float> argCenter(
          "c", "center", "Center value", false, 0., "float");
        TCLAP::ValueArg<int> argThreadCount(
          "t", "thread", "Number of threads per process", false, 1, "int");
        TCLAP::ValueArg<float> argWriteFreq(
          "", "write-frequency", "Write frequency", false, 10000, "int");
        TCLAP::ValueArg<float> argWindowLen(
          "", "window-length", "Number of projections that will be stored in the window",
          false, 32, "int");
        TCLAP::ValueArg<float> argWindowStep(
          "", "window-step", "Number of projections that will be received in each request",
          false, 1, "int");
        TCLAP::ValueArg<float> argWindowIter(
          "", "window-iter", "Number of iterations on received window",
          false, 1, "int");
        TCLAP::ValueArg<std::string> argDestHost(
          "", "dest-host", "Destination host/ip address", false, "164.54.143.3", 
            "string");
        TCLAP::ValueArg<float> argDestPort(
          "", "dest-port", "Starting port of destination host", false, 5560, "int");

        cmd.add(argReconOutputPath);
        cmd.add(argReconOutputDir);
        cmd.add(argReconDatasetPath);
        cmd.add(argCenter);
        cmd.add(argThreadCount);
        cmd.add(argWriteFreq);
        cmd.add(argWindowLen);
        cmd.add(argWindowStep);
        cmd.add(argWindowIter);
        cmd.add(argDestHost);
        cmd.add(argDestPort);

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
        dest_host= argDestHost.getValue();
        dest_port= argDestPort.getValue();

        std::cout << "MPI rank:"<< rank << "; MPI size:" << size << std::endl;
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
          std::cout << "Destination host address=" << dest_host << std::endl;
          std::cout << "Destination port=" << dest_port << std::endl;
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
  TraceStream tstream(config.dest_host, config.dest_port, config.window_len, 
    comm->rank(), comm->size());

  /* Get metadata structure */
  tomo_msg_metadata_t tmetadata = (tomo_msg_metadata_t)tstream.metadata();
  int n_blocks = tmetadata.n_sinograms;
  int num_cols = tmetadata.n_rays_per_proj_row;

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

  DataRegionBase<float, TraceMetadata> *curr_slices = nullptr;
  /// Reconstructed image
  DataRegionBareBase<float> recon_image(n_blocks*num_cols*num_cols);  
  for(size_t i=0; i<recon_image.count(); ++i) 
    recon_image[i]=0.; /// Initial values of the reconstructe image
  #endif

  /// Number of requested ray-sum values by each thread poll
  int64_t req_number = num_cols; 
  /// Required data structure for dumping image to h5 file
  trace_io::H5Metadata h5md; 
  h5md.ndims=3; 
  h5md.dims= new hsize_t[3];
  h5md.dims[1] = tmetadata.tn_sinograms; 
  h5md.dims[0] = 0;   /// Number of projections is unknown
  h5md.dims[2] = tmetadata.n_rays_per_proj_row; 
  for(int passes=0; ; ++passes){
      #ifdef TIMERON
      auto datagen_beg = std::chrono::system_clock::now();
      #endif
      curr_slices = tstream.ReadSlidingWindow(recon_image, config.window_step);
      if(config.center!=0 && curr_slices!=nullptr) 
        curr_slices->metadata().center(config.center);
      #ifdef TIMERON
      datagen_tot += (std::chrono::system_clock::now()-datagen_beg);
      #endif

      if(curr_slices == nullptr) break; /// If nullptr, there is no more projection 

      /// Check window effect
      /// and iteration
      /*
      if((passes/config.write_freq)>793){
        tstream.WindowLength(5);
        config.window_iter=5;
      }
      */

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
      }
      #ifdef TIMERON
      write_tot += (std::chrono::system_clock::now()-write_beg);
      #endif

      //delete curr_slices->metadata(); //TODO Check for memory leak
      delete curr_slices;
  }

  /**************************/
  if(comm->rank()==0){
    std::cout << "Reconstruction time=" << recon_tot.count() << std::endl;
    std::cout << "Local combination time=" << inplace_tot.count() << std::endl;
    std::cout << "Update time=" << update_tot.count() << std::endl;
    std::cout << "Write time=" << write_tot.count() << std::endl;
    std::cout << "Data gen total time=" << datagen_tot.count() << std::endl;
    std::cout << "Total comp=" << recon_tot.count() + inplace_tot.count() + update_tot.count() << std::endl;
    std::cout << "Sustained proj/sec=" << tstream.counter() / 
                                          (recon_tot.count()+inplace_tot.count()+update_tot.count()) << std::endl;
  }

  /* Clean-up the resources */
  delete [] h5md.dims;
  //delete main_recon_space;
  //delete curr_slices;
  delete comm;
  delete engine;
}

