#include "trace_stream.h"

TraceStream::TraceStream(
    std::string dest_ip,
    int dest_port,
    uint32_t window_len, 
    int comm_rank,
    int comm_size) :
  window_len_ {window_len},
  traceMQ_ {dest_ip, dest_port, comm_rank, comm_size}
{
  traceMQ().Initialize();
}

DataRegionBase<float, TraceMetadata>* TraceStream::ReadSlidingWindow(
  DataRegionBareBase<float> &recon_image) 
{
  /// Receive new message
  tomo_msg_t *msg = traceMQ().ReceiveMsg();

  // TODO: After receiving message corrections might need to be applied

  /// End of the processing
  if(msg==nullptr && vtheta.size()<=1) 
    return nullptr; 
  /// End of messages, but there is data to be processed in window
  else if(msg==nullptr && vtheta.size()>1) 
    EraseBegTraceMsg();
  /// New message arrived, there is space in window
  else if(msg!=nullptr && vtheta.size()<window_len_){
    AddTomoMsg(*(traceMQ().read_data(msg)));   
    traceMQ().free_msg(msg);
  }
  /// New message arrived, there is no space in window
  else if(msg!=nullptr && vtheta.size()==window_len_){
    EraseBegTraceMsg();  /// Remove first projection
    AddTomoMsg(*(traceMQ().read_data(msg)));    /// Add projection to the end
    traceMQ().free_msg(msg);
  }
  else std::cerr << "Unknown state in ReadWindow!" << std::endl;

  /// Clean-up received message

  /// Generate new data and metadata
  DataRegionBase<float, TraceMetadata>* data_region = 
    SetupTraceDataRegion(recon_image);

  return data_region; 
}

void TraceStream::AddTomoMsg(tomo_msg_data_t &dmsg){
  // Convert to radian
  dmsg.theta = dmsg.theta*3.14159265358979f/180.0;
  std::cout << "Theta=" << dmsg.theta << std::endl;
  tomo_msg_data_t rdmsg = {
    .projection_id=dmsg.projection_id,
    .theta=dmsg.theta,
    .center=dmsg.center};
  vmeta.push_back(rdmsg); /// Setup metadata
  vtheta.push_back(rdmsg.theta);
  vproj.insert(vproj.end(), 
      dmsg.data,
      dmsg.data + metadata().n_sinograms*metadata().n_rays_per_proj_row);
}

void TraceStream::EraseBegTraceMsg(){
    vtheta.erase(vtheta.begin());
    size_t n_rays_per_proj = metadata().n_sinograms * metadata().n_rays_per_proj_row;
    vproj.erase(vproj.begin(),vproj.begin()+n_rays_per_proj); 
    vmeta.erase(vmeta.begin());
}

DataRegionBase<float, TraceMetadata>* TraceStream::SetupTraceDataRegion(
  DataRegionBareBase<float> &recon_image)
{
  TraceMetadata *mdata = new TraceMetadata(
    vtheta.data(),
    0,                                // metadata().proj_id(),
    metadata().beg_sinogram,          // metadata().slice_id(),
    0,                                // metadata().col_id(),
    metadata().tn_sinograms,            // metadata().num_total_slices(),
    vtheta.size(),                    // int const num_projs,
    metadata().n_sinograms,            // metadata().num_slices(),
    metadata().n_rays_per_proj_row,    // metadata().num_cols(),
    metadata().n_rays_per_proj_row * 
      metadata().n_rays_per_proj_row, // metadata().num_grids(),
    vmeta.back().center);             // use the last incoming center for recon.);

  mdata->recon(recon_image);

  // Will be deleted at the end of main loop
  float *data=new float[mdata->count()];
  for(size_t i=0; i<mdata->count(); ++i) data[i]=vproj[i];
  auto curr_data = new DataRegionBase<float, TraceMetadata> (
      data,
      mdata->count(),
      mdata);
  
  curr_data->ResetMirroredRegionIter();

  return curr_data;
}

