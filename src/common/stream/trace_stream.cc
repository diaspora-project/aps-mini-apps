#include "trace_stream.h"

TraceStream::TraceStream(
    std::string dest_ip, int dest_port,
    uint32_t window_len, 
    int comm_rank, int comm_size, 
    std::string pub_info) :
  window_len_ {window_len},
  counter_ {0},
  traceMQ_ {dest_ip, dest_port, comm_rank, comm_size, pub_info}
{
  traceMQ().Initialize();
}

TraceStream::TraceStream(
    std::string dest_ip, int dest_port,
    uint32_t window_len, 
    int comm_rank, int comm_size) :
  TraceStream(dest_ip, dest_port, window_len, comm_rank, comm_size, "")
{ }


DataRegionBase<float, TraceMetadata>* TraceStream::ReadSlidingWindow(
  DataRegionBareBase<float> &recon_image, 
  int step) 
{
  // Dynamically meet sizes
  while(vtheta.size()>window_len_)
    EraseBegTraceMsg();

  // Receive new message
  std::vector<tomo_msg_t*> received_msgs; 
  for(int i=0; i<step; ++i) {
    tomo_msg_t *msg = traceMQ().ReceiveMsg();
    if(msg == nullptr) break;
    received_msgs.push_back(msg);
  }

  // TODO: After receiving message corrections might need to be applied

  /// End of the processing
  if(received_msgs.size()==0 && vtheta.size()==0){
    //std::cout << "End of the processing: " << vtheta.size() << std::endl;
    return nullptr; 
  }
  /// End of messages, but there is data to be processed in window
  else if(received_msgs.size()==0 && vtheta.size()>0){ 
    for(int i=0; i<step; ++i){  // Delete step size element
      if(vtheta.size()>0) EraseBegTraceMsg();
      else break;
    }
    //std::cout << "End of messages, but there might be data in window:" << vtheta.size() << std::endl;
    if(vtheta.size()==0) return nullptr;
  }
  /// New message(s) arrived, there is space in window
  else if(received_msgs.size()>0 && vtheta.size()<window_len_){
    //std::cout << "New message(s) arrived, there is space in window: " << window_len_ - vtheta.size() << std::endl;
    for(auto msg : received_msgs){
      tomo_msg_data_t *dmsg = traceMQ().read_data(msg);
      //traceMQ().print_data(dmsg, metadata().n_sinograms*metadata().n_rays_per_proj_row);
      AddTomoMsg(*dmsg);   
      traceMQ().free_msg(msg);
      ++counter_;
    }
    //std::cout << "After adding # items in window: " << vtheta.size() << std::endl;
  }
  /// New message arrived, there is no space in window
  else if(received_msgs.size()>0 && vtheta.size()>=window_len_){
    //std::cout << "New message arrived, there is no space in window: " << vtheta.size() << std::endl;
    for(int i=0; i<step; ++i) {
      if(vtheta.size()>0) EraseBegTraceMsg();
      else break;
    }
    for(auto msg : received_msgs){
      tomo_msg_data_t *dmsg = traceMQ().read_data(msg);
      //traceMQ().print_data(dmsg, metadata().n_sinograms*metadata().n_rays_per_proj_row);
      AddTomoMsg(*dmsg);   
      traceMQ().free_msg(msg);
      ++counter_;
    }
    //std::cout << "After remove/add, new window size: " << vtheta.size() << std::endl;
  }
  else std::cerr << "Unknown state in ReadWindow!" << std::endl;

  /// Clean-up vector
  received_msgs.clear();

  /// Generate new data and metadata
  DataRegionBase<float, TraceMetadata>* data_region = 
    SetupTraceDataRegion(recon_image);

  return data_region; 
}

void TraceStream::AddTomoMsg(tomo_msg_data_t &dmsg){
  // Convert to radian
  //dmsg.theta = dmsg.theta*3.14159265358979f/180.0;
  //std::cout << "Theta=" << dmsg.theta << std::endl;
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
    metadata().n_rays_per_proj_row, // * metadata().n_rays_per_proj_row, // metadata().num_grids(),
    vmeta.back().center);             // use the last incoming center for recon.);

  mdata->recon(recon_image);

  //mdata->Print();

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

void TraceStream::WindowLength(int wlen){
  window_len_ = wlen;
}

void TraceStream::PublishImage(DataRegionBase<float, TraceMetadata> &slice){
  auto &mdata = slice.metadata();
  auto &image = mdata.recon();
  traceMQ().PublishMsg( &image[0], 
                          {mdata.num_slices(), mdata.num_cols(), mdata.num_cols()});
}

