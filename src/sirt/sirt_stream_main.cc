#include <iomanip>
#include <cassert>
#include <time.h>
#include <string>
#include <iostream>
#include <vector>
#include <unistd.h>
#include <csignal>
#include "sirt/trace_runtime_config.h"
#include "sirt/recon_engine.h"
#include <mofka_stream.h>
#include <unordered_map>
#include <thread>

std::unordered_map<int, ReconTask> running_tasks;
std::unordered_map<int, int> task_progresses;
std::unordered_map<int, std::thread> running_threads;
std::vector<std::thread> stopped_threads;

std::mutex pending_task_ids_mutex;
std::vector<int> pending_task_ids;

void cleanup() {
    for (auto& [task_id, thread] : running_threads) {
        thread.join();
        std::cout << "Task " << task_id << " stopped." << std::endl;
    }
    for (auto& thread : stopped_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    std::cout << "All tasks stopped." << std::endl;
}

volatile std::sig_atomic_t sigterm_captured = 0;
void handle_sigterm(int signum) {
    std::cerr << "Received SIGTERM, stoping reconstruction..." << std::endl;
    sigterm_captured = signum;
    // Kill running tasks
    std::cerr << "Cleanning up by killing running tasks..." << std::endl;
    for (auto& [task_id, task] : running_tasks) {
        task.kill(sigterm_captured);
    }
    // give it a short grace period
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // if still not exiting (e.g., SST Open stuck), force exit
    std::_Exit(1);   // or _exit(1)
}

void snapshot_running_tasks(int worker_id, std::string& outputpath, const std::vector<int>* tasks, int action_seq = 0) {
    std::cout << "[Worker-" << worker_id << "] snapshot of running task ids at " << outputpath << std::endl;
    json md;
    std::vector<int> task_ids;
    if (tasks != nullptr) {
        for (const auto& task_id : *tasks) {
            task_ids.push_back(task_id);
        }
    }else{
        for (const auto& [task_id, task] : running_tasks) {
            task_ids.push_back(task_id);
        }
    }
    md["running_task_ids"] = task_ids;
    md["action_seq"] = action_seq;
    std::ofstream ofs(outputpath);
    ofs << md.dump(4);
    ofs.close();
    std::cout << "[Worker-" << worker_id << "] Snapshot for " << md.dump() << " saved." << std::endl;
}

std::vector<int> load_running_tasks(int worker_id, std::string& inputpath, int &action_seq) {
    std::cout << "Loading snapshot of running task ids from " << inputpath << std::endl;
    std::vector<int> events;
    if (std::filesystem::exists(inputpath)) {
        std::ifstream ifs(inputpath);
        json md;
        ifs >> md;
        ifs.close();
        action_seq = md.value("action_seq", 0);
        events = md["running_task_ids"].get<std::vector<int>>();
    }else{
        std::cout << "No snapshot file found at " << inputpath << ". Starting fresh." << std::endl;
    }
    return events;
}

void start_task(/* mofka::Producer& producer, */ const json& metadata, const TraceRuntimeConfig& config,
                mofka::MofkaDriver& driver, std::mutex& ckpt_mutex,
                int argc, char** argv) {
    int task_id = metadata["task_id"].get<int>();
    if (running_tasks.find(task_id) != running_tasks.end()) {
        std::cerr << "[Worker-" << config.worker_id << "] Task " << task_id << " is already running. Ignoring START_TASK command." << std::endl;
    }else{
        std::cout << "[Worker-" << config.worker_id << "] Starting Task " << task_id << std::endl;
        running_tasks.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(task_id),
            std::forward_as_tuple(task_id, driver, &ckpt_mutex, argc, argv)
        );
        running_threads.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(task_id),
            std::forward_as_tuple([&, task_id] {
                running_tasks.at(task_id).run();
            })
        );
        task_progresses[task_id] = 0;
    }
}

int main(int argc, char **argv) {
    std::signal(SIGTERM, handle_sigterm);

    /* Initiate middleware's communication layer */
    std::cout << "Reading worker configuration..." << std::endl;
    TraceRuntimeConfig config(argc, argv);
    
    // Send worker information to dist
    std::cout << "[Worker-" << config.worker_id << "] Handshaking: sending worker information to dist..." << std::endl;
    std::string topic_name = "handshake_s_d";
    mofka::MofkaDriver driver(config.group_file, true);
    mofka::TopicHandle hs_topic = driver.openTopic(topic_name);
    mofka::Producer hs_producer = hs_topic.producer(
      "hs_p",
      config.batchsize,
      config.thread_count,
      mofka::Ordering::Strict
    );

    json md = {{"num_workers", config.num_workers},
             {"worker_index", config.worker_index}};
    mofka::Metadata metadata{md};
    auto future = hs_producer.push(metadata);
    future.wait();

    std::cout << "[Worker-" << config.worker_id << "] Handshaking completed" << std::endl;

    // Prepare action channel between consumer and producer
    mofka::TopicHandle consuming_topic = driver.openTopic("dist_sirt_action");
    mofka::TopicHandle producing_topic = driver.openTopic("sirt_dist_action");
    mofka::Producer producer = producing_topic.producer("sirt", 1, 1, mofka::Ordering::Strict);
    // std::vector<size_t> targets = {static_cast<size_t>(config.worker_index)};
    std::vector<size_t> targets = {static_cast<size_t>(0)};
    mofka::Consumer consumer = consuming_topic.consumer("sirt", 1, 1, targets);
    // mofka::Consumer consumer = consuming_topic.consumer(
    //     "sirt",
    //     1, // thread count
    //     1, // batch size
    //     [](const mofka::Metadata& metadata, const mofka::DataDescriptor& descriptor) {
    //         (void)metadata;
    //         return descriptor;
    //     },
    //     [](const mofka::Metadata& metadata, const mofka::DataDescriptor& descriptor) {
    //         (void)metadata;
    //         return mofka::Data{new float[descriptor.size()], descriptor.size()};
    //     },
    //     targets
    // );

    std::cout << "[Worker-" << config.worker_id << "] Listening for exchange information from DIST..." << std::endl;

    std::mutex ckpt_mutex;

    bool running = true;

    std::string task_assignment_path = config.logdir + "/sirt-" + config.worker_id + ".json";
    int action_seq = -1;
    auto init_events = load_running_tasks(config.worker_index, task_assignment_path, action_seq);
    if (!init_events.empty()) {
        std::cout << "[Worker-" << config.worker_id << "] Restarting from snapshot with " << init_events.size() << " tasks." << std::endl;
        for (const auto& task_id : init_events) {
            start_task(json{{"task_id", task_id}}, config, driver, ckpt_mutex, argc, argv);
        }
    }

    while (running) {
        // listen for action messages and initialize/terminate assigned reconstruction tasks.
        // auto event = consumer.pull().wait();

        // Check if reconstruction task made, progress
        // If so, notify DIST
        for (auto& [task_id, task] : running_tasks) {
            int progress = task.getCheckpointedProgress();
            if (progress > task_progresses[task_id]) {
                json progress_md = {
                    {"Type", "PROGRESS"},
                    {"task_id", task_id},
                    {"progress", progress}
                };
                std::cout << "[Worker-" << config.worker_id << "] Report progress for Task " << task_id << ": Progress: " << task_progresses[task_id] << " --> " << progress << std::endl;
                task_progresses[task_id] = progress;
                producer.push(progress_md);
            }
        }
        producer.flush();

        auto future_event = consumer.pull();
        while (!future_event.completed()) {
            // sleep for 1 ms to avoid busy waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(1));

            // Also check progress if needed while waiting
            for (auto& [task_id, task] : running_tasks) {
                int progress = task.getCheckpointedProgress();
                if (progress > task_progresses[task_id]) {
                    json progress_md = {
                        {"Type", "PROGRESS"},
                        {"task_id", task_id},
                        {"progress", progress}
                    };
                    std::cout << "[Worker-" << config.worker_id << "] Report progress for Task " << task_id << ": Progress: " << task_progresses[task_id] << " --> " << progress << std::endl;
                    task_progresses[task_id] = progress;
                    producer.push(progress_md);
                }
            }
            producer.flush();

            while (!pending_task_ids.empty()) {
                std::lock_guard<std::mutex> lock(pending_task_ids_mutex);
                if (pending_task_ids.empty()) {
                    break;
                }
                int task_id = pending_task_ids.front();
                ReconTask& task = running_tasks.at(task_id);
                if (!task.isMSCompleted()) {
                    break;
                }
                pending_task_ids.erase(pending_task_ids.begin());
                
                std::cout << "[Worker-" << config.worker_id << "] Cleaning up task " << task_id << std::endl;
                stopped_threads.push_back(std::move(running_threads[task_id]));
                std::cout << "[Worker-" << config.worker_id << "] Joined stopped thread for Task " << task_id << std::endl;
                running_tasks.erase(task_id);
                std::cout << "[Worker-" << config.worker_id << "] Erased running task for Task " << task_id << std::endl;
                running_threads.erase(task_id);
                std::cout << "[Worker-" << config.worker_id << "] Erased running thread for Task " << task_id << std::endl;
                task_progresses.erase(task_id);
                std::cout << "[Worker-" << config.worker_id << "] Task " << task_id << " stopped." << std::endl;
            }

            if (sigterm_captured) {
                running = false;
                break;
            }
        }
        if (!running) {
            break;
        }
        auto event = future_event.wait();

        auto json_metadata = event.metadata().json();
        std::cout << "[Worker-" << config.worker_id << "] Receive event from DIST: for " << json_metadata.dump() << std::endl;
        if (json_metadata["worker_id"].get<int>() != config.worker_index) {
            std::cout << "[Worker-" << config.worker_id << "] Event not meant for this worker. Ignoring." << std::endl;
            continue; // Ignore messages not meant for this worker
        }
        int as = json_metadata.value("action_seq", action_seq);
        if (as <= action_seq) {
            std::cout << "[Worker-" << config.worker_id << "] Already processed action_seq " << as << " (current: " << action_seq << "). Ignoring." << std::endl;
            continue;
        }
        action_seq = as;
        std::string event_type = json_metadata["Type"].get<std::string>();
        if (event_type == "END_TASK") {
            int task_id = json_metadata["task_id"].get<int>();
            if (running_tasks.find(task_id) != running_tasks.end()) {
                std::cout << "[Worker-" << config.worker_id << "] Stoping [Task-" << task_id << "]..." << std::endl;
                // Quickly snapshot the state to prevent immediate failure
                std::vector<int> current_running_task_ids;
                for (const auto& [tid, task] : running_tasks) {
                    if (tid != task_id) {
                        current_running_task_ids.push_back(tid);
                    }
                }
                snapshot_running_tasks(config.worker_index, task_assignment_path, &current_running_task_ids, action_seq);
                std::cout << "[Worker-" << config.worker_id << "] Snapshot of running tasks saved at: " << task_assignment_path << std::endl;
                running_tasks[task_id].stop([task_id, &producer, &config]() {
                    std::cout << "[Task-" << task_id << "] Stopped. Notifying the producer the completion" << std::endl;
                    json end_md = {
                        {"Type", "COMPLETE"},
                        {"worker_id", config.worker_id},
                        {"task_id", task_id}
                    };
                    producer.push(end_md);
                    std::lock_guard<std::mutex> lock(pending_task_ids_mutex);
                    pending_task_ids.push_back(task_id);

                });
            }else{
                std::cout << "[Worker-" << config.worker_id << "] Received END_TASK for Task " << task_id << " which is not running. Ignoring." << std::endl;
            }
        }else if (event_type == "START_TASK") {
            start_task(json_metadata, config, driver, ckpt_mutex, argc, argv);
            snapshot_running_tasks(config.worker_index, task_assignment_path, nullptr, action_seq);
        }else if (event_type == "SHUTDOWN") {
            std::cout << "[Worker-" << config.worker_id << "] End of stream. Exiting..." << std::endl;
            running = false;
        }else{
            std::cerr << "Unknown event type: " << event_type << std::endl;
        }
        std::cout << "[Worker-" << config.worker_id << "] Acknowledging the event " << json_metadata.dump() << std::endl;
        event.acknowledge();
    }
    
    cleanup();
    std::cerr << "[Worker-" << config.worker_id << "] Exiting..." << std::endl;
    return sigterm_captured;

}