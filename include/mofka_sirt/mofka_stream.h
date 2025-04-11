#ifndef MOFKA_STREAM_H
#define MOFKA_STREAM_H

#include "data_region_base.h"
#include <cassert>
#include <mofka/Client.hpp>
#include <mofka/TopicHandle.hpp>
#include <mofka/MofkaDriver.hpp>
#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include <time.h>
#include <string>
#include <iostream>
#include <fstream>
#include <vector>
#include <utility>
#include <nlohmann/json.hpp>
#include "trace_data.h"
#include <mofka_stream.h>
#include <queue>

using json = nlohmann::json;
namespace tl = thallium;



class MofkaStream
{
  private:
    size_t batchsize;
    uint32_t window_len;
    uint32_t counter;
    int comm_rank;
    int comm_size;

    std::vector<float> vproj;
    std::vector<float> vtheta;
    std::vector<json> vmeta;
    std::queue<mofka::Future<mofka::EventID>> futures;
    json info;

    mofka::MofkaDriver driver;
    size_t batch = 0;
    std::vector<float*> buffer;
    std::vector<std::tuple<std::string, uint64_t, float>> producer_times; // type, size, duration
    std::vector<std::tuple<std::string, uint64_t, float>> consumer_times; // type, size, duration

    mofka::BatchSize   batchSize   = mofka::BatchSize{batchsize};
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

    /* Add streaming message to buffers
    * @param event: mofka event containing data and metadata
    */
    void addTomoMsg(mofka::Event event);


    /* Erase streaming message to buffers
    */
    void eraseBegTraceMsg();


    /* Generates a data region that can be processed by Trace
    * @param recon_image: reconstruction image

      return: DataRegionBase
    */
    DataRegionBase<float, TraceMetadata>* setupTraceDataRegion(
      DataRegionBareBase<float> &recon_image);

  public:

    MofkaStream(std::string group_file,
                size_t batchsize,
                uint32_t window_len,
                int rank,
                int size);


    /* Handshake with Dist component
    * @param rank: MPI rank
    * @param size: MPI size
    */
    void handshake(int rank, int size);

    /* Publish reconstructed image
    * @param metadata: metadata in json format
    * @param data: pointer to the reconstructed image
    * @param producer: mofka producer
    */
    void publishImage(
      json &metadata,
      float *data,
      size_t size,
      mofka::Producer producer);


    /* Create and return a mofka producer
    * @param topic_name: mofka topic
    * @param producer_name: producer name

      return: mofka producer
    */
    mofka::Producer getProducer( std::string topic_name,
                                  std::string producer_name);

    /* Create and return a mofka consumer
    * @param topic_name: mofka topic
    * @param consumer_name: consumer name
    * @param targets: list of mofka partitions to consume from

      return: mofka consumer
    */
    mofka::Consumer getConsumer(std::string topic_name,
                                std::string consumer_name,
                                std::vector<size_t>);

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
      mofka::Consumer consumer);

    json getInfo();

    int getRank();

    int getBufferSize();

    uint32_t getBatch();

    uint32_t getCounter();

    void setInfo(json &j);

    void windowLength(uint32_t wlen);

    std::vector<std::tuple<std::string, uint64_t, float>> getConsumerTimes();

    void setConsumerTimes(std::string op, uint64_t size, float time);

    std::vector<std::tuple<std::string, uint64_t, float>> getProducerTimes();

    std::queue<mofka::Future<mofka::EventID>> getFutures();

    void setProducerTimes(std::string op, uint64_t size, float time);

    int writeTimes(std::string type);

};
#endif // MOFKA_STREAM_H