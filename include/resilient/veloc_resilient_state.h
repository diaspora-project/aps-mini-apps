#ifndef VELOC_RESILIENT_STATE_H
#define VELOC_RESILIENT_STATE_H

#include "ckpt_service.h"
#include <veloc.hpp>
#include <string>
#include <iostream>

namespace resilient {

class VelocResilientState : public ResilientState {

protected:
    typedef std::function<void (std::ostream &)> serializer_t;
    typedef std::function<bool (std::istream &)> deserializer_t;

private:
    veloc::client_t *ckpt_client;
    std::string ckpt_name;
    int task_index;
    std::string ckpt_config;
    std::map<std::string, int> key_to_id;
    int next_id = 0;

    int get_or_create_id(const std::string &key);

public:
    VelocCkptService(int task_index, const std::string &ckpt_name, const std::string &ckpt_config);
    ~VelocCkptService() override;

    void register_state(std::string key, void *ptr, size_t count, size_t base_size) override;
    bool register_state(std::string key, const serializer_t &s, const deserializer_t &d) override;
    bool unregister_state(std::string key) override;
    

    bool checkpoint(int version) override;
    bool restore(int version) override;
};

} // namespace resilient

#endif // VELOC_CKPT_SERVICE_H