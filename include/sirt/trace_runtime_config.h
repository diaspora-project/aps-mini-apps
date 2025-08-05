#include <iomanip>
#include "trace_h5io.h"
#include "tclap/CmdLine.h"
#include <cassert>
#include <time.h>
#include <string>
#include <fstream>
#include <iostream>
#include "trace_data.h"
#include <vector>
#include <unistd.h>
#include <charconv>
#include <csignal>

class TraceRuntimeConfig {
    public:
      std::string kReconOutputPath;
      std::string kReconDatasetPath;
      std::string kReconOutputDir;
      std::string protocol;
      std::string group_file;
      size_t batchsize;
      int thread_count;
      int window_len;
      int window_step;
      int window_iter;
      int write_freq = 0;
      int center;
      std::string dest_host;
      int dest_port;
      std::string pub_addr;
      int pub_freq = 0;
      std::string worker_id;
      int worker_index;
      int num_workers;
      int num_tasks;
      int num_passes;
      int ckpt_freq = 1;
      std::string ckpt_config;
      std::string ckpt_name;
      std::string logdir = ".";
  
      TraceRuntimeConfig(int argc, char **argv){
        try
        {
          TCLAP::CmdLine cmd("SIRT Iterative Image Reconstruction", ' ', "0.01");
          TCLAP::ValueArg<std::string> argWorkerId(
            "", "worker-id", "The Worker Id", false, "0", "string");
          TCLAP::ValueArg<int> argNumWorkers(
            "", "num-workers", "Number of Reconstruction Workers", false, 1, "int");
          TCLAP::ValueArg<int> argNumTasks(
            "", "num-tasks", "Number of Reconstruction Tasks", false, 1, "int");
          TCLAP::ValueArg<std::string> argMofkaProtocol(
            "", "protocol", "Mofka protocol", false, "na+sm", "string");
          TCLAP::ValueArg<std::string> argGroupFile(
            "", "group-file", "Mofka group file", false, "mofka.json", "string");
          TCLAP::ValueArg<size_t> argBatchSize(
            "", "batchsize", "Mofka batchsize", false, 1, "size_t");
          TCLAP::ValueArg<std::string> argReconOutputPath(
            "o", "reconOutputPath", "Output file path for reconstructed image (hdf5)",
            false, "./output.h5", "string");
          TCLAP::ValueArg<std::string> argReconOutputDir(
            "", "recon-output-dir", "Output directory for the streaming outputs",
            false, ".", "string");
          TCLAP::ValueArg<std::string> argReconDatasetPath(
            "r", "reconDatasetPath", "Reconstruction dataset path in hdf5 file",
            false, "/data", "string");
          TCLAP::ValueArg<float> argPubFreq(
            "", "pub-freq", "Publish frequency", false, 10000, "int");
          TCLAP::ValueArg<float> argCenter(
            "c", "center", "Center value", false, 0., "float");
          TCLAP::ValueArg<int> argThreadCount(
            "t", "thread", "Number of threads per process", false, 1, "int");
          TCLAP::ValueArg<float> argWriteFreq(
            "", "write-freq", "Write frequency", false, 10000, "int");
          TCLAP::ValueArg<float> argWindowLen(
            "", "window-length", "Number of projections that will be stored in the window",
            false, 32, "int");
          TCLAP::ValueArg<float> argWindowStep(
            "", "window-step", "Number of projections that will be received in each request",
            false, 1, "int");
          TCLAP::ValueArg<float> argWindowIter(
            "", "window-iter", "Number of iterations on received window",
            false, 1, "int");
          TCLAP::ValueArg<int> argNumPasses(
            "", "num-passes", "Number of passes on data streams",
            // false, 201, "int");
            false, 4001, "int");
            // false, 21, "int");
            // false, 41, "int");
          TCLAP::ValueArg<std::string> argLogDir(
            "", "logdir", "Log directory", false, ".", "string");
          TCLAP::ValueArg<int> argCkptFreq(
            "", "ckpt-freq", "Checkpoint frequency", false, 1, "int");
          TCLAP::ValueArg<std::string> argCkptConfig(
            "", "ckpt-config", "Checkpoint Configuration (VeloC)", false, "veloc.cfg", "string");
          TCLAP::ValueArg<std::string> argCkptName(
            "", "ckpt-name", "Checkpoint Name (VeloC)", false, "sirt-ckpt", "string");
  
          cmd.add(argWorkerId);
          cmd.add(argNumWorkers);
          cmd.add(argNumTasks);
  
          cmd.add(argMofkaProtocol);
          cmd.add(argGroupFile);
          cmd.add(argBatchSize);
          cmd.add(argReconOutputPath);
          cmd.add(argReconOutputDir);
          cmd.add(argReconDatasetPath);
  
          cmd.add(argPubFreq);
  
          cmd.add(argCenter);
          cmd.add(argThreadCount);
          cmd.add(argWriteFreq);
          cmd.add(argWindowLen);
          cmd.add(argWindowStep);
          cmd.add(argWindowIter);
          cmd.add(argNumPasses);
  
          cmd.add(argLogDir);
  
          cmd.add(argCkptFreq);
          cmd.add(argCkptConfig);
          cmd.add(argCkptName);
  
          cmd.parse(argc, argv);
  
          worker_id = argWorkerId.getValue();
          std::from_chars(worker_id.data(), worker_id.data() + worker_id.size(), worker_index);
          num_workers = argNumWorkers.getValue();
          num_tasks = argNumTasks.getValue();
          kReconOutputPath = argReconOutputPath.getValue();
          kReconOutputDir = argReconOutputDir.getValue();
          kReconDatasetPath = argReconDatasetPath.getValue();
          center = argCenter.getValue();
          thread_count = argThreadCount.getValue();
          write_freq = argWriteFreq.getValue();
          window_len = argWindowLen.getValue();
          window_step = argWindowStep.getValue();
          window_iter = argWindowIter.getValue();
          num_passes = argNumPasses.getValue();
  
          protocol = argMofkaProtocol.getValue();
          batchsize = argBatchSize.getValue();
          group_file = argGroupFile.getValue();
          ckpt_freq = argCkptFreq.getValue();
          ckpt_config = argCkptConfig.getValue();
          ckpt_name = argCkptName.getValue();
          logdir = argLogDir.getValue();
  
          // std::cout << "MPI rank:"<< rank << "; MPI size:" << size << "; PID:" << getpid() << std::endl;
          std::cout << "Worker ID: " << worker_id << " Worker Index: " << worker_index << "; Number of Workers: " << num_workers << "; PID: " << getpid() << std::endl;
          if(worker_index==0)
          {
            std::cout << "Output file path=" << kReconOutputPath << std::endl;
            std::cout << "Output dir path=" << kReconOutputDir << std::endl;
            std::cout << "Recon. dataset path=" << kReconDatasetPath << std::endl;
            std::cout << "Center value=" << center << std::endl;
            std::cout << "Number of threads per process=" << thread_count << std::endl;
            std::cout << "Write frequency=" << write_freq << std::endl;
            std::cout << "Window length=" << window_len << std::endl;
            std::cout << "Window step=" << window_step << std::endl;
            std::cout << "Window iter=" << window_iter << std::endl;
            std::cout << "Publish frequency=" << pub_freq << std::endl;
            std::cout << "Mofka Protocol=" << protocol << std::endl;
            std::cout << "Mofka batchsize=" << batchsize << std::endl;
            std::cout << "Group file=" << group_file << std::endl;
          }
        }
        catch (TCLAP::ArgException &e)
        {
          std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
        }
      }
  };