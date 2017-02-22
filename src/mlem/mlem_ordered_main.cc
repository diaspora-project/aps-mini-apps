#include <iomanip>
#include "mpi.h"
#include "trace_h5io.h"
#include "tclap/CmdLine.h"
#include "disp_comm_mpi.h"
#include "data_region_base.h"
#include "disp_engine_reduction.h"
#include "mlem.h"
#include "trace_stream.h"

class TraceRuntimeConfig {
  public:
    std::string kProjectionFilePath;
    std::string kProjectionDatasetPath;
    std::string kThetaFilePath;
    std::string kThetaDatasetPath;
    std::string kReconOutputPath;
    std::string kReconDatasetPath;
    std::string kReconOutputDir;
    int iteration;
    float center;
    int thread_count;
    int subsets = 1;

    int subset_iteration = 1;
    int block_remove_subsets = 1;
    int block_remove_iteration = 1;
    int write_freq = 0;
    int write_block_freq = 0;

    TraceRuntimeConfig(int argc, char **argv, int rank, int size){
      try
      {
        TCLAP::CmdLine cmd("MLEM Iterative Image Reconstruction", ' ', "0.01");
        TCLAP::ValueArg<std::string> argProjectionFilePath(
          "f", "projectionFilePath", "Projection file path", true, "", "string");
        TCLAP::ValueArg<std::string> argProjectionDatasetPath(
          "d", "projectionDatasetPath", "Projection dataset path in hdf5", false,
          "/exchange/data", "string");
        TCLAP::ValueArg<std::string> argThetaFilePath(
          "e", "thetaFilePath", "Theta file path", true, "", "string");
        TCLAP::ValueArg<std::string> argThetaDatasetPath(
          "q", "thetaDatasetPath", "Theta dataset path", false, "/exchange/theta",
          "string");
        TCLAP::ValueArg<std::string> argReconOutputPath(
          "o", "reconOutputPath", "Output file path for reconstructed image (hdf5)",
          false, "./output.h5", "string");
        TCLAP::ValueArg<std::string> argReconOutputDir(
          "", "recon-output-dir", "Output directory for the streaming outputs",
          false, ".", "string");
        TCLAP::ValueArg<std::string> argReconDatasetPath(
          "r", "reconDatasetPath", "Reconstruction dataset path in hdf5 file",
          false, "/data", "string");
        TCLAP::ValueArg<int> argIteration(
          "i", "iteration", "Number of iterations", true, 0, "int");
        TCLAP::ValueArg<int> argSubsetIteration(
          "", "subset-iteration", "Number of iterations for on subset", false, 1, "int");
        TCLAP::ValueArg<int> argBlockRemoveIteration(
          "", "block-remove-iteration", "Number of iterations for removing subset blocks on 3D image", false, 0, "int");
        TCLAP::ValueArg<float> argCenter(
          "c", "center", "Center value", false, 0., "float");
        TCLAP::ValueArg<int> argThreadCount(
          "t", "thread", "Number of threads per process", false, 1, "int");
        TCLAP::ValueArg<float> argSubsets(
          "", "subsets", "Ordered subsets", false, 1, "int");
        TCLAP::ValueArg<float> argBlockRemoveSubsets(
          "", "block-remove-subsets", "Number of subsets for removing block lines", false, 1, "int");
        TCLAP::ValueArg<float> argWriteFreq(
          "", "write-frequency", "Write frequency", false, 0, "int");
        TCLAP::ValueArg<float> argWriteBlockFreq(
          "", "write-block-frequency", "Write frequency during subset block cleaning", false, 0, "int");

        cmd.add(argProjectionFilePath);
        cmd.add(argProjectionDatasetPath);
        cmd.add(argThetaFilePath);
        cmd.add(argThetaDatasetPath);
        cmd.add(argReconOutputPath);
        cmd.add(argReconOutputDir);
        cmd.add(argReconDatasetPath);
        cmd.add(argIteration);
        cmd.add(argCenter);
        cmd.add(argThreadCount);
        cmd.add(argSubsets);
        cmd.add(argBlockRemoveSubsets);
        cmd.add(argBlockRemoveIteration);
        cmd.add(argWriteFreq);
        cmd.add(argWriteBlockFreq);
        cmd.add(argSubsetIteration);

        cmd.parse(argc, argv);
        kProjectionFilePath = argProjectionFilePath.getValue();
        kProjectionDatasetPath = argProjectionDatasetPath.getValue();
        kThetaFilePath = argThetaFilePath.getValue();
        kThetaDatasetPath = argThetaDatasetPath.getValue();
        kReconOutputPath = argReconOutputPath.getValue();
        kReconOutputDir = argReconOutputDir.getValue();
        kReconDatasetPath = argReconDatasetPath.getValue();
        iteration = argIteration.getValue();
        center = argCenter.getValue();
        thread_count = argThreadCount.getValue();
        subsets = argSubsets.getValue();
        block_remove_subsets = argBlockRemoveSubsets.getValue();
        block_remove_iteration = argBlockRemoveIteration.getValue();
        write_freq= argWriteFreq.getValue();
        write_block_freq= argWriteBlockFreq.getValue();
        subset_iteration = argSubsetIteration.getValue();

        std::cout << "MPI rank:"<< rank << "; MPI size:" << size << std::endl;
        if(rank==0)
        {
          std::cout << "Projection file path=" << kProjectionFilePath << std::endl;
          std::cout << "Projection dataset path in hdf5=" << kProjectionDatasetPath << std::endl;
          std::cout << "Theta file path=" << kThetaFilePath << std::endl;
          std::cout << "Theta dataset path=" << kThetaDatasetPath << std::endl;
          std::cout << "Output file path=" << kReconOutputPath << std::endl;
          std::cout << "Output dir path=" << kReconOutputDir << std::endl;
          std::cout << "Recon. dataset path=" << kReconDatasetPath << std::endl;
          std::cout << "Number of iterations=" << iteration << std::endl;
          std::cout << "Center value=" << center << std::endl;
          std::cout << "Number of threads per process=" << thread_count << std::endl;
          std::cout << "Subsets=" << subsets  << std::endl;
          std::cout << "Number of subset iterations=" << subset_iteration << std::endl;
          std::cout << "Block remove subsets=" << block_remove_subsets  << std::endl;
          std::cout << "Number of block remove iterations=" << block_remove_iteration << std::endl;
          std::cout << "Write frequency=" << write_freq << std::endl;
          std::cout << "Write block frequency=" << write_block_freq << std::endl;
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

  #ifdef TIMERON
  std::chrono::duration<double> read_tot(0.);
  auto read_beg = std::chrono::system_clock::now();
  #endif

  /* Read slice data and setup job information */
  auto d_metadata = trace_io::ReadMetadata(
        config.kProjectionFilePath.c_str(), 
        config.kProjectionDatasetPath.c_str());
  int beg_index, n_blocks;
  trace_io::DistributeSlices(
      comm->rank(), comm->size(), 
      d_metadata->dims[1], beg_index, n_blocks);
  auto input_slice = 
    trace_io::ReadSlices(d_metadata, beg_index, n_blocks, 0);

  /* Read theta data */
  auto t_metadata = trace_io::ReadMetadata(
        config.kThetaFilePath.c_str(), 
        config.kThetaDatasetPath.c_str());
  auto theta = trace_io::ReadTheta(t_metadata);
  #ifdef TIMERON
  read_tot += (std::chrono::system_clock::now()-read_beg);
  #endif
  /* Convert degree values to radian */
  trace_utils::DegreeToRadian(*theta);
  size_t ray_count = 
    input_slice->metadata->dims[0]*input_slice->count*input_slice->metadata->dims[2]; 
  trace_utils::RemoveNegatives(
      static_cast<float *>(input_slice->data),
      ray_count);
  trace_utils::RemoveAbnormals(
      static_cast<float *>(input_slice->data),
      ray_count);

  /* Setup metadata data structure */
  // INFO: TraceMetadata destructor frees theta->data!
  // TraceMetadata internally creates reconstruction object
  TraceMetadata trace_metadata(
      static_cast<float *>(theta->data),  /// float const *theta,
      0,                                  /// int const proj_id,
      beg_index,                          /// int const slice_id,
      0,                                  /// int const col_id,
      input_slice->metadata->dims[1],     /// int const num_tot_slices,
      input_slice->metadata->dims[0],     /// int const num_projs,
      n_blocks,                           /// int const num_slices,
      input_slice->metadata->dims[2],     /// int const num_cols,
      input_slice->metadata->dims[2],     /// int const num_grids,
      config.center,                      /// float const center,
      0,                                  /// int const num_neighbor_recon_slices,
      1.);                                /// float const recon_init_val

  // INFO: DataRegionBase destructor deletes input_slice.data pointer
  DataRegionBase<float, TraceMetadata> *slices = 
    new DataRegionBase<float, TraceMetadata>(
        static_cast<float *>(input_slice->data),
        trace_metadata.count(),
        &trace_metadata);

  /***********************/
  /* Initiate middleware */
  /* Prepare main reduction space and its objects */
  /* The size of the reconstruction object (in reconstruction space) is
   * twice the reconstruction object size, because of the length storage
   */
  auto main_recon_space = new MLEMReconSpace(
      n_blocks, 
      2*trace_metadata.num_cols()*trace_metadata.num_cols());
  main_recon_space->Initialize(trace_metadata.num_grids());
  auto &main_recon_replica = main_recon_space->reduction_objects();
  float init_val=0.;
  main_recon_replica.ResetAllItems(init_val);

  /* Prepare processing engine and main reduction space for other threads */
  DISPEngineBase<MLEMReconSpace, float> *engine =
    new DISPEngineReduction<MLEMReconSpace, float>(
        comm,
        main_recon_space,
        config.thread_count); 
          /// # threads (0 for auto assign the number of threads)
  /**********************/

  /**************************/
  /* Perform reconstruction */
  /* Define job size per thread request */
  int64_t req_number = trace_metadata.num_cols();

  #ifdef TIMERON
  std::chrono::duration<double> recon_tot(0.), inplace_tot(0.), update_tot(0.), 
    datagen_tot(0.);
  std::chrono::duration<double> write_tot(0.);
  #endif
  TraceStream projection_stream(*slices, config.subsets, config.subset_iteration); 
  DataRegionBase<float, TraceMetadata> *curr_slices = nullptr;

  for(int i=0; i<config.iteration; ++i){
    std::cout << "Iteration=" << i << std::endl;
    while(true){
        #ifdef TIMERON
        auto datagen_beg = std::chrono::system_clock::now();
        #endif
        curr_slices = projection_stream.ReadOrderedSubsetting();
        #ifdef TIMERON
        datagen_tot += (std::chrono::system_clock::now()-datagen_beg);
        #endif
        if(curr_slices == nullptr) break;

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
        main_recon_space->UpdateRecon(trace_metadata.recon(), main_recon_replica);
        #ifdef TIMERON
        update_tot += (std::chrono::system_clock::now()-update_beg);
        #endif

        std::cout << "Current proj id=" << projection_stream.curr_proj_index() << std::endl;

        /// Reset iteration
        engine->ResetReductionSpaces(init_val);
    }
      #ifdef TIMERON
      auto write_beg = std::chrono::system_clock::now();
      #endif
      if(!(i%config.write_freq)){
      //if(!(projection_stream.curr_proj_index() % 16)){
        std::stringstream iteration_stream;
        iteration_stream << std::setfill('0') << std::setw(6) <<//projection_stream.curr_proj_index();
                                                                //projection_stream.curr_iteration();
                                                                i;
        std::string outputpath = config.kReconOutputDir + "/" + iteration_stream.str() + "-recon.h5";
        trace_io::WriteRecon(
            trace_metadata, *d_metadata, 
            outputpath, 
            config.kReconDatasetPath);
      }
      #ifdef TIMERON
      write_tot += (std::chrono::system_clock::now()-write_beg);
      #endif
      projection_stream.Reset();
  }
  /**************************/

  /* Write reconstructed data to disk */
  #ifdef TIMERON
  //std::chrono::duration<double> write_tot(0.);
  auto write_beg = std::chrono::system_clock::now();
  #endif
  std::string outputpath_subset =  config.kReconOutputDir + "/" +"output-subset-recon.h5";
  trace_io::WriteRecon(
      trace_metadata, *d_metadata, 
      outputpath_subset, 
      config.kReconDatasetPath);
  #ifdef TIMERON
  write_tot += (std::chrono::system_clock::now()-write_beg);
  #endif

  TraceStream block_remove_stream(*slices, config.block_remove_subsets, config.subset_iteration); 
  for(int i=0; i<config.block_remove_iteration; ++i){
    std::cout << "Iteration=" << i << std::endl;
    while(true){
        #ifdef TIMERON
        auto datagen_beg = std::chrono::system_clock::now();
        #endif
        curr_slices = block_remove_stream.ReadOrderedSubsetting();
        #ifdef TIMERON
        datagen_tot += (std::chrono::system_clock::now()-datagen_beg);
        #endif
        if(curr_slices == nullptr) break;

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
        main_recon_space->UpdateRecon(trace_metadata.recon(), main_recon_replica);
        #ifdef TIMERON
        update_tot += (std::chrono::system_clock::now()-update_beg);
        #endif

        std::cout << "Current proj id=" << block_remove_stream.curr_proj_index() << std::endl;

        /// Reset iteration
        engine->ResetReductionSpaces(init_val);
    }
    #ifdef TIMERON
    auto write_beg = std::chrono::system_clock::now();
    #endif
    if(!(i%config.write_block_freq)){
      std::stringstream iteration_stream;
      iteration_stream << std::setfill('0') << std::setw(6) <<//projection_stream.curr_proj_index();
                                                              //projection_stream.curr_iteration();
                                                              i;
      std::string outputpath =  config.kReconOutputDir + "/" +iteration_stream.str() + "-br-recon.h5";
      trace_io::WriteRecon(
          trace_metadata, *d_metadata, 
          outputpath, 
          config.kReconDatasetPath);
    }
    #ifdef TIMERON
    write_tot += (std::chrono::system_clock::now()-write_beg);
    #endif
    projection_stream.Reset();
    #ifdef TIMERON
  }

  std::string outputpath_cleaned =  config.kReconOutputDir + "/" +"output-cleaned-recon.h5";
  trace_io::WriteRecon(
      slices->metadata(), *d_metadata, 
      outputpath_cleaned, 
      config.kReconDatasetPath);

  /**************************/
  //write_tot += (std::chrono::system_clock::now()-write_beg);
  if(comm->rank()==0){
    std::cout << "Reconstruction time=" << recon_tot.count() << std::endl;
    std::cout << "Local combination time=" << inplace_tot.count() << std::endl;
    std::cout << "Update time=" << update_tot.count() << std::endl;
    std::cout << "Read time=" << read_tot.count() << std::endl;
    std::cout << "Write time=" << write_tot.count() << std::endl;
    std::cout << "Data gen total time=" << datagen_tot.count() << std::endl;
  }
  #endif

  /* Clean-up the resources */
  delete d_metadata->dims;
  delete d_metadata;
  delete slices;
  delete t_metadata->dims;
  delete t_metadata;
  delete theta;
  delete engine;
  delete input_slice;
}


