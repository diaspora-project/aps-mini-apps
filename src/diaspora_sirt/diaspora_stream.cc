#include <diaspora_stream.h>
#include <chrono>

int64_t DiasporaStream::ts_now() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

void DiasporaStream::recordTs(const std::string& entry) {
    m_ts_entries.push_back(fmt::format("{} {}", ts_now(), entry));
}

void DiasporaStream::writeTs(int rank) {
    std::ofstream f("sirt." + std::to_string(rank) + ".ts.txt");
    for(auto& e : m_ts_entries) f << e << "\n";
}

void DiasporaStream::addTomoMsg(diaspora::Event event){
  diaspora::Metadata metadata = event.metadata();
  diaspora::DataView data = event.data();
  // event.acknowledge(); // acknowledge event
  vmeta.push_back(metadata.json()); /// Setup metadata
  vtheta.push_back(metadata.json()["theta"].get<float_t>());
  spdlog::info("Received data {}", metadata.json().dump());

  size_t n_rays_per_proj =
  getInfo()["n_sinograms"].get<int64_t>() *
  getInfo()["n_rays_per_proj_row"].get<int64_t>();
  size_t ptr_size = data.segments()[0].size / sizeof(float);
  assert(n_rays_per_proj == ptr_size && "Pointer size does not match n_rays_per_projection");

  float* start = static_cast<float*>(data.segments()[0].ptr);
  float* end = static_cast<float*>(data.segments()[0].ptr)+ n_rays_per_proj;
  if (start == nullptr || end == nullptr) {
    throw std::runtime_error("Invalid pointer arithmetic in insertion");
  }
  vproj.insert(vproj.end(), start, end);
}

/* Erase streaming message to buffers
*/
void DiasporaStream::eraseBegTraceMsg(){
  vtheta.erase(vtheta.begin());
  size_t n_rays_per_proj =
  getInfo()["n_sinograms"].get<int64_t>() *
  getInfo()["n_rays_per_proj_row"].get<int64_t>();
  vproj.erase(vproj.begin(),vproj.begin()+n_rays_per_proj);
  vmeta.erase(vmeta.begin());
}


/* Generates a data region that can be processed by Trace
* @param recon_image: reconstruction image

  return: DataRegionBase
*/
DataRegionBase<float, TraceMetadata>* DiasporaStream::setupTraceDataRegion(
  DataRegionBareBase<float> &recon_image){
    TraceMetadata *mdata = new TraceMetadata(
    vtheta.data(),
    0,                                                  // metadata().proj_id(),
    getInfo()["beg_sinogram"].get<int64_t>(),           // metadata().slice_id(),
    0,                                                  // metadata().col_id(),
    getInfo()["tn_sinograms"].get<int64_t>(),           // metadata().num_total_slices(),
    vtheta.size(),                                      // int const num_projs,
    getInfo()["n_sinograms"].get<int64_t>(),            // metadata().num_slices(),
    getInfo()["n_rays_per_proj_row"].get<int64_t>(),    // metadata().num_cols(),
    getInfo()["n_rays_per_proj_row"].get<int64_t>(),    // * metadata().n_rays_per_proj_row, // metadata().num_grids(),
    vmeta.back()["center"].get<float>());               // use the last incoming center for recon.);

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

DiasporaStream::DiasporaStream(
            std::string driver_type,
            std::string driver_config_file,
            size_t batchsize,
            uint32_t window_len,
            int rank,
            int size):
  batchsize {batchsize},
  window_len {window_len},
  counter {0},
  comm_rank {rank},
  comm_size {size}
  {
      diaspora::Metadata driver_options;
      if(!driver_config_file.empty()) {
        auto ifs = std::ifstream(driver_config_file);
        if(!ifs.good()) {
            throw diaspora::Exception{std::string{"Cannot open driver config file "} + driver_config_file};
        }
        driver_options = json::parse(ifs);
      } else if (driver_type == "files") {
        driver_options = json::parse("{\"root_path\":\"./diaspora-data\"}");
      }
      driver = diaspora::Driver::New(driver_type.c_str(), driver_options);
  }


/* Handshake with Dist component
* @param rank: MPI rank
* @param size: MPI size
*/
void DiasporaStream::handshake(int rank, int size){
  std::string topic_name = "handshake_s_d";
  // Send comm size to dist_streamer
  diaspora::Producer hs_producer = getProducer(topic_name, "hs_p");

  json md = {{"comm_size", size}};
  diaspora::Metadata metadata{md};
  hs_producer.push(metadata);
  hs_producer.flush().wait(-1);  // ensure comm_size is on disk before waiting for DIST's reply

  // Receive metadata info
  topic_name = "handshake_d_s";
  std::vector<size_t> targets = {static_cast<size_t>(rank)};
  diaspora::TopicHandle topic = driver.openTopic(topic_name);
  diaspora::Consumer hs_consumer = topic.consumer( "hs_c",
                                                batchSize,
                                                threadCount,
                                                targets);
  std::optional<diaspora::Event> event;
  while(!event) {
    event = hs_consumer.pull().wait(-1);
  }
  diaspora::Metadata m = event->metadata();
  json mdata = m.json();
  setInfo(mdata);
}

/* Publish reconstructed image
* @param metadata: metadata in json format
* @param data:     pointer to the reconstructed image
* @param producer: diaspora producer
*/
void DiasporaStream::publishImage(
  json &meta,
  float *data,
  size_t size,
  diaspora::Producer producer,
  std::optional<size_t> partition){

  diaspora::Metadata metadata{meta};
  float* copy = new float[size];
  std::memcpy(copy, data, size * sizeof(float));
  auto free_cb = [](diaspora::DataView::UserContext ctx) {
      delete[] static_cast<float*>(ctx);
  };
  auto data_m = diaspora::DataView(static_cast<void*>(copy), size * sizeof(float), copy, free_cb);
  recordTs(fmt::format("PUSH_START topic=sirt_den,data_size={}", size * sizeof(float)));
  producer.push(metadata, data_m, partition);
  recordTs("PUSH_END topic=sirt_den");
}


/* Create and return a diaspora producer
* @param topic_name:    diaspora topic
* @param producer_name: producer name

  return: diaspora producer
*/
diaspora::Producer DiasporaStream::getProducer(std::string topic_name,
                                         std::string producer_name="streamer_sirt"){
  auto topic = driver.openTopic(topic_name);
  diaspora::Producer producer = topic.producer(producer_name,
                                            batchSize,
                                            threadCount,
                                            ordering);
  return producer;
}

/* Create and return a diaspora consumer
* @param topic_name:    diaspora topic
* @param consumer_name: consumer name
* @param targets:       list of diaspora partitions to consume from

  return: diaspora consumer
*/
diaspora::Consumer DiasporaStream::getConsumer(std::string topic_name,
                                         std::string consumer_name="dist_sirt",
                                         std::vector<size_t> targets={0}){
  diaspora::TopicHandle topic = driver.openTopic(topic_name);
  diaspora::Consumer consumer = topic.consumer(consumer_name,
                                            threadCount,
                                            batchSize,
                                            data_selector,
                                            data_allocator,
                                            targets);
  return consumer;
}

/* Create a data region from sliding window
  * @param recon_image Initial values of reconstructed image
  * @param step        Sliding step. Waits at least step projection
  *                    before returning window back to the reconstruction
  *                    engine
  *
  * Return:  nullptr if there is no message and sliding window is empty
  *          DataRegionBase if there is data in sliding window
  */

DataRegionBase<float, TraceMetadata>* DiasporaStream::readSlidingWindow(
  DataRegionBareBase<float> &recon_image,
  int step,
  diaspora::Consumer consumer){
  // Dynamically meet sizes
  while(vtheta.size()> window_len)
    eraseBegTraceMsg();

  // Receive new message
  std::vector<diaspora::Event> diaspora_events;

  for(int i=0; i<step; ++i) {
    // diaspora messages
    recordTs("PULL_WAIT_START topic=dist_sirt");
    std::optional<diaspora::Event> event;
    while(!event) {
        event = consumer.pull().wait(-1);
    }
    size_t ev_data_size = 0;
    for(auto& seg : event->data().segments()) ev_data_size += seg.size;
    recordTs(fmt::format("PULL_WAIT_END topic=dist_sirt,event_id={},data_size={}",
        event->id(), ev_data_size));
    diaspora_events.push_back(event.value());
    //if endMsg break
    if (event->metadata().json()["Type"].get<std::string>() == "FIN") return nullptr;
  }
  // TODO: After receiving message corrections might need to be applied

  /// End of the processing
  if(diaspora_events.size()==0 && vtheta.size()==0){
    //std::cout << "End of the processing: " << vtheta.size() << std::endl;
    return nullptr;
  }
  /// End of messages, but there is data to be processed in window
  else if(diaspora_events.size()==0 && vtheta.size()>0){
    for(int i=0; i<step; ++i){  // Delete step size element
      if(vtheta.size()>0) eraseBegTraceMsg();
      else break;
    }
    //std::cout << "End of messages, but there might be data in window:" << vtheta.size() << std::endl;
    if(vtheta.size()==0) return nullptr;
  }
  /// New message(s) arrived, there is space in window
  else if(diaspora_events.size()>0 && vtheta.size()<window_len){
    //std::cout << "New message(s) arrived, there is space in window: " << window_len_ - vtheta.size() << std::endl;
    for(auto msg : diaspora_events){
      addTomoMsg(msg);
      ++counter;
    }
  std::cout << "After adding # items in window: " << vtheta.size() << std::endl;
  }
  /// New message arrived, there is no space in window
  else if(diaspora_events.size()>0 && vtheta.size()>=window_len){
    //std::cout << "New message arrived, there is no space in window: " << vtheta.size() << std::endl;
    for(int i=0; i<step; ++i) {
      if(vtheta.size()>0) eraseBegTraceMsg();
      else break;
    }
    for(auto msg : diaspora_events){
      addTomoMsg(msg);
      ++counter;
    }
  }
  else std::cerr << "Unknown state in ReadWindow!" << std::endl;

  /// Clean-up vector
  diaspora_events.clear();

  /// Generate new data and metadata
  DataRegionBase<float, TraceMetadata>* data_region =
    setupTraceDataRegion(recon_image);

  return data_region;
  }

json DiasporaStream::getInfo(){ return info;}

int DiasporaStream::getRank() {return comm_rank;}

uint32_t DiasporaStream::getCounter(){ return counter;}

void DiasporaStream::setInfo(json &j) {info = j;}

void DiasporaStream::windowLength(uint32_t wlen){ window_len = wlen;}

