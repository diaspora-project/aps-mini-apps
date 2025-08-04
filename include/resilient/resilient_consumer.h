#ifndef RESILIENT_PRODUCER_H
#define RESILIENT_PRODUCER_H

namespace resilient {

class ResilientProducer {
public:
	ResilientProducer() = default;
	virtual ~ResilientProducer() = default;

	ResilientFuture<ResilientEvent> pull() = 0;
    
};

} // namespace resilient

#endif // RESILIENT_PRODUCER_H