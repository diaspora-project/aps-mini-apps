#ifndef FAKE_DIASPORA_STREAM_H
#define FAKE_DIASPORA_STREAM_H

#include <diaspora/TopicHandle.hpp>
#include <diaspora/Driver.hpp>
#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

using json = nlohmann::json;

// Minimal diaspora wrapper for fake-sirt: same handshake + same per-pass
// consume/publish cadence as the real DiasporaStream, but no SIRT engine,
// no TraceMetadata, no DataRegionBase — just enough to keep wire traffic
// byte-identical.
class FakeDiasporaStream {
  public:
    FakeDiasporaStream(std::string driver_type,
                       std::string driver_config_file,
                       size_t batchsize,
                       int rank,
                       int size);

    // SIRT -> DIST: push {"comm_size": size} on handshake_s_d;
    // DIST -> SIRT: read this rank's slice info from handshake_d_s.
    void handshake(int rank, int size);

    diaspora::Producer getProducer(const std::string& topic_name,
                                   const std::string& producer_name);
    diaspora::Consumer getConsumer(const std::string& topic_name,
                                   const std::string& consumer_name,
                                   std::vector<size_t> targets);

    // Pull up to `step` events from the consumer. Returns true if FIN seen.
    // Each pulled event is counted; data is discarded.
    bool pullStep(int step, diaspora::Consumer& consumer);

    // Push a zero-filled buffer of `n_floats` floats with the given metadata
    // to the producer (partition = rank). Used for sirt_den output.
    void publishZeros(json& meta, size_t n_floats,
                      diaspora::Producer& producer,
                      std::optional<size_t> partition = std::nullopt);

    json getInfo() const { return info; }
    int  getRank() const { return comm_rank; }
    uint32_t getCounter() const { return counter; }

    void recordTs(const std::string& entry);
    void writeTs(int rank);

  private:
    size_t batchsize;
    int comm_rank;
    int comm_size;
    uint32_t counter = 0;
    json info;

    diaspora::Driver driver;
    std::vector<std::string> m_ts_entries;

    diaspora::BatchSize   batchSize   = diaspora::BatchSize{batchsize};
    diaspora::ThreadCount threadCount = diaspora::ThreadCount{1};
    diaspora::Ordering    ordering    = diaspora::Ordering::Strict;

    diaspora::DataSelector data_selector = [](const diaspora::Metadata&,
                                              const diaspora::DataDescriptor& d) {
        return d;
    };
    diaspora::DataAllocator data_allocator = [](const diaspora::Metadata&,
                                                const diaspora::DataDescriptor& d) {
        return diaspora::DataView{new char[d.size()], d.size()};
    };

    static int64_t ts_now();
};

#endif
