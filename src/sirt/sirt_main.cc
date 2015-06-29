#include "mpi.h"
#include "trace_h5io.h"
#include "trace_utils.h"
#include "disp_comm_mpi.h"
#include "data_region_base.h"
#include "disp_engine_reduction.h"
#include "sirt.h"

std::string const kProjectionFilePath="/Users/bicer/Projects/data/original/13-ID/13id1_fixed_1s.h5";
//std::string const kProjectionFilePath="/Users/bicer/Projects/data/original/13-ID/13id1_fixed_16s.h5";
//std::string const kProjectionFilePath="/Users/bicer/Projects/tomopy/shepp-tekin.h5";
//std::string const kProjectionFilePath="/Users/bicer/Projects/data/original/13-ID/13id_x8_16s.h5";
//std::string const kProjectionFilePath="/Users/bicer/Projects/data/original/13-ID/13id_x8.h5";
std::string const kProjectionDatasetPath="/exchange/data";
std::string const kThetaFilePath="/Users/bicer/Projects/data/original/13-ID/13id1_fixed_1s.h5";
//std::string const kThetaFilePath="/Users/bicer/Projects/data/original/13-ID/13id1_fixed_16s.h5";
//std::string const kThetaFilePath="/Users/bicer/Projects/tomopy/shepp-tekin.h5";
//std::string const kThetaFilePath="/Users/bicer/Projects/data/original/13-ID/13id_x8_16s.h5";
//std::string const kThetaFilePath="/Users/bicer/Projects/data/original/13-ID/13id_x8.h5";
std::string const kThetaDatasetPath="/exchange/theta";
std::string const kReconOutputPath="./13id_i1.h5";
std::string const kReconDatasetPath="/data";

int const iteration=10;

struct {
  std::string const kProjectionFilePath="/Users/bicer/Projects/data/original/13-ID/13id1_fixed_1s.h5";
  //std::string const kProjectionFilePath="/Users/bicer/Projects/data/original/13-ID/13id1_fixed_16s.h5";
  //std::string const kProjectionFilePath="/Users/bicer/Projects/tomopy/shepp-tekin.h5";
  //std::string const kProjectionFilePath="/Users/bicer/Projects/data/original/13-ID/13id_x8_16s.h5";
  //std::string const kProjectionFilePath="/Users/bicer/Projects/data/original/13-ID/13id_x8.h5";
  std::string const kProjectionDatasetPath="/exchange/data";
  std::string const kThetaFilePath="/Users/bicer/Projects/data/original/13-ID/13id1_fixed_1s.h5";
  //std::string const kThetaFilePath="/Users/bicer/Projects/data/original/13-ID/13id1_fixed_16s.h5";
  //std::string const kThetaFilePath="/Users/bicer/Projects/tomopy/shepp-tekin.h5";
  //std::string const kThetaFilePath="/Users/bicer/Projects/data/original/13-ID/13id_x8_16s.h5";
  //std::string const kThetaFilePath="/Users/bicer/Projects/data/original/13-ID/13id_x8.h5";
  std::string const kThetaDatasetPath="/exchange/theta";
  std::string const kReconOutputPath="./13id_i1.h5";
  std::string const kReconDatasetPath="/data";

  int const iteration=2;
  int const thread_count=0;

} TraceRuntimeConfig;

int main(int argc, char **argv)
{
  /* Initiate middleware's communication layer */
  DISPCommBase<float> *comm =
        new DISPCommMPI<float>(&argc, &argv);
  std::cout << "MPI size: " << comm->size() << std::endl;
  std::cout << "MPI rank: " << comm->rank() << std::endl;

  /* Read slice data and setup job information */
  auto d_metadata = trace_io::ReadMetadata(
        kProjectionFilePath.c_str(), 
        kProjectionDatasetPath.c_str());
  int beg_index, n_blocks;
  trace_io::DistributeSlices(
      comm->rank(), comm->size(), 
      d_metadata->dims[1], beg_index, n_blocks);
  auto input_slice = 
    trace_io::ReadSlices(d_metadata, beg_index, n_blocks, 0);

  /* Read theta data */
  auto t_metadata = trace_io::ReadMetadata(
        kThetaFilePath.c_str(), 
        kThetaDatasetPath.c_str());
  auto theta = trace_io::ReadTheta(t_metadata);
  /* Convert degree values to radian */
  trace_utils::DegreeToRadian(*theta);

  /* Setup metadata data structure */
  // INFO: TraceMetadata destructor frees theta->data!
  // TraceMetadata internally creates reconstruction object
  TraceMetadata trace_metadata(
      static_cast<float *>(theta->data),  /// float const *theta,
      0,                                  /// int const proj_id,
      beg_index,                          /// int const slice_id,
      0,                                  /// int const col_id,
      input_slice->metadata->dims[0],     /// int const num_projs,
      n_blocks,                           /// int const num_slices,
      input_slice->metadata->dims[2],     /// int const num_cols,
      input_slice->metadata->dims[2],     /// int const num_grids,
      0);                                 /// float const center
  // trace_metadata.Print();

  // INFO: DataRegionBase destructor deletes input_slice.data pointer
  ADataRegion<float> *slices = 
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
  auto main_recon_space = new SIRTReconSpace(
      n_blocks, 
      2*trace_metadata.num_cols()*trace_metadata.num_cols());
  main_recon_space->Initialize(trace_metadata.num_grids());
  auto &main_recon_replica = main_recon_space->reduction_objects();
  float init_val=0.;
  main_recon_replica.ResetAllItems(init_val);

  /* Prepare processing engine and main reduction space for other threads */
  DISPEngineBase<SIRTReconSpace, float> *engine =
    new DISPEngineReduction<SIRTReconSpace, float>(
        comm,
        main_recon_space,
        0); /// # threads (0 for automatically assign the number of threads)
  /**********************/

  /**************************/
  /* Perform reconstruction */
  /* Define job size per thread request */
  int64_t req_number = trace_metadata.num_cols();

  for(int i=0; i<iteration; ++i){
    std::cout << "Iteration: " << i << std::endl;
    engine->RunParallelReduction(*slices, req_number);  /// Reconstruction
    engine->ParInPlaceLocalSynchWrapper();              /// Local combination
    
    trace_utils::UpdateRecon(trace_metadata.recon(), main_recon_replica);
    // Reset iteration
    engine->ResetReductionSpaces(init_val);
    slices->ResetMirroredRegionIter();
  }
  /**************************/

  /* Write reconstructed data to disk */
  trace_io::WriteRecon(
      trace_metadata, *d_metadata, 
      kReconOutputPath, kReconDatasetPath);

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

