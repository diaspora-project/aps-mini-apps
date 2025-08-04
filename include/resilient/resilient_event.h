#ifndef RESILIENT_EVENT_H
#define RESILIENT_EVENT_H

namespace resilient {

class ResilientEvent {
public:
    // Constructor
    ResilientEvent();

    // Destructor
    ~ResilientEvent();

    // Copy constructor
    ResilientEvent(const ResilientEvent& other);

    // Move constructor
    ResilientEvent(ResilientEvent&& other) noexcept;

    // Copy assignment operator
    ResilientEvent& operator=(const ResilientEvent& other);

    // Move assignment operator
    ResilientEvent& operator=(ResilientEvent&& other) noexcept;

    // Constructor with parameters
    ResilientEvent(const json& metadata, void* data, int size);

    // Confirm the event has been acknowledged by the consumer
    void acknowledge() = 0;

    // Getters
    inline int getId() const { return id; }
    inline json getMetadata() const { return metadata; }
    inline void* getData() const { return data; }
    inline int getSize() const { return size; }

    // Setters
    inline void setId(int newId) { id = newId; }
    inline void setMetadata(const json& metadata) { this->metadata = metadata; }
    inline void setData(void* data) { this->data = data; }
    inline void setSize(int size) { this->size = size; }
    

private:
    // Private member variables
    int id;        // Unique identifier for the event
    json metadata; // Metadata in JSON format
    void* data;    // Pointer to the event data
    int size;      // Size of the event data

};

} // namespace resilient

#endif // RESILIENT_EVENT_H