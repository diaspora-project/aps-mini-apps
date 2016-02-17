#include <chrono>
#include "mpi.h"
#include "trace_h5io.h"
//#include "trace_utils.h"
#include "disp_comm_mpi.h"
#include "data_region_base.h"
#include "disp_engine_reduction.h"
#include "sirt.h"


class TDataMock
{
  private:
    const float kPI = 3.14159265358979f;
    float *data_ = nullptr;
    float *theta_ = nullptr;
    int beg_index_;
    int num_projs_; /// in local memory
    int num_slices_;
    int num_cols_;
    int num_iter_;
    int num_threads_;

    void ParseArgs(int argc, char **argv)
    {
      int c;
      extern char *optarg;
      extern int optind;

      if(argc!=11){
        std::cout << "Usage: " << std::endl
          << argv[0] 
          << " -p <number of projections> -s <number of slices>"
          " -c <number of columns> -i <number of iterations>"
          " -t <number of threads>" << std::endl;
         exit(0);
      }

      while ((c = getopt(argc, argv, "p:s:c:i:t:")) != EOF) {
        switch (c) {
          case 't':
            num_threads_ = atoi(optarg);
            break;
          case 'c':
            num_cols_ = atoi(optarg);
            break;
          case 's':
            num_slices_= atoi(optarg);
            break;
          case 'i':
            num_iter_= atoi(optarg);
            break;
          case 'p':
            num_projs_= atoi(optarg);
            break;
          case '?':
            std::cout << "Usage: " << std::endl
              << "./" << argv[0] 
              << " -p <number of projections> -s <number of slices>"
              " -c <number of columns> -i <number of iterations>"
              " -t <number of threads>" << std::endl;
             exit(0);
        }
      }
      std::cout << "# Projections=" << num_projs_ <<
        "; # Slices=" << num_slices_ <<
        "; # Columns=" << num_cols_ <<
        "; # Iterations=" << num_iter_ <<
        "; # Threads=" << num_threads_ << std:: endl;
    }

  public:
    TDataMock(
        int beg_index, 
        int num_projs, 
        int num_slices, 
        int num_cols,
        int num_iter,
        int num_threads):
      beg_index_{beg_index},
      num_projs_{num_projs}, 
      num_slices_{num_slices},
      num_cols_{num_cols},
      num_iter_{num_iter},
      num_threads_{num_threads}
    {
      data_ = new float[num_projs_*num_slices_*num_cols_];
      theta_ = new float[num_cols_];
    }

    TDataMock(int argc, char **argv)
    {
      ParseArgs(argc,argv);
      data_ = new float[num_projs_*num_slices_*num_cols_];
      theta_ = new float[num_cols_];
    }

    ~TDataMock()
    {
      //delete [] data_;
      //delete [] theta_;
    }


    void GenParData(float val)
    {
      for(int i=0; i<num_projs_; ++i)
        for(int j=0; j<num_slices_; ++j)
          for(int k=0; k<num_cols_; ++k)
            data_[i*num_slices_*num_cols_+j*num_cols_+k] = val;
    }
    void GenProjTheta(float beg, float end)
    {
      float rate = (end-beg)/num_cols_;
      for(int i=0; i<num_cols_; ++i)
        theta_[i] = (beg+i*rate)*kPI/180.;
    }

    int beg_index() const { return beg_index_; }
    int num_projs() const { return num_projs_; }
    int num_slices() const { return num_slices_; }
    int num_cols() const { return num_cols_; }
    float* data() const { return data_; }
    float* theta() const { return theta_; }
    int num_iter() const { return num_iter_; }
    int num_threads() const { return num_threads_; };
};



int main(int argc, char **argv)
{
  auto tot_beg_time = std::chrono::high_resolution_clock::now();
  
  /* Initiate middleware's communication layer */
  DISPCommBase<float> *comm =
        new DISPCommMPI<float>(&argc, &argv);
  std::cout << "MPI rank: "<< comm->rank() << "; MPI size: " << comm->size() << std::endl;

  auto dg_beg_time = std::chrono::high_resolution_clock::now();
  TDataMock mock_data(argc, argv);
  mock_data.GenParData(0.);
  mock_data.GenProjTheta(0., 180.);
  std::chrono::duration<double> dg_time = 
    std::chrono::high_resolution_clock::now()-dg_beg_time; 


  /* Setup metadata data structure */
  // INFO: TraceMetadata destructor frees theta->data!
  // TraceMetadata internally creates reconstruction object
  TraceMetadata trace_metadata(
      static_cast<float *>(mock_data.theta()),  /// float const *theta,
      0,                                  /// int const proj_id,
      mock_data.beg_index(),                          /// int const slice_id,
      0,                                  /// int const col_id,
      mock_data.num_slices(),     /// int const num_tot_slices,
      mock_data.num_projs(),     /// int const num_projs,
      mock_data.num_slices(),                           /// int const num_slices,
      mock_data.num_cols(),     /// int const num_cols,
      mock_data.num_cols(),     /// int const num_grids,
      0.);         /// float const center

  // INFO: DataRegionBase destructor deletes input_slice.data pointer
  ADataRegion<float> *slices = 
    new DataRegionBase<float, TraceMetadata>(
        static_cast<float *>(mock_data.data()),
        trace_metadata.count(),
        &trace_metadata);

  /***********************/
  /* Initiate middleware */
  /* Prepare main reduction space and its objects */
  /* The size of the reconstruction object (in reconstruction space) is
   * twice the reconstruction object size, because of the length storage
   */
  auto main_recon_space = new SIRTReconSpace(
      mock_data.num_slices(), 
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
        mock_data.num_threads());
        /// # threads (0 for auto assign the number of threads)

  /**********************/

  /**************************/
  /* Perform reconstruction */
  /* Define job size per thread request */
  int64_t req_number = trace_metadata.num_cols();

  auto rec_beg_time = std::chrono::high_resolution_clock::now();
  for(int i=0; i<mock_data.num_iter(); ++i){
    //std::cout << "Iteration: " << i << std::endl;
    engine->RunParallelReduction(*slices, req_number);  /// Reconstruction
    engine->ParInPlaceLocalSynchWrapper();              /// Local combination

    /// Update reconstruction object
    main_recon_space->UpdateRecon(trace_metadata.recon(), main_recon_replica);
    
    /// Reset iteration
    engine->ResetReductionSpaces(init_val);
    slices->ResetMirroredRegionIter();
  }
  std::chrono::duration<double> rec_time = 
    std::chrono::high_resolution_clock::now()-rec_beg_time; 

  /**************************/

  /* Write reconstructed data to disk */
  /*
  trace_io::WriteRecon(
      trace_metadata, *d_metadata, 
      TraceRuntimeConfig.kReconOutputPath, 
      TraceRuntimeConfig.kReconDatasetPath);
  */
  std::chrono::duration<double> tot_time = 
    std::chrono::high_resolution_clock::now()-tot_beg_time; 

  std::cout << "Total time=" << tot_time.count() <<
    "; Reconstruction time=" << rec_time.count() <<
    "; Data Generation time=" << dg_time.count() << std::endl;

  /* Clean-up the resources */
  delete slices;
  delete engine;
}

