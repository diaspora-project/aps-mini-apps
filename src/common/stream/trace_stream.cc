#include "trace_stream.h"

TraceStream::TraceStream(
    std::string dest_ip,
    int dest_port,
    int window_len, 
    int iteration_on_each_window,
    TraceMetadata &metadata) :
  dest_ip_ {dest_ip},
  dest_port_ {dest_port},
  window_len_ {window_len},
  iteration_on_window_ {iteration_on_each_window},
  metadata_ {metadata}
{}

DataRegionBase<float, TraceMetadata>* TraceStream::ReadSlidingOrderedSubsetting ()
{
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

DataRegionBase<float, TraceMetadata>* TraceStream::ReadOrderedSubsetting ()
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

DataRegionBase<float, TraceMetadata>* TraceStream::ReadSlidingWindow ()
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

DataRegionBase<float, TraceMetadata>* TraceStream::ReadWindow()
{
  // XXX: not ready!!!
  /// Check the current state of the execution
  if(state_ & TRACE_RUNTIME_FINALIZE) {
    /// If there is no more data in the queue finalize execution
    if(vtheta.size() == 0) return nullptr;

  } else if (state_ & TRACE_RUNTIME_RECON) {
    /// Release/free the tomo_msg if it was previously allocated
    if(&tomo_msg()!=nullptr) trace_stream::ReleaseTomoMsg(&tomo_msg());
    /// Receive new message
    tomo_msg(trace_stream::Receive());
    if(tomo_msg()==nullptr) state_ = TRACE_RUNTIME_FINALIZE;

    /// Delete projection if: 
    ///   (1) the queue is full; 
    ///   (2) all the projections are processed and window is being moved on the
    ///       remaining projections.
    if( (vtheta.size()+1 > window_len_) || // Remove projection if window is full 
        // Remove projection if only the projections in window remained
        (tomo_msg()==nullptr & vtheta.size()>0)) 
    {
      vtheta.erase(vtheta.begin());
      vproj.erase(vproj.begin(),vproj.begin()+n_rays_per_proj); 
    }

    /// Correction of the received ray-sum values
    size_t n_rays_per_proj = tomo_msg().n_sinogram*tomo_msg().n_rays_per_proj_row;
    trace_utils::RemoveNegatives(tomo_msg().data, n_rays_per_proj);
    trace_utils::RemoveAbnormals(tomo_msg().data, n_rays_per_proj);
    float theta = (tomo_msg().rotation_type & TRACE_DEGREE) ? 
      trace_utils::DegreeToRadian(tomo_msg().theta) : 
      tomo_msg().theta;   /// Theta is radian
    float *data = tomo_msg().data;

  } else { /// Unknown state
  }



  /// Check if this is the end of data acquisition
  if((tomo_msg().type & TRACE_FIN) && vtheta.size()==0) return nullptr;
  else if((tomo_msg().type & TRACE_FIN) )




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
      vtheta.push_back(theta);





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
        tomo_msg_->theta,
        tomo_msg_->projection_id,
        tomo_msg_->beg_sinogram,
        0,                          //metadata().col_id(),
        tomo_msg_->t_sinogram,            //metadata().num_total_slices(),
        window_l_en_,
        tomo_msg_->n_sinogram,            //metadata().num_slices(),
        tomo_msg_->n_rays_per_proj_row,   //metadata().num_cols(),
        tomo_msg_->n_rays_per_proj_row *  //metadata().num_grids(),
          tomo_msg_->n_rays_per_proj_row, 
        tomo_msg_->center);               //metadata().center());
    mdata->recon(metadata().recon());
  }
  curr_data_ = new DataRegionBase<float, TraceMetadata> (
      curr_projs,
      mdata->count(),
      mdata);
  
  curr_data_->ResetMirroredRegionIter();
  ++curr_iteration_;
  return curr_data_;
}
