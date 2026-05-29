#include "fake_diaspora_stream.h"

#include <cstring>

int64_t FakeDiasporaStream::ts_now() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

void FakeDiasporaStream::recordTs(const std::string& entry) {
    m_ts_entries.push_back(fmt::format("{} {}", ts_now(), entry));
}

void FakeDiasporaStream::writeTs(int rank) {
    std::ofstream f("sirt." + std::to_string(rank) + ".ts.txt");
    for (auto& e : m_ts_entries) f << e << "\n";
}

FakeDiasporaStream::FakeDiasporaStream(std::string driver_type,
                                       std::string driver_config_file,
                                       size_t batchsize_,
                                       int rank,
                                       int size)
  : batchsize{batchsize_},
    comm_rank{rank},
    comm_size{size} {
    diaspora::Metadata driver_options;
    if (!driver_config_file.empty()) {
        auto ifs = std::ifstream(driver_config_file);
        if (!ifs.good()) {
            throw diaspora::Exception{std::string{"Cannot open driver config file "}
                                      + driver_config_file};
        }
        driver_options = json::parse(ifs);
    } else if (driver_type == "files") {
        driver_options = json::parse("{\"root_path\":\"./diaspora-data\"}");
    }
    driver = diaspora::Driver::New(driver_type.c_str(), driver_options);
}

void FakeDiasporaStream::handshake(int rank, int size) {
    diaspora::Producer hs_producer = getProducer("handshake_s_d", "hs_p");
    json md = {{"comm_size", size}};
    hs_producer.push(diaspora::Metadata{md});
    hs_producer.flush().wait(-1);

    std::vector<size_t> targets = {static_cast<size_t>(rank)};
    diaspora::TopicHandle topic = driver.openTopic("handshake_d_s");
    diaspora::Consumer hs_consumer = topic.consumer("hs_c", batchSize,
                                                    threadCount, targets);
    std::optional<diaspora::Event> event;
    while (!event) {
        event = hs_consumer.pull().wait(-1);
    }
    info = event->metadata().json();
}

diaspora::Producer FakeDiasporaStream::getProducer(const std::string& topic_name,
                                                   const std::string& producer_name) {
    auto topic = driver.openTopic(topic_name);
    return topic.producer(producer_name, batchSize, threadCount, ordering);
}

diaspora::Consumer FakeDiasporaStream::getConsumer(const std::string& topic_name,
                                                   const std::string& consumer_name,
                                                   std::vector<size_t> targets) {
    diaspora::TopicHandle topic = driver.openTopic(topic_name);
    return topic.consumer(consumer_name, threadCount, batchSize,
                          data_selector, data_allocator, targets);
}

bool FakeDiasporaStream::pullStep(int step, diaspora::Consumer& consumer) {
    for (int i = 0; i < step; ++i) {
        recordTs("PULL_WAIT_START topic=dist_sirt");
        std::optional<diaspora::Event> event;
        while (!event) {
            event = consumer.pull().wait(-1);
        }
        size_t ev_data_size = 0;
        for (auto& seg : event->data().segments()) ev_data_size += seg.size;
        recordTs(fmt::format("PULL_WAIT_END topic=dist_sirt,event_id={},data_size={}",
                             event->id(), ev_data_size));
        if (event->metadata().json()["Type"].get<std::string>() == "FIN") {
            return true;
        }
        ++counter;
    }
    return false;
}

void FakeDiasporaStream::publishZeros(json& meta, size_t n_floats,
                                      diaspora::Producer& producer,
                                      std::optional<size_t> partition) {
    diaspora::Metadata metadata{meta};
    float* buf = new float[n_floats];
    std::memset(buf, 0, n_floats * sizeof(float));
    auto free_cb = [](diaspora::DataView::UserContext ctx) {
        delete[] static_cast<float*>(ctx);
    };
    auto data_m = diaspora::DataView(static_cast<void*>(buf),
                                     n_floats * sizeof(float), buf, free_cb);
    recordTs(fmt::format("PUSH_START topic=sirt_den,data_size={}",
                         n_floats * sizeof(float)));
    producer.push(metadata, data_m, partition);
    recordTs("PUSH_END topic=sirt_den");
}
