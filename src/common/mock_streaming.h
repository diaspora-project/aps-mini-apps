#include "trace_data.h"
#include "data_region_base.h"
#include "disp_engine_reduction.h"
#include <vector>

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

    std::vector<float> vproj;
    std::vector<float> vtheta;

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

    DataRegionBase<float, TraceMetadata>* ReadSlidingOrderedSubsetting(){
      int dist= std::floor(metadata().num_projs()/window_len_);
      if(!(curr_proj_index_ < metadata().num_projs()) && !(vtheta.size()>0)) return nullptr;
      // if(curr_proj_index_ > metadata().num_projs()) return nullptr;

      float *orig_data = data();
      const float *theta_orig = metadata().theta();

      int beg_index = curr_proj_index_%window_len_ * dist+std::floor(curr_proj_index_/window_len_);
      int copy_size = 1; /// Only one projection at a time

      if( (vtheta.size()+1 > window_len_) ||            // Remove projection if window is full 
          !(curr_proj_index_ < metadata().num_projs())) // Remove projection if only the projections in window remained
      { // Erase the element at the beginning of the vector
        std::cout << "before vtheta.begin()=" << *vtheta.begin() << std::endl;
        std::cout << "before vproj.begin()=" << *vproj.begin() << std::endl;
        vtheta.erase(vtheta.begin());  // Delete theta
        vproj.erase(vproj.begin(),vproj.begin()+ray_per_proj); // Delete one projection data
        std::cout << "after vtheta.begin()=" << &(*vtheta.begin()) << std::endl;
        std::cout << "after vproj.begin()=" << &(*vproj.begin()) << std::endl;
        std::cout << "Remove projection; curr_proj_index=" << curr_proj_index_ << std::endl;
      }

      std::cout << "metadata().num_projs()=" << metadata().num_projs() << std::endl;
      std::cout << "beg_index=" << beg_index << std::endl;
      if(curr_proj_index_ < metadata().num_projs()){
        for(int i=0; i<copy_size; ++i){
          //curr_theta[i] = theta_orig[beg_index+i]; 
          vtheta.push_back(theta_orig[beg_index+i]);
          for(int j=0; j<ray_per_proj; ++j){
            //curr_projs[(i*ray_per_proj)+j] = 
            //  orig_data[(beg_index+i)*ray_per_proj + j];
            vproj.push_back(orig_data[(beg_index+i)*ray_per_proj + j]);
          }
        }
      }

      float ncenter = metadata().center(); //0.;
      //if(curr_proj_index_<metadata().num_projs()/5){
      //  ncenter = 1000.;
      //} else ncenter = metadata().center();
      TraceMetadata *mdata = new TraceMetadata(
        vtheta.data(),
        metadata().proj_id(),
        metadata().slice_id(),
        metadata().col_id(),
        metadata().num_total_slices(),
        vtheta.size(),
        metadata().num_slices(),
        metadata().num_cols(),
        metadata().num_grids(),
        ncenter);
      mdata->recon(metadata().recon());

      curr_data_ = new DataRegionBase<float, TraceMetadata> (
       vproj.data(),
       mdata->count(),
       mdata);

      curr_proj_index_++;
      curr_data_->ResetMirroredRegionIter();
      std::cout << "Added projections index=" << beg_index << 
        "; copy_size=" << copy_size << 
        "; size of projection vector=" << vproj.size()<<
        "; size of theta vector=" << vtheta.size()<<
        "; mdata->count()=" << mdata->count() << std::endl;
        
      return curr_data_;
    }

    DataRegionBase<float, TraceMetadata>* ReadOrderedSubsetting()
    {
      int dist= std::floor(metadata().num_projs()/window_len_);
      if(curr_proj_index_ > dist) return nullptr;

      if(!(curr_iteration_ % iteration_on_window_)){
        float *orig_data = data();
        const float *theta_orig = metadata().theta();
        /// Assumption window len evenly divides # projs.
        std::cout << "Window content:";
        for(int i=0; i<window_len_; ++i){
          std::cout << (curr_proj_index_+(i*dist)) << " ";
          curr_theta[i] = theta_orig[curr_proj_index_+(i*dist)]; 
          for(int j=0; j<ray_per_proj; ++j){
            curr_projs[(i*ray_per_proj)+j] = 
              orig_data[(curr_proj_index_+(i*dist))*ray_per_proj + j];
            //std::cout << "theta=" << curr_theta[i] << "; ray=" << 
            //  curr_projs[(i*ray_per_proj)+j] << std::endl;
          }
        }
        std::cout << std::endl;
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

        curr_proj_index_ += 1;
      } 
      //for(int i=1024*350; i<1024*350+5; ++i)
      //  std::cout << "i=" << metadata().recon()[i] << std::endl;
      ++curr_iteration_;
      curr_data_->ResetMirroredRegionIter();
      return curr_data_;
    }

    DataRegionBase<float, TraceMetadata>* ReadSlidingWindow()
    {
      if(curr_proj_index_ > metadata().num_projs()) return nullptr;

      float *orig_data = data();
      const float *theta_orig = metadata().theta();


      /// Assumption window len evenly divides # projs.
      int beg_index = (curr_proj_index_<window_len_) ? 0 : (curr_proj_index_-window_len_);
      int end_index = curr_proj_index_;

      int copy_size = end_index-beg_index;

      for(int i=0; i<copy_size; ++i){
        curr_theta[i] = theta_orig[beg_index+i]; 
        for(int j=0; j<ray_per_proj; ++j){
          curr_projs[(i*ray_per_proj)+j] = 
            orig_data[(beg_index+i)*ray_per_proj + j];
        }
      }

      TraceMetadata *mdata = new TraceMetadata(
        curr_theta,
        metadata().proj_id(),
        metadata().slice_id(),
        metadata().col_id(),
        metadata().num_total_slices(),
        copy_size,
        metadata().num_slices(),
        metadata().num_cols(),
        metadata().num_grids(),
        metadata().center());
      mdata->recon(metadata().recon());

      curr_data_ = new DataRegionBase<float, TraceMetadata> (
       curr_projs,
       mdata->count(),
       mdata);

      curr_proj_index_++;
      curr_data_->ResetMirroredRegionIter();
      std::cout << "beg_index=" << beg_index << 
        "; end_index=" << end_index  <<
        "; copy_size=" << copy_size << 
        "; mdata->count()=" << mdata->count() << std::endl;
        
      return curr_data_;
    }

    DataRegionBase<float, TraceMetadata>* ReadWindow()
    {
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
    void Reset() {curr_proj_index_=0; curr_iteration_=0; }
};
