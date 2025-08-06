#ifndef SIRT_RETCON_ENGINE_H
#define SIRT_RETCON_ENGINE_H

#include "trace_runtime_config.h"
#include <atomic>

class ReconTask {

  private:
    TraceRuntimeConfig config;
    int task_id = 0;
    // stop flag as atomic variable to handle graceful shutdown
    std::atomic<bool> stop_flag{false};
    std::function<void()> on_stop_callback = nullptr;

    
  public:
    ReconTask(int task_id, int argc, char **argv)
    : config(argc, argv), task_id(task_id) {}

    ReconTask(int task_id, const TraceRuntimeConfig &cfg)
    : config(cfg), task_id(task_id) {}

    ReconTask() : config(0, nullptr), task_id(0) {}
    
    ReconTask& operator=(ReconTask&& other) noexcept {
      if (this != &other) {
        config = std::move(other.config);
        task_id = other.task_id;
        stop_flag.store(other.stop_flag.load());
        if (other.on_stop_callback) {
          on_stop_callback = std::move(other.on_stop_callback);
        } else {
          on_stop_callback = nullptr;
        }
        on_stop_callback = std::move(other.on_stop_callback);
      }
      return *this;
    }
    ReconTask(const ReconTask& other) = delete; // Disable copy constructor

    /**
     * Run the RetCon task.
     * @return 0 on success, non-zero on failure.
     */
    int run();

    /**
     * Stop the RetCon task with callback as a parameter that will be called when the stop flag is set.
     */
    void stop(std::function<void()> callback = nullptr);
};

#endif // SIRT_RETCON_ENGINE_H
