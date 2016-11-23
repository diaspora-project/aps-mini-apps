#include "trace_data.h"
#include "data_region_base.h"
#include "disp_engine_reduction.h"

class MockStreamingData
{
  private:
    DataRegionBase<float, TraceMetadata> &orig_data_;
    DataRegionBase<float, TraceMetadata> *curr_data_;
    int window_len_;
    int iteration_on_window_; 

    int curr_proj_index_ = 0;
    int curr_iteration_ = 0;

    size_t ray_per_proj;
    float *curr_projs = nullptr;
    float *curr_theta = nullptr;

  public:
    MockStreamingData(
      DataRegionBase<float, TraceMetadata> &orig_data, 
      int window_len, 
      int iteration_on_each_window) :
      orig_data_ {orig_data},
      window_len_ {window_len},
      iteration_on_window_ {iteration_on_each_window},
      ray_per_proj {orig_data.metadata().num_slices()*orig_data.metadata().num_cols()},
      curr_projs {new float[window_len_ * ray_per_proj]},
      curr_theta {new float[window_len_]}
    {}

    DataRegionBase<float, TraceMetadata>* ReadWindow()
    {
      TraceMetadata &meta = metadata(); /// metadata includes theta values

      if(curr_proj_index_ > metadata().num_projs()) return nullptr;

      if(!(curr_iteration_ % iteration_on_window_)){
        float *orig_data = data();
        const float *theta_orig = metadata().theta();
        /// Assumption window len evenly divides # projs.
        for(int i=0; i<window_len_; ++i){
          curr_theta[i] = theta_orig[curr_proj_index_+i]; 
          for(int j=0; j<ray_per_proj; ++j){
            curr_projs[(i*ray_per_proj)+j] = 
              orig_data[(curr_proj_index_+i)*ray_per_proj + j];
            //std::cout << "theta=" << curr_theta[i] << "; ray=" << 
            //  curr_projs[(i*ray_per_proj)+j] << std::endl;
          }
        }
        TraceMetadata *mdata = new TraceMetadata(
          curr_theta,
          metadata().proj_id(),
          metadata().slice_id(),
          metadata().col_id(),
          metadata().num_total_slices(),
          window_len_,
          metadata().num_slices(),
          metadata().num_cols(),
          metadata().num_grids(),
          metadata().center());
        mdata->recon(metadata().recon());
        //std::cout << "recon=" << &metadata().recon()[0] << std::endl;
        curr_data_ = new DataRegionBase<float, TraceMetadata> (
         curr_projs,
         mdata->count(),
         mdata);

        curr_proj_index_ += window_len_;
      } 
      //for(int i=1024*350; i<1024*350+5; ++i)
      //  std::cout << "i=" << metadata().recon()[i] << std::endl;
      ++curr_iteration_;
      curr_data_->ResetMirroredRegionIter();
      return curr_data_;
    }

    float* data() const { return &orig_data_[0]; }
    TraceMetadata& metadata() const { return orig_data_.metadata(); }

    int curr_proj_index() const { return curr_proj_index_; }
    int curr_iteration() const { return curr_iteration_; }
};
