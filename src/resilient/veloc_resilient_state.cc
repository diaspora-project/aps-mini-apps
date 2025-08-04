#include "resilient/veloc_resilient_state.h"
#include <stdexcept>

namespace resilient {

VelocResilientState::VelocResilientState(int task_index, const std::string &ckpt_name, const std::string &ckpt_config)
    : ckpt_client(nullptr), ckpt_name(ckpt_name), task_index(task_index), ckpt_config(ckpt_config), progress(0) {
    // Initialize the VeloC client
    ckpt_client = veloc::get_client(static_cast<unsigned int>(task_index), ckpt_config);
    if (!ckpt_client) {
        throw std::runtime_error("Failed to initialize VeloC client");
    }
}

VelocResilientState::~VelocResilientState() {
    // Clean up resources
    if (ckpt_client) {
        delete ckpt_client;
    }
}

int get_or_create_id(const std::string &key) {
    // check if key is in key_to_id map
    int id = -1;
    auto it = key_to_id.find(key);
    if (it == key_to_id.end()) {
        // If not found, assign a new id
        id = next_id;
        key_to_id[key] = id;
        next_id++;
    }else{
        // If found, use the existing id
        id = it->second;
    }
    return id;
}

bool VelocResilientState::register_state(std::string key, void *ptr, size_t count, size_t base_size) {
    return ckpt_client->mem_protect(get_or_create_id(key), ptr, count, base_size);
}

bool VelocResilientState::(int id, const serializer_t &s, const deserializer_t &d) {
    return ckpt_client->mem_protect(get_or_create_id(key), s, d);
}

bool VelocResilientState::unregister_state(std::string key) {
    auto it = key_to_id.find(key);
    if (it == key_to_id.end()) {
        // std::cerr << "Key not found: " << key << std::endl;
        return false; // Key not found
    }else{
        int id = it->second;
        key_to_id.erase(it);
        return ckpt_client->mem_unprotect(id);
    }
    
}

bool VelocResilientState::mem_clear() {
    ckpt_client->mem_clear();
    return true;
}

bool VelocResilientState::checkpoint(int version) {
    std::cout << "Checkpointing version: " << version << std::endl;
    bool success = ckpt_client->checkpoint(ckpt_name, version);
    if (success) {
        version++;
    }
    return success;
}

bool VelocResilientState::restore(int version) {
    if (version <= 0) { 
        version = ckpt_client->restart_test(ckpt_name, 0, task_index);
    }
    if (version > 0) {
        std::cout << "Restoring from checkpoint version: " << version << std::endl;
        if (!ckpt_client->restart(ckpt_name, version)) {
            throw std::runtime_error("Failed to restore from checkpoint");
        }
        return true;
    } else {
        std::cout << "No checkpoint found. Starting from scratch." << std::endl;
        return false;
    }
}

} // namespace resilient