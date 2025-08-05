#ifndef SIRT_RETCON_ENGINE_H
#define SIRT_RETCON_ENGINE_H

#include "trace_runtime_config.h"
#include <atomic>

class RetConTask {

  private:
    TraceRuntimeConfig config;
    int task_id = 0;
    int num_tasks = 1;
    // stop flag as atomic variable to handle graceful shutdown
    std::atomic<bool> stop_flag{false};
    std::function<void()> on_stop_callback = nullptr;

  public:
    RetConTask(int task_id, int num_tasks, int argc, char **argv)
    : config(argc, argv), task_id(task_id), num_tasks(num_tasks) {}

    RetConTask(int task_id, int num_tasks, const TraceRuntimeConfig &cfg)
    : config(cfg), task_id(task_id), num_tasks(num_tasks) {}
    
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
