#include "resilient/mofka_resilient_consumer.h"
#include <stdexcept>
#include <iostream>

namespace resilient {

MofkaResilientConsumer::MofkaResilientConsumer(mofka::TopicHandle topic, const std::string &consumer_name, size_t thread_count, size_t batch_size, const std::vector<size_t> &targets)
    : consumer(topic.consumer(consumer_name, mofka::ThreadCount{thread_count}, mofka::BatchSize{batch_size}, targets)) {}

MofkaResilientConsumer::~MofkaResilientConsumer() {
    // Cleanup if necessary
}

ResilientFuture<ResilientEvent> MofkaResilientConsumer::pull() {
    ResilientFuture<ResilientEvent> future;

    // Simulate asynchronous event pulling
    std::thread([this, future]() mutable {
        try {
            auto event = consumer.pull().wait();
            ResilientEvent resilient_event(event.metadata().json(), event.data().segments()[0].ptr, event.data().segments()[0].size);
            future.set_result(resilient_event);
        } catch (...) {
            future.set_exception(std::current_exception());
        }
    }).detach();

    return future;
}

} // namespace resilient