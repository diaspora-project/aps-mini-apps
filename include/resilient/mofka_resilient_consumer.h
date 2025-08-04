#ifndef MOFKA_RESILIENT_CONSUMER_H
#define MOFKA_RESILIENT_CONSUMER_H

#include "resilient_consumer.h"
#include <mofka/Consumer.hpp>
#include <mofka/TopicHandle.hpp>
#include <mofka/MofkaDriver.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace resilient {

class MofkaResilientConsumer : public ResilientConsumer {
private:
    mofka::Consumer consumer;

public:
    MofkaResilientConsumer(mofka::TopicHandle topic, const std::string &consumer_name, size_t thread_count, size_t batch_size, const std::vector<size_t> &targets);
    ~MofkaResilientConsumer();

    ResilientFuture<ResilientEvent> pull() override;
};

} // namespace resilient

#endif // MOFKA_RESILIENT_CONSUMER_H