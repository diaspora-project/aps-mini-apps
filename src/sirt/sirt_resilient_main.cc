#include <iomanip>
#include "mpi.h"
#include "trace_h5io.h"
#include "data_region_base.h"
#include "tclap/CmdLine.h"
#include "disp_comm_mpi.h"
#include "disp_engine_reduction.h"
#include "sirt.h" // Include SIRTReconSpace
#include <cassert>
#include <time.h>
#include <string>
#include <fstream>
#include <iostream>
#include "resilient/mofka_resilient_consumer.h"
#include "resilient/mofka_resilient_producer.h"
#include "trace_data.h"
#include <vector>
#include <unistd.h>
#include <charconv>
#include <csignal>
#include "sirt/trace_runtime_config.h"
#include <mofka_stream.h>


volatile std::sig_atomic_t sigterm_captured = 0;
void handle_sigterm(int signum) {
    std::cerr << "Received SIGTERM, stoping reconstruction..." << std::endl;
    sigterm_captured = signum;
}

int main(int argc, char **argv) {
    std::signal(SIGTERM, handle_sigterm);

    /* Initiate middleware's communication layer */
    TraceRuntimeConfig config(argc, argv);
    MofkaStream ms = MofkaStream{ config.group_file,
                                config.batchsize,
                                static_cast<uint32_t>(config.window_len),
                                config.task_index,
                                config.num_tasks,
                                0}; // Add the missing progress argument

    ms.handshake(config.task_index, config.num_tasks);

    std::cout << "Handshake completed" << std::endl;

    // Prepare action channel between consumer and producer
    std::string consuming_topic = "dist_sirt_action";
    std::string producing_topic = "sirt_dist_action";
    std::vector<size_t> targets = {static_cast<size_t>(config.worker_index)};

    mofka::Producer producer = ms.getProducer(producing_topic, "sirt");
    mofka::Consumer consumer = ms.getConsumer(consuming_topic, "sirt", targets);
    /* Get metadata structure */
    json channel_metadata = ms.getInfo();
    auto n_blocks = tmetadata["n_sinograms"].get<int64_t>();
    auto num_cols = tmetadata["n_rays_per_proj_row"].get<int64_t>();

    std::map<int, RetconTask> running_tasks;

    bool running = true;
    while (true) {
        // listen for action messages and initialize/terminate assigned reconstruction tasks.
        auto event = consumer.pull().wait();
        auto json_metadata = event.metadata().json();
        std::string event_type = json_metadata["Type"].get<std::string>();
        switch (event_type) {
        case "END_TASK":
            int task_id = json_metadata["task_id"].get<int>();
            if (running_tasks.find(task_id) != running_tasks.end()) {
                running_tasks[task_id].stop([&] {
                    std::cout << "[Worker-" << config.task_id << "] Task " << task_id << " completed. Notifying the producer the completion" << std::endl;
                    json end_md = {
                        {"Type", "COMPLETE"},
                        {"task_id", task_id},
                        {"iteration_stream", std::to_string(running_tasks[task_id].getNumPasses())}
                    };
                    producer.push(end_md).wait();
                });
                running_tasks.erase(task_id);
            }
            break;
        case "START_TASK":
            int task_id = json_metadata["task_id"].get<int>();
            RetConTask new_task(task_id, config.num_tasks, argc, argv);
            running_tasks[task_id] = std::move(new_task);
            running_tasks[task_id].run();
            break;
        case "SHUTDOWN":
            std::cout << "[Worker-" << config.task_id << "] End of stream. Exiting..." << std::endl;
            running = false;
            break;
        default:
            std::cerr << "Unknown event type: " << event_type << std::endl;
            break;
        }
    }

    // Stop running tasks
    for (auto& [task_id, task] : running_tasks) {
        task.stop();
    }
}