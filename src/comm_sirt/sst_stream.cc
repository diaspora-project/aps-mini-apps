#include "sst_stream.h"

#include <iostream>
#include <thread> // only needed for sleep in pull_data (optional)
#include <future>

SSTStream::SSTStream(const std::string &streamName,
                        int partitionId,
                        int numPartitions,
                        bool enable)
    : m_streamName(streamName),
      m_partitionId(partitionId),
      m_numPartitions(numPartitions),
      m_eos(false),
      m_stepIndex(0)
{
    if (enable == false) {
        m_is_active.store(false);
        return;
    }

    if (m_partitionId < 0 || m_partitionId >= m_numPartitions) {
        throw std::runtime_error("Invalid partitionId for SSTStream");
    }

    try {
        std::cout << "[Task " << m_partitionId << "] Initializing SSTStream for stream '"
              << m_streamName << "' with " << m_numPartitions << " partitions."
              << std::endl;

        m_adios = std::make_unique<adios2::ADIOS>();
        m_io    = std::make_unique<adios2::IO>(
            m_adios->DeclareIO("SSTScatterConsumerIO_" + std::to_string(m_partitionId)));

        m_io->SetEngine("SST");


        std::cout << "[Task " << m_partitionId << "] Opening SST stream '" << m_streamName
                << "' for reading." << std::endl;

        // You can set SST parameters here, e.g.:
        // m_io->SetParameters({{"OpenTimeoutSecs", "30"}});
        // m_io->SetParameter("RendezvousReaderCount", "1");
        m_io->SetParameter("QueueLimit", "1");
        m_io->SetParameter("OpenTimeoutSecs", "1"); // wait 1 seconds max

        // m_engine = std::make_unique<adios2::Engine>(
        //     m_io->Open(m_streamName, adios2::Mode::Read));

        // std::cout << "[Task " << m_partitionId << "] SSTStream initialized." << std::endl;
        // m_is_active.store(true);

        m_openFuture.emplace(async(std::launch::async, [&] {
            return m_io->Open(m_streamName, adios2::Mode::Read);
        }));

        if (m_openFuture->wait_for(std::chrono::seconds(10)) != std::future_status::ready) {
            // timed out
            m_is_active.store(false);
            m_eos.store(true);
            std::cout << "[Task " << m_partitionId << "] SSTStream initialization failed." << std::endl;
            return;
        }

        m_engine = std::make_unique<adios2::Engine>(m_openFuture->get());
        std::cout << "[Task " << m_partitionId << "] SSTStream initialized." << std::endl;
        m_openFuture.reset();
        m_is_active.store(true);
    }catch (const std::exception &ex) {
        std::cout << "SSTStream initialization failed: " << ex.what();
        m_is_active.store(false);
    }

}

SSTStream::~SSTStream()
{
    try {
        if (m_engine) {
            // In older ADIOS2 versions there is no Engine::Good().
            // Close() is safe to call once; if it's already closed
            // ADIOS2 will generally just ignore it or handle internally.
            m_engine->Close();
        }
    } catch (...) {
        // Destructors must not throw
    }
}

bool SSTStream::pull_data(SSTPayload &out)
{
    if (!is_active()) {
        return false;
    }

    if (m_eos.load()) {
        out.endOfStream = true;
        return false;
    }

    adios2::StepStatus status;

    try {
        // Non-blocking polling
        status = m_engine->BeginStep(adios2::StepMode::Read, 0.0 /*timeout sec*/);
    } catch (const std::exception &ex) {
        // This is where your std::runtime_error is coming from
        std::cerr << "[Partition " << m_partitionId
                  << "] BeginStep() failed: " << ex.what() << std::endl;

        // Treat this as end-of-stream (or a fatal error, your choice)
        m_eos.store(true);
        out.endOfStream = true;
        return false;
    }

    if (status == adios2::StepStatus::EndOfStream) {
        m_eos.store(true);
        out.endOfStream = true;
        return false;
    }

    if (status == adios2::StepStatus::NotReady) {
        // No new step yet
        return false;
    }

    if (status != adios2::StepStatus::OK) {
        // Unexpected, but treat as no data / soft error
        return false;
    }

    // --- We have a valid step ---
    ++m_stepIndex;

    auto varData        = m_io->InquireVariable<float>("data");
    auto varMetaBytes   = m_io->InquireVariable<std::uint8_t>("meta_bytes");
    auto varMetaOffsets = m_io->InquireVariable<std::int64_t>("meta_offsets");

    if (!varData || !varMetaBytes || !varMetaOffsets) {
        std::cerr << "[Partition " << m_partitionId
                  << "] Missing SST variables\n";
        m_engine->EndStep();
        return false;
    }

    auto dataShape = varData.Shape();        // [total_size]
    auto offShape  = varMetaOffsets.Shape(); // [numPartitions+1]

    if (dataShape.size() != 1 || offShape.size() != 1) {
        std::cerr << "[Partition " << m_partitionId
                  << "] Unexpected ADIOS2 shapes\n";
        m_engine->EndStep();
        return false;
    }

    std::size_t totalSize   = dataShape[0];
    std::size_t offsetsSize = offShape[0];

    if (offsetsSize < 2) {
        std::cerr << "[Partition " << m_partitionId
                  << "] meta_offsets too small\n";
        m_engine->EndStep();
        return false;
    }

    std::size_t numPartsStream = offsetsSize - 1;
    if (numPartsStream != static_cast<std::size_t>(m_numPartitions)) {
        std::cerr << "[Partition " << m_partitionId
                  << "] partition mismatch: producer=" << numPartsStream
                  << " local=" << m_numPartitions << "\n";
        m_engine->EndStep();
        return false;
    }

    if (totalSize % m_numPartitions != 0) {
        std::cerr << "[Partition " << m_partitionId
                  << "] totalSize not divisible by numPartitions\n";
        m_engine->EndStep();
        return false;
    }

    std::size_t chunkSize = totalSize / m_numPartitions;
    std::size_t start     = m_partitionId * chunkSize;

    varData.SetSelection(adios2::Box<adios2::Dims>({start}, {chunkSize}));
    std::vector<float> dataBuf(chunkSize);
    m_engine->Get(varData, dataBuf.data(), adios2::Mode::Sync);

    std::vector<std::int64_t> offsets(offsetsSize);
    varMetaOffsets.SetSelection(adios2::Box<adios2::Dims>({0}, {offsetsSize}));
    m_engine->Get(varMetaOffsets, offsets.data(), adios2::Mode::Sync);

    std::int64_t metaBegin = offsets[m_partitionId];
    std::int64_t metaEnd   = offsets[m_partitionId + 1];

    if (metaBegin < 0 || metaEnd < metaBegin) {
        std::cerr << "[Partition " << m_partitionId
                  << "] Invalid metadata offsets\n";
        m_engine->EndStep();
        return false;
    }

    std::size_t metaCount = static_cast<std::size_t>(metaEnd - metaBegin);
    std::string jsonStr;

    if (metaCount > 0) {
        varMetaBytes.SetSelection(
            adios2::Box<adios2::Dims>({static_cast<std::size_t>(metaBegin)},
                                      {metaCount}));

        std::vector<uint8_t> metaBuf(metaCount);
        m_engine->Get(varMetaBytes, metaBuf.data(), adios2::Mode::Sync);

        jsonStr.assign(reinterpret_cast<char *>(metaBuf.data()), metaCount);
    }

    m_engine->EndStep();

    out.data        = std::move(dataBuf);
    out.metadata    = std::move(jsonStr);
    out.stepIndex   = m_stepIndex;
    out.endOfStream = false;

    return true;
}

