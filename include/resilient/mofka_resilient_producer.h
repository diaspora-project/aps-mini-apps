#ifndef MOFKA_RESILIENT_PRODUCER_H
#define MOFKA_RESILIENT_PRODUCER_H

#include "resilient_producer.h"
#include "event_status.h"
#include <mofka/Producer.hpp>
#include <mofka/TopicHandle.hpp>
#include <mofka/MofkaDriver.hpp>
#include <nlohmann/json.hpp>
#include <vector>
#include <string>
#include <memory>

using json = nlohmann::json;

namespace resilient {

class MofkaResilientProducer : public ResilientProducer {
private:
    mofka::Producer producer;
    size_t batch_size;
    size_t batch_count;
    std::vector<float*> buffer;

public:
    MofkaResilientProducer(mofka::TopicHandle topic, const std::string &producer_name, size_t batch_size);
    ~MofkaResilientProducer();

    ResilientFuture<ResilientStatus> push(ResilientEvent event) override;

    void flush();
};

} // namespace resilient

#endif // MOFKA_RESILIENT_PRODUCER_H