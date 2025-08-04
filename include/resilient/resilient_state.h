#ifndef RESILIENT_STATE_H
#define RESILIENT_STATE_H

namespace resilient {

class ResilientState {
public:

    ResilientState() = default;
    virtual ~ResilientState() = default;
    
    // Public methods
    
    /* Checkpoint the current state */
    virtual bool checkpoint(int version)=0;

    /* Restore the state from a given version. If 
     * the version is 0, restore the most recent checkpoint */
    virtual bool restore(int version)=0;

    virtual void register_state(std::string key, void *ptr, size_t count, size_t base_size)=0;
    virtual bool register_state(std::string key, const serializer_t &s, const deserializer_t &d)=0;

    virtual bool unregister_state(std::string key)=0;
};

} // namespace resilient

#endif // RESILIENT_STATE_H