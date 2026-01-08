#include <diaspora_stream.h>

void DiasporaStream::addTomoMsg(diaspora::Event event){
  auto start_t = std::chrono::high_resolution_clock::now();
  diaspora::Metadata metadata = event.metadata();
  auto end_t = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = end_t - start_t;
  setConsumerTimes("mata_t", metadata.string().size(), elapsed.count());
  start_t = std::chrono::high_resolution_clock::now();
  diaspora::DataView data = event.data();
  end_t = std::chrono::high_resolution_clock::now();
  elapsed = end_t - start_t;
  setConsumerTimes("data_t", data.segments()[0].size, elapsed.count());
  // event.acknowledge(); // acknowledge event
  vmeta.push_back(metadata.json()); /// Setup metadata
  vtheta.push_back(metadata.json()["theta"].get<float_t>());
  spdlog::info("Received data {}", metadata.string());

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
  auto future = hs_producer.push(metadata);
  future.wait(-1);

  // Receive metadata info
  topic_name = "handshake_d_s";
  std::vector<size_t> targets = {static_cast<size_t>(rank)};
  diaspora::TopicHandle topic = driver.openTopic(topic_name);
  diaspora::Consumer hs_consumer = topic.consumer( "hs_c",
                                                batchSize,
                                                threadCount,
                                                targets);
  auto event = hs_consumer.pull().wait(-1).value();
  diaspora::Metadata m = event.metadata();
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
  diaspora::Producer producer){

  diaspora::Metadata metadata{meta};
  float* copy = new float[size];
  std::memcpy(copy, data, size * sizeof(float));
  buffer.push_back(copy);
  auto data_m = diaspora::DataView(buffer[buffer.size()-1], size*sizeof(float));
  auto start = std::chrono::high_resolution_clock::now();
  auto f  = producer.push(metadata, data_m);
  batch++;
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed_push = end - start;
  std::chrono::duration<double> elapsed_flush = end - end;
  // std::cout << "Push " << elapsed.count() << " sec" << std::endl;
  producer_times.emplace_back("Push", size*sizeof(float), elapsed_push.count());
  if (batch % batchsize == 0) futures.push(std::move(f));

  if (futures.size() == 3){
    // start = std::chrono::high_resolution_clock::now();
    // producer.flush();
    // futures.front().wait();
    // futures.pop();
    // end = std::chrono::high_resolution_clock::now();
    // elapsed_flush = end - start;
    // // std::cout << "Flush " << batch << " Time: " << elapsed_flush.count() << " sec" << std::endl;
    // producer_times.emplace_back("Wait", buffer.size()*size*sizeof(float), elapsed_flush.count());

    // try {
    //   for(auto& f : futures){
    //     start = std::chrono::high_resolution_clock::now();
    //     f.wait();
    //     end = std::chrono::high_resolution_clock::now();
    //     elapsed_flush = end - start;
    //     producer_times.emplace_back("Wait", buffer.size()*size*sizeof(float), elapsed_flush.count());

    //   }
    // } catch(const diaspora::Exception& ex) {
    //     std::cerr << "MOFKA EXCEPTION: " << ex.what() << std::endl;
    // }
    size_t c=0;
    for (float* ptr : buffer) {
        if (ptr==nullptr) continue;
        else delete[] ptr;
        c++;
        if (c==batchsize) break;

    }
    buffer.clear();
    batch=0;
  }
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
    auto start = std::chrono::high_resolution_clock::now();
    auto event = consumer.pull().wait(-1).value();
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    setConsumerTimes("wait_t", 1, elapsed.count());
    diaspora_events.push_back(event);
    //if endMsg break
    if (event.metadata().json()["Type"].get<std::string>() == "FIN") return nullptr;
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

int DiasporaStream::getBufferSize() {return buffer.size();}

uint32_t DiasporaStream::getBatch() {return batch;}

uint32_t DiasporaStream::getCounter(){ return counter;}

std::queue<diaspora::Future<std::optional<diaspora::EventID>>>& DiasporaStream::getFutures(){ return futures;}

void DiasporaStream::setInfo(json &j) {info = j;}

void DiasporaStream::windowLength(uint32_t wlen){ window_len = wlen;}

const std::vector<std::tuple<std::string, uint64_t, float>>& DiasporaStream::getConsumerTimes(){return consumer_times;}

void DiasporaStream::setConsumerTimes(std::string op, uint64_t size, float time){
  consumer_times.emplace_back(op, size, time);
}

std::vector<std::tuple<std::string, uint64_t, float>> DiasporaStream::getProducerTimes(){return producer_times;}

void DiasporaStream::setProducerTimes(std::string op, uint64_t size, float time){
  producer_times.emplace_back(op, size, time);
}

int DiasporaStream::writeTimes(std::string type){
  std::string filename = "Sirt_"+ type + "_rank_" + std::to_string(getRank()) + ".csv";
  std::ofstream file(filename);
  if (!file.is_open()) {
      std::cerr << "Failed to open file for writing." << std::endl;
      return -1;
  }
  std::vector<std::tuple<std::string, uint64_t, float>> data;
  if (type == "producer"){
    data = getProducerTimes() ;
  } else if (type == "consumer") {
    data = getConsumerTimes() ;
  } else{
    std::cerr << type <<" data does not exist, 'producer' and 'consumer' are the only supported types" << std::endl;
    return -1;
  }    std::cout << "Consumer size data " << data.size() << std::endl ;
  file << "type,size,duration\n";
  for (const auto& entry : data) {
      file << std::get<0>(entry) << ","
            << std::get<1>(entry) << ","
            << std::get<2>(entry) << "\n";
  }
  file.close();
  std::cout << "Producer stats successfully written to " << filename << std::endl ;
  return 0;
}
