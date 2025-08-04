#include "resilient/event_status.h"

namespace resilient {

EventStatus::EventStatus(const ResilientEvent& event, Status initial_status)
    : event(event), status(initial_status), error_message("") {}

bool EventStatus::isPending() const {
    return status.load() == Status::Pending;
}

bool EventStatus::isTransmitted() const {
    return status.load() == Status::Transmitted;
}

bool EventStatus::isFailed() const {
    return status.load() == Status::Failed;
}

} // namespace resilient