#ifndef EVENT_STATUS_H
#define EVENT_STATUS_H

#include "resilient_event.h"
#include <atomic>
#include <string>

namespace resilient {

enum class Status {
    Pending,    // Event is created but not yet transmitted
    Transmitted, // Event has been transmitted successfully
    Received,   // Event has been received by the consumer
    Acknowledged, // Event has been acknowledged by the consumer
    Failed      // Event transmission failed
};

class EventStatus {
private:
    ResilientEvent event; // The associated ResilientEvent
    std::atomic<Status> status; // Atomic status to track the event state
    std::string error_message; // Optional error message for failed events

public:
    // Constructor
    EventStatus(const ResilientEvent& event, Status initial_status = Status::Pending);

    // Getters
    inline ResilientEvent getEvent() const { return event; }
    inline Status getStatus() const { return status.load(); }
    inline std::string getErrorMessage() const { return error_message; }

    // Setters
    inline void setStatus(Status new_status) { status.store(new_status); }
    inline void setErrorMessage(const std::string& message) { error_message = message; }

    // Utility methods
    bool isPending() const;
    bool isTransmitted() const;
    bool isFailed() const;
};

} // namespace resilient

#endif // EVENT_STATUS_H