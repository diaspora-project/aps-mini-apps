#include "trace_stream.h"

TraceStream::TraceStream(
    std::string dest_ip,
    int dest_port,
    int window_len, 
    TraceMetadata &metadata,
    int comm_rank,
    int comm_size) :
  window_len_ {window_len},
  metadata_ {metadata},
  traceMQ_ {dest_ip, dest_port, comm_rank, comm_size}
{}

DataRegionBase<float, TraceMetadata>* TraceStream::ReadSlidingWindow() {
  /// Receive new message
  tomo_msg_t *msg = traceMQ().ReceiveMsg();

  /// End of the processing
  if(msg==nullptr && vtheta.size()<=1) return nullptr; 
  /// End of messages, but there is data to be processed in window
  else if(msg==nullptr && vtheta.size()>1) EraseBegTraceMsg();   
  /// New message arrived, there is space in window
  else if(msg!=nullptr && vtheta.size()<window_len_-1) AddTomoMsg(*msg);   
  /// New message arrived, there is no space in window
  else if(msg!=nullptr && vtheta.size()==window_len_-1){
    EraseBegTraceMsg();  /// Remove first projection
    AddTomoMsg(*msg);    /// Add projection to the end
  }
  else std::err("Unknown state in ReadWindow!");

  /// Clean-up received message
  traceMQ().DeleteMsg(msg);

  return SetupTraceMsg(); /// Generate new data and metadata, return it
}

void TraceStream::AddTomoMsg(tomo_msg_t &msg){
    vmeta.push_back(msg); /// Setup metadata
    vtheta.push_back(vmeta.back().theta);
    vproj.insert(vproj.end(), msg.data, msg.data+n_rays_per_proj);
}

void TraceStream::EraseBegTraceMsg(){
    vtheta.erase(vtheta.begin());
    size_t n_rays_per_proj = vmeta[0].n_sinogram * vmeta[0].n_rays_per_proj_row;
    vproj.erase(vproj.begin(),vproj.begin()+n_rays_per_proj); 
    vmeta.erase(vmeta.begin());
}

DataRegionBase<float, TraceMetadata>* SetupTraceDataRegion(){
  TraceMetadata *mdata = new TraceMetadata(
    vtheta.data(),
    0,                                  // metadata().proj_id(),
    vmeta.back().beg_sinogram,          // metadata().slice_id(),
    0,                                  // metadata().col_id(),
    vmeta.back().tn_sinogram            // metadata().num_total_slices(),
    vtheta.size(),
    vmeta.back().n_sinogram,            // metadata().num_slices(),
    vmeta.back().n_rays_per_proj_row    // metadata().num_cols(),
    vmeta.back().n_rays_per_proj_row * 
      vmeta.back().n_rays_per_proj_row, // metadata().num_grids(),
    vmeta.back().center);               // ncenter);

  mdata->recon(metadata().recon());

  auto curr_data = new DataRegionBase<float, TraceMetadata> (
      curr_projs,
      mdata->count(),
      mdata);
  
  curr_data->ResetMirroredRegionIter();

  return curr_data;
}

