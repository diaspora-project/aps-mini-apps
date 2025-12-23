#ifndef TRACE_RUNTIME_CONFIG_H
#define TRACE_RUNTIME_CONFIG_H

#include <cassert>
#include <csignal>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

#include "tclap/CmdLine.h"
#include "trace_data.h"
#include "trace_h5io.h"

// Detect compilers that lack a usable <charconv> (NVHPC/PGI)
#if defined(__NVCOMPILER) || defined(__PGI)
  #define TRACE_NO_CHARCONV 1
#else
  #include <charconv>
  #include <system_error>
#endif

#include <cstdlib>   // strtol fallback

class TraceRuntimeConfig {
  public:
    std::string kReconOutputPath;
    std::string kReconDatasetPath;
    std::string kReconOutputDir;
    std::string protocol;
    std::string group_file;
    size_t      batchsize = 1;
    int         thread_count = 1;
    int         window_len = 32;
    int         window_step = 1;
    int         window_iter = 1;
    int         write_freq = 0;
    int         center = 0;
    std::string dest_host;
    int         dest_port = 0;
    std::string pub_addr;
    int         pub_freq = 0;
    std::string worker_id = "0";
    int         worker_index = 0;   // initialize
    int         num_workers = 1;
    int         num_passes = 1;
    int         ckpt_freq = 1;
    std::string ckpt_config = "veloc.cfg";
    std::string ckpt_name   = "sirt-ckpt";
    std::string logdir      = ".";
    bool        sst         = true;

    TraceRuntimeConfig(int argc, char **argv) {
      try {
        TCLAP::CmdLine cmd("SIRT Iterative Image Reconstruction", ' ', "0.01");

        TCLAP::ValueArg<std::string> argWorkerId(
          "", "worker-id", "The Worker Id", false, "0", "string");
        TCLAP::ValueArg<int> argNumWorkers(
          "", "num-workers", "Number of Reconstruction Workers", false, 1, "int");
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
          "c", "center", "Center value", false, 0.f, "float");
        TCLAP::ValueArg<int> argThreadCount(
          "t", "thread", "Number of threads per process", false, 1, "int");
        TCLAP::ValueArg<float> argWriteFreq(
          "", "write-freq", "Write frequency", false, 10000, "int");
        TCLAP::ValueArg<float> argWindowLen(
          "", "window-length", "Number of projections stored in the window",
          false, 32, "int");
        TCLAP::ValueArg<float> argWindowStep(
          "", "window-step", "Number of projections received in each request",
          false, 1, "int");
        TCLAP::ValueArg<float> argWindowIter(
          "", "window-iter", "Number of iterations on received window",
          false, 1, "int");
        TCLAP::ValueArg<int> argNumPasses(
          "", "num-passes", "Number of passes on data streams",
          false, 4001, "int");
        TCLAP::ValueArg<std::string> argLogDir(
          "", "logdir", "Log directory", false, ".", "string");
        TCLAP::ValueArg<int> argCkptFreq(
          "", "ckpt-freq", "Checkpoint frequency", false, 1, "int");
        TCLAP::ValueArg<std::string> argCkptConfig(
          "", "ckpt-config", "Checkpoint Configuration (VeloC)", false, "veloc.cfg", "string");
        TCLAP::ValueArg<std::string> argCkptName(
          "", "ckpt-name", "Checkpoint Name (VeloC)", false, "sirt-ckpt", "string");
        TCLAP::ValueArg<float> argSST(
          "", "sst", "Get data from SST Stream", false, true, "bool");


        cmd.add(argWorkerId);
        cmd.add(argNumWorkers);

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

        cmd.add(argSST);

        cmd.parse(argc, argv);

        worker_id = argWorkerId.getValue();
        // ---- robust parse of worker_index from worker_id ----
        if (!parse_int(worker_id, worker_index)) {
          std::cerr << "Warning: worker-id=\"" << worker_id
                    << "\" is not an integer; defaulting worker_index=0\n";
          worker_index = 0;
        }

        num_workers       = argNumWorkers.getValue();
        kReconOutputPath  = argReconOutputPath.getValue();
        kReconOutputDir   = argReconOutputDir.getValue();
        kReconDatasetPath = argReconDatasetPath.getValue();
        center            = static_cast<int>(argCenter.getValue());
        thread_count      = argThreadCount.getValue();
        write_freq        = static_cast<int>(argWriteFreq.getValue());
        window_len        = static_cast<int>(argWindowLen.getValue());
        window_step       = static_cast<int>(argWindowStep.getValue());
        window_iter       = static_cast<int>(argWindowIter.getValue());
        num_passes        = argNumPasses.getValue();

        protocol   = argMofkaProtocol.getValue();
        batchsize  = argBatchSize.getValue();
        group_file = argGroupFile.getValue();
        ckpt_freq  = argCkptFreq.getValue();
        ckpt_config= argCkptConfig.getValue();
        ckpt_name  = argCkptName.getValue();
        logdir     = argLogDir.getValue();
        pub_freq   = static_cast<int>(argPubFreq.getValue()); // actually store it

        sst        = argSST.getValue();

        std::cout << "Worker ID: " << worker_id
                  << " Worker Index: " << worker_index
                  << "; Number of Workers: " << num_workers
                  << "; PID: " << getpid() << std::endl;

        if (worker_index == 0) {
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
          std::cout << "SST Stream=" << (sst ? "true" : "false") << std::endl;
        }
      } catch (TCLAP::ArgException &e) {
        std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
      }
    }

  private:
    // Parse signed int from string; use std::from_chars when available, otherwise a safe fallback.
    static bool parse_int(const std::string &s, int &out) {
#if !defined(TRACE_NO_CHARCONV)
      const char* first = s.data();
      const char* last  = s.data() + s.size();
      std::from_chars_result res = std::from_chars(first, last, out);
      return (res.ec == std::errc{} && res.ptr == last);
#else
      // Fallback: strtol (no exceptions, C-locale, simple)
      char* end = nullptr;
      errno = 0;
      long v = std::strtol(s.c_str(), &end, 10);
      if (errno != 0 || end == s.c_str() || *end != '\0') return false;
      if (v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max()) return false;
      out = static_cast<int>(v);
      return true;
#endif
    }
};

#endif // TRACE_RUNTIME_CONFIG_H