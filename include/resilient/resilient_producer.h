#ifndef RESILIENT_PRODUCER_H
#define RESILIENT_PRODUCER_H

namespace resilient {

class ResilientProducer {
public:
	ResilientProducer() = default;
	virtual ~ResilientProducer() = default;

	ResilientFuture<EventStatus> push(ResilientEvent event) = 0;
    
};

} // namespace resilient

#endif // RESILIENT_PRODUCER_H