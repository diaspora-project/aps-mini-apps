#pragma once

#include <adios2.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Result of one SST step (for one partition)
struct SSTPayload {
    std::vector<float>              data;      // Slice of data for this partition
    std::string                     metadata;  // JSON metadata for this partition
    // std::map<std::string, float>    metadata_json;  // JSON metadata for this partition
    std::uint64_t                   stepIndex = 0;
    bool                            endOfStream = false;
};

// A non-blocking SST scatter partition client.
// Each partition/thread creates one instance and pulls only its own slice.
class SSTStream {
public:
    // partitionId: which piece of the scatter this client reads (0 .. numPartitions-1)
    // numPartitions: total partitions (must match the producer)
    // streamName: SST stream created by Python writer
    SSTStream(const std::string &streamName,
                              int partitionId,
                              int numPartitions);

    ~SSTStream();

    // Non-blocking:
    //
    // Returns:
    //   true  → new data available (payload is valid)
    //   false → no new step or end-of-stream
    //
    // If end-of-stream happens, payload.endOfStream == true.
    bool pull_data(SSTPayload &out);

    bool is_eos() const {
        return m_eos.load();
    }

    bool is_active() const {
        return m_is_active.load();
    }

private:
    std::string m_streamName;
    int m_partitionId;
    int m_numPartitions;

    std::unique_ptr<adios2::ADIOS>  m_adios;
    std::unique_ptr<adios2::IO>     m_io;
    std::unique_ptr<adios2::Engine> m_engine;

    std::atomic<bool> m_eos;
    std::uint64_t     m_stepIndex;

    std::atomic<bool> m_is_active;

    // void parse_metadata_json(
    //         const std::string &json,
    //         std::vector<MetadataEntry> &out)
    // {
    //     out.clear();
    //     if (json.empty()) return;

    //     // Expect format like:
    //     // {"key1": 1.0, "key2": 2.3, "theta": 10.5 }
    //     //
    //     // This is NOT a general JSON parser.
    //     // It handles your flat numeric metadata.

    //     std::string s = json;

    //     // Remove braces
    //     if (!s.empty() && s.front() == '{') s.erase(0, 1);
    //     if (!s.empty() && s.back()  == '}') s.pop_back();

    //     std::stringstream ss(s);
    //     std::string token;

    //     while (std::getline(ss, token, ',')) {
    //         // token = " "key": value"
    //         auto colon = token.find(':');
    //         if (colon == std::string::npos) continue;

    //         std::string key = token.substr(0, colon);
    //         std::string val = token.substr(colon + 1);

    //         // cleanup: remove spaces and quotes
    //         auto trim = [&](std::string &x) {
    //             while (!x.empty() && (x.front() == ' ' || x.front() == '"'))
    //                 x.erase(0, 1);
    //             while (!x.empty() &&
    //                 (x.back() == ' ' || x.back() == '"'))
    //                 x.pop_back();
    //         };

    //         trim(key);
    //         trim(val);

    //         try {
    //             float f = std::stof(val);
    //             out.emplace_back(key, f);
    //         } catch (...) {
    //             // ignore non-numeric fields
    //         }
    //     }
    // }

};
