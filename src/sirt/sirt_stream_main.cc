#include <iomanip>
#include "mpi.h"
#include "trace_h5io.h"
#include "data_region_base.h"
#include "tclap/CmdLine.h"
#include "disp_comm_mpi.h"
#include "disp_engine_reduction.h"
#include "sirt.h"

#include <mofka/Client.hpp>
#include <mofka/TopicHandle.hpp>
#include <mofka/MofkaDriver.hpp>
#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include <time.h>
#include <string>
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace tl = thallium;

#include "trace_data.h"
#include <vector>

class MofkaStream
{
  private:

    uint32_t window_len;
    uint32_t counter;
    int comm_rank;
    int comm_size;
    tl::engine engine;
    mofka::MofkaDriver driver;

    std::vector<float> vproj;
    std::vector<float> vtheta;
    std::vector<json> vmeta;
    json info;

    // /// Add streaming message to vectors
    void addTomoMsg(mofka::Event event){

      mofka::Data data = event.data();
      mofka::Metadata metadata = event.metadata();
      event.acknowledge();

      vmeta.push_back(metadata.json()); /// Setup metadata
      vtheta.push_back(metadata.json()["theta"].get<float_t>());
      spdlog::info("Received data {}", metadata.string());
      vproj.insert(vproj.end(),
          static_cast<float*>(data.segments()[0].ptr),
          static_cast<float*>(data.segments()[0].ptr)+
          getInfo()["n_sinograms"].get<int32_t>() *
          getInfo()["n_rays_per_proj_row"].get<int32_t>());
    }

    void eraseBegTraceMsg(){
      vtheta.erase(vtheta.begin());
      size_t n_rays_per_proj =
      getInfo()["n_sinograms"].get<int64_t>() *
      getInfo()["n_rays_per_proj_row"].get<int64_t>();
      vproj.erase(vproj.begin(),vproj.begin()+n_rays_per_proj);
      vmeta.erase(vmeta.begin());
    }

    /// Generates a data region that can be processed by Trace
    DataRegionBase<float, TraceMetadata>* setupTraceDataRegion(
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

    mofka::BatchSize   batchSize   = mofka::BatchSize::Adaptive();
    mofka::ThreadCount threadCount = mofka::ThreadCount{1};
    mofka::Ordering    ordering    = mofka::Ordering::Strict;

    mofka::Validator         validator;
    mofka::Serializer        serializer;
    mofka::PartitionSelector selector;

    mofka::DataSelector data_selector = [](const mofka::Metadata& metadata,
                                    const mofka::DataDescriptor& descriptor) {
      (void)metadata;
      return descriptor;
    };

    mofka::DataBroker data_broker = [](const mofka::Metadata& metadata,
                                  const mofka::DataDescriptor& descriptor) {
        (void)metadata;
        return mofka::Data{new float[descriptor.size()], descriptor.size()};
    };

  public:

    MofkaStream(std::string protocol,
        std::string group_file,
        uint32_t window_len,
        int rank,
        int size):
      window_len {window_len},
      counter {0},
      comm_rank {rank},
      comm_size {size},
      engine {protocol, THALLIUM_SERVER_MODE},
      driver {group_file, engine}
      {}

    mofka::Producer producer( std::string topic_name,
                            std::string producer_name="streamer_sirt"){
      // -- Create/open a topic
      if (!driver.topicExists(topic_name)){
        driver.createTopic(topic_name, validator, selector, serializer);
        driver.addDefaultPartition(topic_name, 0);
      }
      mofka::TopicHandle topic = driver.openTopic(topic_name);
      // -- Get a producer for the topic
      mofka::Producer producer = topic.producer(producer_name,
                                                batchSize,
                                                threadCount,
                                                ordering);
      return producer;
    }

    mofka::Consumer consumer( std::string topic_name,
                              std::string consumer_name="dist_sirt",
                              std::vector<size_t> targets={0}){
      // -- wait for topic to be created
      while (!driver.topicExists(topic_name)){
        continue;
      }
      // -- Wait until all partitions are created by producer
      mofka::TopicHandle topic = driver.openTopic(topic_name);
      while (static_cast<int>(topic.partitions().size()) < comm_size) {
        topic = driver.openTopic(topic_name);
        continue;
      }
      mofka::Consumer consumer = topic.consumer(consumer_name,
                                                batchSize,
                                                threadCount,
                                                data_selector,
                                                data_broker,
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

    DataRegionBase<float, TraceMetadata>* readSlidingWindow(
      DataRegionBareBase<float> &recon_image,
      int step,
      mofka::Consumer consumer){

      // Dynamically meet sizes
      while(vtheta.size()> window_len)
        eraseBegTraceMsg();

      // Receive new message
      std::vector<mofka::Event> mofka_events;

      for(int i=0; i<step; ++i) {
        // mofka messages
        auto event = consumer.pull().wait();
        mofka_events.push_back(event);
        //if endMsg break
        if (event.metadata().json()["Type"].get<std::string>() == "FIN") return nullptr;
      }
      // TODO: After receiving message corrections might need to be applied

      /// End of the processing
      if(mofka_events.size()==0 && vtheta.size()==0){
        //std::cout << "End of the processing: " << vtheta.size() << std::endl;
        return nullptr;
      }
      /// End of messages, but there is data to be processed in window
      else if(mofka_events.size()==0 && vtheta.size()>0){
        for(int i=0; i<step; ++i){  // Delete step size element
          if(vtheta.size()>0) eraseBegTraceMsg();
          else break;
        }
        //std::cout << "End of messages, but there might be data in window:" << vtheta.size() << std::endl;
        if(vtheta.size()==0) return nullptr;
      }
      /// New message(s) arrived, there is space in window
      else if(mofka_events.size()>0 && vtheta.size()<window_len){
        //std::cout << "New message(s) arrived, there is space in window: " << window_len_ - vtheta.size() << std::endl;
        for(auto msg : mofka_events){
          addTomoMsg(msg);
          ++counter;
        }
      std::cout << "After adding # items in window: " << vtheta.size() << std::endl;
      }
      /// New message arrived, there is no space in window
      else if(mofka_events.size()>0 && vtheta.size()>=window_len){
        //std::cout << "New message arrived, there is no space in window: " << vtheta.size() << std::endl;
        for(int i=0; i<step; ++i) {
          if(vtheta.size()>0) eraseBegTraceMsg();
          else break;
        }
        for(auto msg : mofka_events){
          addTomoMsg(msg);
          ++counter;
        }
      }
      else std::cerr << "Unknown state in ReadWindow!" << std::endl;

      /// Clean-up vector
      mofka_events.clear();

      /// Generate new data and metadata
      DataRegionBase<float, TraceMetadata>* data_region =
        setupTraceDataRegion(recon_image);

      return data_region;
      }

    void handshake(int rank, int size){
      std::string topic = "handshake_s_d";

      // Send comm size to dist_streamer
      mofka::Producer hs_producer = producer(topic, "hs_p");
      json md = {{"comm_size", size}};
      mofka::Metadata metadata{md};
      auto future = hs_producer.push(metadata);
      future.wait();
      // Receive metadata info
      topic = "handshake_d_s";
      std::vector<size_t> targets = {static_cast<size_t>(rank)};
      mofka::Consumer hs_consumer = consumer(topic,
                                            "hs_c",
                                            targets);
      auto event = hs_consumer.pull().wait();
      mofka::Metadata m = event.metadata();
      json mdata = m.json();
      setInfo(mdata);
    }


    json getInfo(){ return info;}
    uint32_t getCounter(){ return counter;}
    void setInfo(json &j) {
      info = j;}
    void windowLength(uint32_t wlen){ window_len = wlen;}

    // /* Publish reconstructed slices.
    //   * @param slice Slice and its metadata information.
    //   */
    // void PublishImage(DataRegionBase<float, TraceMetadata> &slice){

    // }
};

class TraceRuntimeConfig {
  public:
    std::string kReconOutputPath;
    std::string kReconDatasetPath;
    std::string kReconOutputDir;
    std::string protocol;
    std::string group_file;
    int thread_count;
    int window_len;
    int window_step;
    int window_iter;
    int write_freq = 0;
    int center;
    std::string dest_host;
    int dest_port;
    std::string pub_addr;
    int pub_freq = 0;

    TraceRuntimeConfig(int argc, char **argv, int rank, int size){
      try
      {
        TCLAP::CmdLine cmd("SIRT Iterative Image Reconstruction", ' ', "0.01");
        TCLAP::ValueArg<std::string> argMofkaProtocol(
          "", "protocol", "Mofka protocol", false, "na+sm", "string");
        TCLAP::ValueArg<std::string> argGroupFile(
          "", "group-file", "Mofka group file", false, "mofka.json", "string");
        TCLAP::ValueArg<std::string> argReconOutputPath(
          "o", "reconOutputPath", "Output file path for reconstructed image (hdf5)",
          false, "./output.h5", "string");
        TCLAP::ValueArg<std::string> argReconOutputDir(
          "", "recon-output-dir", "Output directory for the streaming outputs",
          false, ".", "string");
        TCLAP::ValueArg<std::string> argReconDatasetPath(
          "r", "reconDatasetPath", "Reconstruction dataset path in hdf5 file",
          false, "/data", "string");
        TCLAP::ValueArg<float> argPubFreq(
          "", "pub-freq", "Publish frequency", false, 10000, "int");
        TCLAP::ValueArg<float> argCenter(
          "c", "center", "Center value", false, 0., "float");
        TCLAP::ValueArg<int> argThreadCount(
          "t", "thread", "Number of threads per process", false, 1, "int");
        TCLAP::ValueArg<float> argWriteFreq(
          "", "write-freq", "Write frequency", false, 10000, "int");
        TCLAP::ValueArg<float> argWindowLen(
          "", "window-length", "Number of projections that will be stored in the window",
          false, 32, "int");
        TCLAP::ValueArg<float> argWindowStep(
          "", "window-step", "Number of projections that will be received in each request",
          false, 1, "int");
        TCLAP::ValueArg<float> argWindowIter(
          "", "window-iter", "Number of iterations on received window",
          false, 1, "int");

        cmd.add(argMofkaProtocol);
        cmd.add(argGroupFile);
        cmd.add(argReconOutputPath);
        cmd.add(argReconOutputDir);
        cmd.add(argReconDatasetPath);

        cmd.add(argPubFreq);

        cmd.add(argCenter);
        cmd.add(argThreadCount);
        cmd.add(argWriteFreq);
        cmd.add(argWindowLen);
        cmd.add(argWindowStep);
        cmd.add(argWindowIter);

        cmd.parse(argc, argv);
        kReconOutputPath = argReconOutputPath.getValue();
        kReconOutputDir = argReconOutputDir.getValue();
        kReconDatasetPath = argReconDatasetPath.getValue();
        center = argCenter.getValue();
        thread_count = argThreadCount.getValue();
        write_freq= argWriteFreq.getValue();
        window_len= argWindowLen.getValue();
        window_step= argWindowStep.getValue();
        window_iter= argWindowIter.getValue();
        protocol = argMofkaProtocol.getValue();
        group_file = argGroupFile.getValue();

        std::cout << "MPI rank:"<< rank << "; MPI size:" << size << std::endl;
        if(rank==0)
        {
          std::cout << "Output file path=" << kReconOutputPath << std::endl;
          std::cout << "Output dir path=" << kReconOutputDir << std::endl;
          std::cout << "Recon. dataset path=" << kReconDatasetPath << std::endl;
          std::cout << "Center value=" << center << std::endl;
          std::cout << "Number of threads per process=" << thread_count << std::endl;
          std::cout << "Write frequency=" << write_freq << std::endl;
          std::cout << "Window length=" << window_len << std::endl;
          std::cout << "Window step=" << window_step << std::endl;
          std::cout << "Window iter=" << window_iter << std::endl;
          std::cout << "Publish frequency=" << pub_freq << std::endl;
          std::cout << "Mofka Protocol=" << protocol << std::endl;
          std::cout << "Group file=" << group_file << std::endl;
        }
      }
      catch (TCLAP::ArgException &e)
      {
        std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
      }
    }
};

int main(int argc, char **argv)
{
  /* Initiate middleware's communication layer */
  DISPCommBase<float> *comm =
        new DISPCommMPI<float>(&argc, &argv);
  TraceRuntimeConfig config(argc, argv, comm->rank(), comm->size());

  MofkaStream ms = MofkaStream{ config.protocol,
                                config.group_file,
                                static_cast<uint32_t>(config.window_len),
                                comm->rank(),
                                comm->size()};

  ms.handshake(comm->rank(), comm->size());
  std::string consuming_topic = "dist_sirt";
  std::string producing_topic = "sirt_den";
  mofka::Producer  producer = ms.producer(producing_topic, "sirt");
  mofka::Consumer consumer = ms.consumer(consuming_topic, "sirt");

  /* Get metadata structure */
  json tmetadata = ms.getInfo();
  auto n_blocks = tmetadata["n_sinograms"].get<int64_t>();
  auto num_cols = tmetadata["n_rays_per_proj_row"].get<int64_t>();

  /***********************/
  /* Initiate middleware */
  /* Prepare main reduction space and its objects */
  /* The size of the reconstruction object (in reconstruction space) is
   * twice the reconstruction object size, because of the length storage
   */
  auto main_recon_space = new SIRTReconSpace(
      n_blocks, 2*num_cols*num_cols);
  main_recon_space->Initialize(num_cols*num_cols);
  auto &main_recon_replica = main_recon_space->reduction_objects();
  float init_val=0.;
  main_recon_replica.ResetAllItems(init_val);

  /* Prepare processing engine and main reduction space for other threads */
  DISPEngineBase<SIRTReconSpace, float> *engine =
    new DISPEngineReduction<SIRTReconSpace, float>(
        comm,
        main_recon_space,
        config.thread_count);
        /// # threads (0 for auto assign the number of threads)

  /**********************/

  /**************************/
  /* Perform reconstruction */
  /* Define job size per thread request */
  #ifdef TIMERON
  std::chrono::duration<double> recon_tot(0.), inplace_tot(0.), update_tot(0.),
    datagen_tot(0.);
  std::chrono::duration<double> write_tot(0.);
  #endif
  DataRegionBase<float, TraceMetadata> *curr_slices = nullptr;
  /// Reconstructed image
  DataRegionBareBase<float> recon_image(n_blocks*num_cols*num_cols);
  for(size_t i=0; i<recon_image.count(); ++i)
    recon_image[i]=0.; /// Initial values of the reconstructe image

  /// Number of requested ray-sum values by each thread poll
  int64_t req_number = num_cols;
  /// Required data structure for dumping image to h5 file
  trace_io::H5Metadata h5md;
  h5md.ndims=3;
  h5md.dims= new hsize_t[3];
  h5md.dims[1] = tmetadata["tn_sinograms"].get<int64_t>();
  h5md.dims[0] = 0;   /// Number of projections is unknown
  h5md.dims[2] = tmetadata["n_rays_per_proj_row"].get<int64_t>();

  for(int passes=0; ; ++passes){
      #ifdef TIMERON
      auto datagen_beg = std::chrono::system_clock::now();
      #endif
      curr_slices = ms.readSlidingWindow(recon_image, config.window_step, consumer);
      if(config.center!=0 && curr_slices!=nullptr)
        curr_slices->metadata().center(config.center);
      #ifdef TIMERON
      datagen_tot += (std::chrono::system_clock::now()-datagen_beg);
      #endif

      if(curr_slices == nullptr) break; /// If nullptr, there is no more projections
      /// Iterate on window
      for(int i=0; i<config.window_iter; ++i){
        #ifdef TIMERON
        auto recon_beg = std::chrono::system_clock::now();
        #endif
        engine->RunParallelReduction(*curr_slices, req_number);  /// Reconstruction

        #ifdef TIMERON
        recon_tot += (std::chrono::system_clock::now()-recon_beg);
        auto inplace_beg = std::chrono::system_clock::now();
        #endif
        engine->ParInPlaceLocalSynchWrapper();              /// Local combination
        #ifdef TIMERON
        inplace_tot += (std::chrono::system_clock::now()-inplace_beg);

        /// Update reconstruction object
        auto update_beg = std::chrono::system_clock::now();
        #endif
        main_recon_space->UpdateRecon(recon_image, main_recon_replica);
        #ifdef TIMERON
        update_tot += (std::chrono::system_clock::now()-update_beg);
        #endif
        engine->ResetReductionSpaces(init_val);
        curr_slices->ResetMirroredRegionIter();
      }

      /* Emit reconstructed data */
      #ifdef TIMERON
      auto write_beg = std::chrono::system_clock::now();
      #endif
      if(!(passes%config.write_freq)){
        std::stringstream iteration_stream;
        iteration_stream << std::setfill('0') << std::setw(6) << passes;
        std::string outputpath = config.kReconOutputDir + "/" +
          iteration_stream.str() + "-recon.h5";
        trace_io::WriteRecon(
            curr_slices->metadata(), h5md,
            outputpath, config.kReconDatasetPath);

        try {
          TraceMetadata &rank_metadata = curr_slices->metadata();

          int recon_slice_data_index = rank_metadata.num_neighbor_recon_slices()* rank_metadata.num_grids() * rank_metadata.num_grids();
          ADataRegion<float> &recon = rank_metadata.recon();

          hsize_t ndims = static_cast<hsize_t>(h5md.ndims);
          hsize_t rank_dims[3] = {
            static_cast<hsize_t>(rank_metadata.num_slices()),
            static_cast<hsize_t>(rank_metadata.num_cols()),
            static_cast<hsize_t>(rank_metadata.num_cols())};
          hsize_t app_dims[3] = {
            static_cast<hsize_t>(h5md.dims[1]),
            static_cast<hsize_t>(h5md.dims[2]),
            static_cast<hsize_t>(h5md.dims[2])};

          json md = { {"Type", "DATA"},
                      {"iteration_stream", iteration_stream.str()},
                      {"rank_dims", rank_dims},
                      {"app_dims", app_dims},
                      {"recon_slice_data_index", recon_slice_data_index}};

          mofka::Metadata metadata{md};

          mofka::Data data = mofka::Data(&recon[recon_slice_data_index], 4*rank_dims[0]*rank_dims[1]*rank_dims[2]);
          auto future = producer.push(metadata, data);
          producer.flush();

        } catch(const mofka::Exception& ex) {

          spdlog::critical("{}", ex.what());
          exit(-1);
        }

      }
      #ifdef TIMERON
      write_tot += (std::chrono::system_clock::now()-write_beg);
      #endif
      //delete curr_slices->metadata(); //TODO Check for memory leak
      delete curr_slices;
  }

  json md = {{"Type", "FIN"}};
  auto future = producer.push(mofka::Metadata{md}, mofka::Data{});

  /**************************/
  #ifdef TIMERON
  if(comm->rank()==0){
    std::cout << "Reconstruction time=" << recon_tot.count() << std::endl;
    std::cout << "Local combination time=" << inplace_tot.count() << std::endl;
    std::cout << "Update time=" << update_tot.count() << std::endl;
    //std::cout << "Write time=" << write_tot.count() << std::endl;
    std::cout << "Data gen total time=" << datagen_tot.count() << std::endl;
    std::cout << "Total comp=" << recon_tot.count() + inplace_tot.count() + update_tot.count() << std::endl;
    std::cout << "Sustained proj/sec=" << ms.getCounter() /
                                          (recon_tot.count()+inplace_tot.count()+update_tot.count()) << std::endl;
  }
  #endif
}