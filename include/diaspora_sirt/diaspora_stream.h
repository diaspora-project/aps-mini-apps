#ifndef MOFKA_STREAM_H
#define MOFKA_STREAM_H

#include "data_region_base.h"
#include <cassert>
#include <diaspora/TopicHandle.hpp>
#include <diaspora/Driver.hpp>
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
#include <diaspora_stream.h>
#include <queue>

using json = nlohmann::json;



class DiasporaStream
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
    std::queue<diaspora::Future<std::optional<diaspora::EventID>>> futures;
    json info;

    diaspora::Driver driver;
    size_t batch = 0;
    std::vector<float*> buffer;
    std::vector<std::tuple<std::string, uint64_t, float>> producer_times; // type, size, duration
    std::vector<std::tuple<std::string, uint64_t, float>> consumer_times; // type, size, duration

    diaspora::BatchSize   batchSize   = diaspora::BatchSize{batchsize};
    diaspora::ThreadCount threadCount = diaspora::ThreadCount{1};
    diaspora::Ordering    ordering    = diaspora::Ordering::Strict;

    diaspora::Validator         validator;
    diaspora::Serializer        serializer;
    diaspora::PartitionSelector selector;

    diaspora::DataSelector data_selector = [](const diaspora::Metadata& metadata,
                                           const diaspora::DataDescriptor& descriptor) {
      (void)metadata;
      return descriptor;
    };

    diaspora::DataAllocator data_allocator = [](const diaspora::Metadata& metadata,
                                                const diaspora::DataDescriptor& descriptor) {
        (void)metadata;
        return diaspora::DataView{new float[descriptor.size()], descriptor.size()};
    };

    /* Add streaming message to buffers
    * @param event: diaspora event containing data and metadata
    */
    void addTomoMsg(diaspora::Event event);


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

    DiasporaStream(std::string driver_type,
                   std::string driver_config_file,
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
    * @param producer: diaspora producer
    */
    void publishImage(
      json &metadata,
      float *data,
      size_t size,
      diaspora::Producer producer);


    /* Create and return a diaspora producer
    * @param topic_name: diaspora topic
    * @param producer_name: producer name

      return: diaspora producer
    */
    diaspora::Producer getProducer( std::string topic_name,
                                  std::string producer_name);

    /* Create and return a diaspora consumer
    * @param topic_name: diaspora topic
    * @param consumer_name: consumer name
    * @param targets: list of diaspora partitions to consume from

      return: diaspora consumer
    */
    diaspora::Consumer getConsumer(std::string topic_name,
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
      diaspora::Consumer consumer);

    json getInfo();

    int getRank();

    int getBufferSize();

    uint32_t getBatch();

    uint32_t getCounter();

    void setInfo(json &j);

    void windowLength(uint32_t wlen);

    const std::vector<std::tuple<std::string, uint64_t, float>>& getConsumerTimes();

    void setConsumerTimes(std::string op, uint64_t size, float time);

    std::vector<std::tuple<std::string, uint64_t, float>> getProducerTimes();

    std::queue<diaspora::Future<std::optional<diaspora::EventID>>>& getFutures();

    void setProducerTimes(std::string op, uint64_t size, float time);

    int writeTimes(std::string type);

};
#endif // MOFKA_STREAM_H
