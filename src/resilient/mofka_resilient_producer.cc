#include "resilient/mofka_resilient_producer.h"
#include <stdexcept>
#include <cstring>
#include <iostream>

namespace resilient {

MofkaResilientProducer::MofkaResilientProducer(mofka::TopicHandle topic, const std::string &producer_name, size_t batch_size)
    : producer(topic.producer(producer_name, mofka::BatchSize{batch_size}, mofka::ThreadCount{1}, mofka::Ordering::Strict)),
      batch_size(batch_size),
      batch_count(0) {}

MofkaResilientProducer::~MofkaResilientProducer() {
    flush();
    for (float* ptr : buffer) {
        if (ptr != nullptr) {
            delete[] ptr;
        }
    }
    buffer.clear();
}

ResilientFuture<EventStatus> MofkaResilientProducer::push(ResilientEvent event) {
    auto promise = std::make_shared<std::promise<EventStatus>>();
    auto future = promise->get_future();

    std::thread([this, event, promise]() {
        try {
            json metadata_json = event.get_metadata();
            mofka::Metadata metadata{metadata_json};

            size_t data_size = event.get_data_size();
            float* data_copy = new float[data_size];
            std::memcpy(data_copy, event.get_data_ptr(), data_size * sizeof(float));
            buffer.push_back(data_copy);

            mofka::Data data_m = mofka::Data(buffer.back(), data_size * sizeof(float));
            auto producer_future = producer.push(metadata, data_m);
            batch_count++;

            if (batch_count == batch_size) {
                flush();
            }

            promise->set_value(producer_future.get());
        } catch (const std::exception& e) {
            promise->set_exception(std::make_exception_ptr(e));
        }
    }).detach();

    return ResilientFuture<EventStatus>(std::move(future));
}

void MofkaResilientProducer::flush() {
    producer.flush();
    for (float* ptr : buffer) {
        if (ptr != nullptr) {
            delete[] ptr;
        }
    }
    buffer.clear();
    batch_count = 0;
}

} // namespace resilient