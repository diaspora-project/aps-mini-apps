#include <mofka_stream.h>
#include <thread>

void MofkaStream::addTomoMsg(StreamEvent event){
  if (event.isFromSST()) {
    SSTPayload sst_payload = event.getSSTPayload();
    vmeta.push_back(json::parse(sst_payload.metadata)); /// Setup metadata
    vtheta.push_back(vmeta.back()["theta"].get<float_t>());
    size_t n_rays_per_proj =
      getInfo()["n_sinograms"].get<int64_t>() *
      getInfo()["n_rays_per_proj_row"].get<int64_t>();
    assert(sst_payload.data.size() == n_rays_per_proj && "Pointer size does not match n_rays_per_projection");
    vproj.insert(vproj.end(), sst_payload.data.begin(), sst_payload.data.end());
  }else{
    auto start_t = std::chrono::high_resolution_clock::now();
    mofka::Event mofka_event = event.getMofkaEvent();
    mofka::Metadata metadata = mofka_event.metadata();
    auto end_t = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_t - start_t;
    setConsumerTimes("mata_t", metadata.string().size(), elapsed.count());
    start_t = std::chrono::high_resolution_clock::now();
    mofka::Data data = mofka_event.data();
    end_t = std::chrono::high_resolution_clock::now();
    elapsed = end_t - start_t;
    setConsumerTimes("data_t", data.segments()[0].size, elapsed.count());
    // event.acknowledge(); // acknowledge event
    pending_events.push_back(mofka_event);
    vmeta.push_back(metadata.json()); /// Setup metadata
    vtheta.push_back(metadata.json()["theta"].get<float_t>());
    // spdlog::info("Received data {}", metadata.string());

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
}

std::thread MofkaStream::receiveEventInBackground(mofka::Consumer consumer)
{
  return std::thread([this, consumer = std::move(consumer)]() mutable {

    std::cout << "[Task-" << getRank()
              << "]: Starting background thread to receive Mofka events..."
              << std::endl;

    while (!isEndOfStream()) {
      auto start = std::chrono::high_resolution_clock::now();
      mofka::Future<mofka::Event> future_event = consumer.pull();

      // Poll until event is ready (or EOS)
      while (!future_event.completed() && !isEndOfStream()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        {
          std::lock_guard<std::mutex> lock(this->mofka_buffer_mutex);
          while (!mofka_buffered_events.empty()) {
            auto &event = mofka_buffered_events.front();
            if (event.metadata().json()["Type"] != "MSG_DATA_REP") {
              std::cout << "[Task-" << getRank()
                        << "]: Received non-DATA event: " << event.metadata().json()["Type"]
                        << std::endl;
              break;
            }
            if (event.metadata().json()["seq_n"].get<int>() <= this->ckpt_progress) {
              event.acknowledge();
              std::cout << "[Task-" << getRank()
                        << "]: Acknowledge (buffered): seq_id = "
                        << event.metadata().json()["seq_n"].get<int>()
                        << std::endl;
              mofka_buffered_events.erase(mofka_buffered_events.begin());
            } else {
              break;
            }
          }
        } // lock_guard released here
      }

      if (isEndOfStream()) {
        break;
      }

      if (future_event.completed()) {
        auto event = future_event.wait();
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end - start;
        setConsumerTimes("wait_t", 1, elapsed.count());

        std::lock_guard<std::mutex> lock(this->mofka_buffer_mutex);
        mofka_buffered_events.push_back(event);
      }
    }
  });
}


bool MofkaStream::getMofkaBufferedEvent(mofka::Event &event) {
  std::lock_guard<std::mutex> lock(this->mofka_buffer_mutex);
  if (mofka_buffered_events.size() > 0) {
    event = std::move(mofka_buffered_events.front());
    mofka_buffered_events.erase(mofka_buffered_events.begin());
    return true;
  }else{
    return false;
  }
}



/* Erase streaming message to buffers
*/
void MofkaStream::eraseBegTraceMsg(){
  progress++; // Update progress = # processed messages
  std::cout << "[Task-" << getRank() << "]: Advancing sliding window: Progress: " << progress << std::endl;
  vtheta.erase(vtheta.begin());
  size_t n_rays_per_proj =
    getInfo()["n_sinograms"].get<int64_t>() *
    getInfo()["n_rays_per_proj_row"].get<int64_t>();
  vproj.erase(vproj.begin(),vproj.begin()+n_rays_per_proj);
  vmeta.erase(vmeta.begin());
}

void MofkaStream::acknowledge() {
  auto current_proj_id = (*vmeta.begin())["seq_n"].get<int>();
  while (!pending_events.empty()) {
    auto event = pending_events.begin();
    auto event_prog_id = event->metadata().json()["seq_n"].get<int>();
    // if (event_prog_id < current_proj_id) {
    if (event_prog_id < current_proj_id + (int)window_len) {
    // if (event_prog_id <= ckpt_progress) {
      event->acknowledge();
      std::cout << "[Task-" << getRank() << "]: Acknowledge: seq_id = " << event_prog_id << std::endl;
      pending_events.erase(event);
      if (event_prog_id > ckpt_progress) {
        ckpt_progress = event_prog_id;
      }
    } else {
      break;
    }
  }
}


/* Generates a data region that can be processed by Trace
* @param recon_image: reconstruction image

  return: DataRegionBase
*/
DataRegionBase<float, TraceMetadata>* MofkaStream::setupTraceDataRegion(
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

MofkaStream::MofkaStream(mofka::MofkaDriver driver,
            size_t batchsize,
            uint32_t window_len,
            int rank,
            // int size,
            int progress) // Removed default argument
  : batchsize {batchsize},
  window_len {window_len},
  counter {0},
  comm_rank {rank},
  // comm_size {size},
  progress {progress},
  next_seq {progress+1},
  driver {driver}
  {}


/* Handshake with Dist component
* @param worker_index: worker index
* @param num_workers:  number of workers

* This function sets up the handshake with the distributed component.
* It sends the worker index and number of workers to the distributed streamer.
* It also receives metadata information from the distributed streamer.
*/
void MofkaStream::handshake(int task_index) {
  // Receive metadata info
  std::string topic_name = "handshake_d_s";
  std::vector<size_t> targets = {static_cast<size_t>(task_index)};
  mofka::TopicHandle topic = driver.openTopic(topic_name);
  mofka::Consumer hs_consumer = topic.consumer( "hs_c",
                                                batchSize,
                                                threadCount,
                                                targets);
  auto event = hs_consumer.pull().wait();
  mofka::Metadata m = event.metadata();
  json mdata = m.json();
  setInfo(mdata);
}

/* Publish reconstructed image
* @param metadata: metadata in json format
* @param data:     pointer to the reconstructed image
* @param producer: mofka producer
*/
void MofkaStream::publishImage(json &meta, float *data, size_t size, mofka::Producer producer){
  mofka::Metadata metadata{meta};
  float* copy = new float[size];
  std::memcpy(copy, data, size * sizeof(float));
  buffer.push_back(copy);
  mofka::Data data_m = mofka::Data(buffer[buffer.size()-1], size*sizeof(float));
  auto start = std::chrono::high_resolution_clock::now();
  auto future = producer.push(metadata, data_m);
  //future.wait();
  batch++;
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed_push = end - start;
  std::chrono::duration<double> elapsed_flush = end - end;
  // std::cout << "Push " << elapsed.count() << " sec" << std::endl;
  producer_times.emplace_back("Push", size*sizeof(float), elapsed_push.count());
  if (batch == batchsize){
    start = std::chrono::high_resolution_clock::now();
    producer.flush();
    end = std::chrono::high_resolution_clock::now();
    elapsed_flush = end - start;
    // std::cout << "Flush " << batch << " Time: " << elapsed_flush.count() << " sec" << std::endl;
    producer_times.emplace_back("Flush", buffer.size()*size*sizeof(float), elapsed_flush.count());
    for (float* ptr : buffer) {
        if (ptr==nullptr) continue;
        else delete[] ptr;
    }
    buffer.clear();
    batch=0;
  }
}


/* Create and return a mofka producer
* @param topic_name:    mofka topic
* @param producer_name: producer name

  return: mofka producer
*/
mofka::Producer MofkaStream::getProducer(std::string topic_name,
                                         std::string producer_name="streamer_sirt"){
  auto topic = driver.openTopic(topic_name);
  mofka::Producer producer = topic.producer(producer_name,
                                            batchSize,
                                            threadCount,
                                            ordering);
  return producer;
}

/* Create and return a mofka consumer
* @param topic_name:    mofka topic
* @param consumer_name: consumer name
* @param targets:       list of mofka partitions to consume from

  return: mofka consumer
*/
mofka::Consumer MofkaStream::getConsumer(std::string topic_name,
                                         std::string consumer_name="dist_sirt",
                                         std::vector<size_t> targets={0}){
  mofka::TopicHandle topic = driver.openTopic(topic_name);
  mofka::Consumer consumer = topic.consumer(consumer_name,
                                            threadCount,
                                            batchSize,
                                            data_selector,
                                            data_broker,
                                            targets);
  return consumer;
}

void MofkaStream::interrupt(int signal) {
  interrupt_signal = signal;
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

DataRegionBase<float, TraceMetadata>* MofkaStream::readSlidingWindow(
  DataRegionBareBase<float> &recon_image,
  int step,
  mofka::Consumer consumer,
  SSTStream& sst_stream){
  // Dynamically meet sizes
  while(vtheta.size()> window_len)
    eraseBegTraceMsg();

  // Receive new message
  // std::vector<mofka::Event> mofka_events;
  std::vector<StreamEvent> stream_events;

  for(int i=0; i<step; ++i) {
    // mofka messages

    bool added_new_data = false;

    while (!added_new_data) {

      if (interrupt_signal) {
        std::cout << "[Task-" << getRank() << "]: Interrupt signal received, stopping pull." << std::endl;
        return nullptr; // Exit if interrupt signal is received
      }

      // Clean up SST payloads before getting new data
      if (!this->pending_sst_payloads.empty() && pending_sst_payloads[0].stepIndex == this->next_seq) {
        std::cout << "[Task-" << getRank() << "]: Processing pending SST stepIndex: " << this->pending_sst_payloads[0].stepIndex << std::endl;
        // Add to stream events
        stream_events.push_back(StreamEvent(this->pending_sst_payloads[0]));
        this->pending_sst_payloads.erase(this->pending_sst_payloads.begin());
        this->next_seq++;
        continue;
      }

      if (sst_stream.is_eos() && this->pending_sst_payloads.empty()) {
        std::cout << "[Task-" << getRank() << "]: SST stream has ended and no pending SST payloads." << std::endl;
        setEndOfStream(true);
        return nullptr;
      }

      // std::cout << "[Task-" << getRank() << "]: Checking for new data from SST stream..." << std::endl;

      // Check SST stream for fast data
      SSTPayload sst_payload;
      if (sst_stream.pull_data(sst_payload)) {

        auto metadata_json = json::parse(sst_payload.metadata);
        auto stepIndex = metadata_json["seq_n"].get<uint64_t>();
        sst_payload.stepIndex = stepIndex;

        if (stepIndex > this->next_seq) {
          std::cout << "[Task-" << getRank() << "]: Delay processing SST stepIndex: " << stepIndex
                    << " > " << this->next_seq << " = next_seq" << std::endl;
          sst_payload.stepIndex = stepIndex;
          this->pending_sst_payloads.push_back(sst_payload);
        }else if (stepIndex < this->next_seq) {
          std::cout << "[Task-" << getRank() << "]: Skipping SST stepIndex: " << stepIndex
                    << " < " << this->next_seq << " = next_seq" << std::endl;
        }else{

          std::cout << "[Task-" << getRank() << "]: Received data from SST stream, stepIndex: " << stepIndex << std::endl;
          this->next_seq = stepIndex + 1;
          // Add to stream events
          stream_events.push_back(StreamEvent(sst_payload));
          added_new_data = true;
        }
      }else{
        // If no SST data, pull from mofka
        // auto start = std::chrono::high_resolution_clock::now();
        // mofka::Future<mofka::Event> future_event = consumer.pull();
        // if (!future_event.completed()) {
        //   // sleep for 1 ms to avoid busy waiting
        //   std::this_thread::sleep_for(std::chrono::milliseconds(1));
        // }
        // if (future_event.completed()) {
          // auto event = future_event.wait();
          // // auto event = consumer.pull().wait();

        // std::cout << "[Task-" << getRank() << "]: Checking for new data from Mofka..." << std::endl;

        mofka::Event event;
        if (getMofkaBufferedEvent(event)) {
          //if endMsg break
          if (event.metadata().json()["Type"].get<std::string>() == "FIN") {
            setEndOfStream(true);
            std::cout << "[Task-" << getRank() << "]: End of stream detected" << std::endl;
            return nullptr;
          }
          
          int sequence_id = event.metadata().json()["seq_n"].get<int>();
          int proj_id = event.metadata().json()["projection_id"].get<int>();
          double theta = event.metadata().json()["theta"].get<float>();
          double center = event.metadata().json()["center"].get<float>();
          
          if (this->next_seq > sequence_id+1) {
            std::cout << "[Task-" << getRank() << "]: Mofka: Skipping seq_id: " << sequence_id
                      << " < " << this->next_seq - 1 << " = (next_seq - 1)" << std::endl;
            // Skip this event
            this->pending_events.push_back(event);
          }else{
            this->next_seq = sequence_id + 1;
            std::cout << "[Task-" << getRank() << "]: Received data from Mofka: seq_id: " << sequence_id << " projection_id: " << proj_id << " theta: " << theta << " center: " << center << ", progress = " << progress << std::endl;
            stream_events.push_back(StreamEvent(event));
            added_new_data = true;
          }
        }else{
          // sleep for 1 ms to avoid busy waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
      }
    }
    
    // float* data = static_cast<float*>(event.data().segments()[0].ptr);
    // auto p = reinterpret_cast<const unsigned char*>(data);
    // std::cout << "[Task-" << getRank() << "] -- Processing window with " 
    //         << " sequence_id " << sequence_id 
    //         << " center=" << center 
    //         << "First float value: " << data[0] << " First value: " << static_cast<unsigned>(p[0]) << std::endl;

    // // Only add the event if its sequence_id higher than the progress
    // int sequence_id = event.metadata().json()["seq_n"].get<int>();
    // std::cout << "[Task-" << getRank() << "]: seq_id: " << sequence_id << ", progress = " << progress << std::endl;
    // if (sequence_id < progress) {
    //   std::cout << "[Task-" << getRank() << "]: Skipping seq_id: " << sequence_id << " < " << progress << " = progress" << std::endl;
    //   continue; // Skip this event
    // }
  }
  // TODO: After receiving message corrections might need to be applied

  /// End of the processing
  if(stream_events.size()==0 && vtheta.size()==0){
    //std::cout << "End of the processing: " << vtheta.size() << std::endl;
    return nullptr;
  }
  /// End of messages, but there is data to be processed in window
  else if(stream_events.size()==0 && vtheta.size()>0){
    for(int i=0; i<step; ++i){  // Delete step size element
      if(vtheta.size()>0) eraseBegTraceMsg();
      else break;
    }
    //std::cout << "End of messages, but there might be data in window:" << vtheta.size() << std::endl;
    if(vtheta.size()==0) return nullptr;
  }
  /// New message(s) arrived, there is space in window
  else if(stream_events.size()>0 && vtheta.size()<window_len){
    //std::cout << "New message(s) arrived, there is space in window: " << window_len_ - vtheta.size() << std::endl;
    for(auto msg : stream_events){
      addTomoMsg(msg);
      ++counter;
    }
    std::cout << "After adding # items in window: " << vtheta.size() << std::endl;
  }
  /// New message arrived, there is no space in window
  else if(stream_events.size()>0 && vtheta.size()>=window_len){
    //std::cout << "New message arrived, there is no space in window: " << vtheta.size() << std::endl;
    for(int i=0; i<step; ++i) {
      if(vtheta.size()>0) eraseBegTraceMsg();
      else break;
    }
    for(auto msg : stream_events){
      addTomoMsg(msg);
      ++counter;
    }
  }
  else std::cerr << "Unknown state in ReadWindow!" << std::endl;

  /// Clean-up vector
  stream_events.clear();

  /// Generate new data and metadata
  DataRegionBase<float, TraceMetadata>* data_region =
    setupTraceDataRegion(recon_image);

  return data_region;
}

json MofkaStream::getInfo(){ return info;}

int MofkaStream::getRank() {return comm_rank;}

int MofkaStream::getBufferSize() {return buffer.size();}

uint32_t MofkaStream::getBatch() {return batch;}

uint32_t MofkaStream::getCounter(){ return counter;}

void MofkaStream::setInfo(json &j) {info = j;}

void MofkaStream::windowLength(uint32_t wlen){ window_len = wlen;}

std::vector<std::tuple<std::string, uint64_t, float>> MofkaStream::getConsumerTimes(){return consumer_times;}

void MofkaStream::setConsumerTimes(std::string op, uint64_t size, float time){
  consumer_times.emplace_back(op, size, time);
}

std::vector<std::tuple<std::string, uint64_t, float>> MofkaStream::getProducerTimes(){return producer_times;}

void MofkaStream::setProducerTimes(std::string op, uint64_t size, float time){
  producer_times.emplace_back(op, size, time);
}

int MofkaStream::writeTimes(std::string path, std::string type){
  std::string filename = path + "/Sirt_"+ type + "_rank_" + std::to_string(getRank()) + ".csv";
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


