// mt_scatter_json_consumer.cpp
#include <adios2.h>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <string>

std::atomic<bool> g_stop{false};

void consumer_thread(int tid, int numThreads, const std::string &streamName)
{
    try {
        adios2::ADIOS adios;
        std::string ioName = "ConsumerIO_" + std::to_string(tid);
        adios2::IO io = adios.DeclareIO(ioName);
        io.SetEngine("SST");

        adios2::Engine reader = io.Open(streamName, adios2::Mode::Read);

        while (!g_stop.load()) {
            auto status = reader.BeginStep();
            if (status == adios2::StepStatus::EndOfStream) {
                std::cout << "[Thread " << tid << "] End of stream.\n";
                break;
            }
            if (status != adios2::StepStatus::OK) {
                continue;
            }

            // ---- inquire variables ----
            auto varData        = io.InquireVariable<double>("data");
            auto varMetaBytes   = io.InquireVariable<uint8_t>("meta_bytes");
            auto varMetaOffsets = io.InquireVariable<long long>("meta_offsets"); // int64

            if (!varData || !varMetaBytes || !varMetaOffsets) {
                std::cerr << "[Thread " << tid << "] Missing variables.\n";
                reader.EndStep();
                continue;
            }

            // ---- figure out our data slice ----
            auto dataShape = varData.Shape();       // [total_size]
            auto offShape  = varMetaOffsets.Shape(); // [numChunks+1]

            if (dataShape.size() != 1 || offShape.size() != 1) {
                std::cerr << "[Thread " << tid << "] unexpected shapes.\n";
                reader.EndStep();
                continue;
            }

            std::size_t total_size = dataShape[0];
            std::size_t off_len    = offShape[0];
            std::size_t numChunks  = off_len - 1;

            if (tid < 0 || tid >= (int)numChunks) {
                std::cerr << "[Thread " << tid << "] tid>=numChunks.\n";
                reader.EndStep();
                continue;
            }

            std::size_t chunk_size = total_size / numChunks;
            std::size_t start      = (std::size_t)tid * chunk_size;
            std::size_t count      = chunk_size;

            // ---- read our data slice ----
            varData.SetSelection(adios2::Box<adios2::Dims>({start}, {count}));
            std::vector<double> dataBuf(count);
            reader.Get(varData, dataBuf.data(), adios2::Mode::Sync);

            // ---- read offsets for all chunks (small) ----
            std::vector<long long> offBuf(off_len);
            // read entire meta_offsets
            varMetaOffsets.SetSelection(adios2::Box<adios2::Dims>({0}, {off_len}));
            reader.Get(varMetaOffsets, offBuf.data(), adios2::Mode::Sync);

            // compute our metadata byte range
            long long metaBegin = offBuf[tid];
            long long metaEnd   = offBuf[tid + 1];
            if (metaBegin < 0 || metaEnd < metaBegin) {
                std::cerr << "[Thread " << tid << "] bad meta offsets.\n";
                reader.EndStep();
                continue;
            }
            std::size_t metaCount = (std::size_t)(metaEnd - metaBegin);

            // ---- read only our JSON bytes ----
            varMetaBytes.SetSelection(
                adios2::Box<adios2::Dims>(
                    {static_cast<std::size_t>(metaBegin)}, {metaCount}));

            std::vector<uint8_t> metaBuf(metaCount);
            reader.Get(varMetaBytes, metaBuf.data(), adios2::Mode::Sync);

            reader.EndStep();

            // convert bytes to string (JSON text)
            std::string jsonStr(metaBuf.begin(), metaBuf.end());

            std::cout << "[Thread " << tid << "] data[0]=" << dataBuf[0]
                      << ", data[last]=" << dataBuf.back()
                      << ", JSON=" << jsonStr << std::endl;

            // You can then parse jsonStr with your favorite JSON lib (nlohmann/json, etc.)
        }

        reader.Close();
    }
    catch (const std::exception &e) {
        std::cerr << "[Thread " << tid << "] exception: " << e.what() << "\n";
    }
}

int main()
{
    const std::string streamName = "sirt_stream";
    const int numThreads = 4;   // must match num_chunks

    std::vector<std::thread> threads;
    threads.reserve(numThreads);

    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back(consumer_thread, t, numThreads, streamName);
    }
    for (auto &th : threads) {
        th.join();
    }
    return 0;
}
