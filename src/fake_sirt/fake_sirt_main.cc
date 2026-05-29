// fake-sirt: protocol-equivalent stand-in for sirt_stream.
//
// Same MPI launch shape, same handshake with DIST, same dist_sirt
// consume cadence and same sirt_den publish cadence. No actual SIRT
// solver — published reconstruction slices are zero-filled buffers of
// the correct shape, and the HDF5 output file is a zero-filled dataset
// of the same dimensions tekapp-sirt would produce.

#include "fake_diaspora_stream.h"
#include "tclap/CmdLine.h"

#include <hdf5.h>
#include <mpi.h>

#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

class FakeRuntimeConfig {
  public:
    std::string kReconOutputPath;
    std::string kReconDatasetPath;
    std::string kReconOutputDir;
    std::string driver_type;
    std::string driver_config_file;
    size_t batchsize;
    int thread_count;
    int window_len;
    int window_step;
    int window_iter;
    int write_freq = 0;
    int center;
    int pub_freq = 0;

    FakeRuntimeConfig(int argc, char** argv, int rank, int /*size*/) {
        try {
            TCLAP::CmdLine cmd("fake SIRT (protocol-equivalent)", ' ', "0.01");
            TCLAP::ValueArg<std::string> argDriverType(
                "", "driver_type", "Type of diaspora driver", false, "files", "string");
            TCLAP::ValueArg<std::string> argDriverConfigFile(
                "", "driver_config_file", "Config file for the Diaspora driver",
                false, "", "string");
            TCLAP::ValueArg<size_t> argBatchSize(
                "", "batchsize", "Mofka batchsize", false, 1, "size_t");
            TCLAP::ValueArg<std::string> argReconOutputPath(
                "o", "reconOutputPath", "Output file path (hdf5)",
                false, "./output.h5", "string");
            TCLAP::ValueArg<std::string> argReconOutputDir(
                "", "recon-output-dir", "Output directory for streaming outputs",
                false, ".", "string");
            TCLAP::ValueArg<std::string> argReconDatasetPath(
                "r", "reconDatasetPath", "Reconstruction dataset path in hdf5",
                false, "/data", "string");
            TCLAP::ValueArg<float> argPubFreq(
                "", "pub-freq", "Publish frequency (ignored)", false, 10000, "int");
            TCLAP::ValueArg<float> argCenter(
                "c", "center", "Center value (ignored)", false, 0., "float");
            TCLAP::ValueArg<int> argThreadCount(
                "t", "thread", "Threads per process (ignored)", false, 1, "int");
            TCLAP::ValueArg<float> argWriteFreq(
                "", "write-freq", "Write frequency", false, 10000, "int");
            TCLAP::ValueArg<float> argWindowLen(
                "", "window-length", "Window length (informational)", false, 32, "int");
            TCLAP::ValueArg<float> argWindowStep(
                "", "window-step", "Projections per request", false, 1, "int");
            TCLAP::ValueArg<float> argWindowIter(
                "", "window-iter", "Iterations per window (ignored)", false, 1, "int");

            cmd.add(argDriverType);
            cmd.add(argDriverConfigFile);
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

            cmd.parse(argc, argv);
            kReconOutputPath   = argReconOutputPath.getValue();
            kReconOutputDir    = argReconOutputDir.getValue();
            kReconDatasetPath  = argReconDatasetPath.getValue();
            center             = argCenter.getValue();
            thread_count       = argThreadCount.getValue();
            write_freq         = argWriteFreq.getValue();
            window_len         = argWindowLen.getValue();
            window_step        = argWindowStep.getValue();
            window_iter        = argWindowIter.getValue();
            batchsize          = argBatchSize.getValue();
            driver_type        = argDriverType.getValue();
            driver_config_file = argDriverConfigFile.getValue();

            if (rank == 0) {
                std::cout << "fake-sirt: write_freq=" << write_freq
                          << " window_step=" << window_step
                          << " window_iter=" << window_iter
                          << " batchsize=" << batchsize
                          << " driver=" << driver_type << std::endl;
            }
        } catch (TCLAP::ArgException& e) {
            std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
        }
    }
};

// Rank 0 writes a zero-filled HDF5 dataset with the same shape tekapp-sirt
// would produce: [tn_sinograms, n_rays_per_proj_row, n_rays_per_proj_row].
static void write_zero_recon(const std::string& path,
                             const std::string& dset_path,
                             hsize_t dim0, hsize_t dim1, hsize_t dim2) {
    hid_t file_id = H5Fcreate(path.c_str(), H5F_ACC_TRUNC,
                              H5P_DEFAULT, H5P_DEFAULT);
    if (file_id < 0) {
        std::cerr << "fake-sirt: failed to create " << path << std::endl;
        return;
    }
    hsize_t dims[3] = {dim0, dim1, dim2};
    hid_t space_id = H5Screate_simple(3, dims, nullptr);
    hid_t dset_id  = H5Dcreate2(file_id, dset_path.c_str(), H5T_NATIVE_FLOAT,
                                space_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    // Allocate one z-slice's worth of zeros and write it slice-by-slice
    // to avoid allocating the full cube for large reconstructions.
    std::vector<float> slice(dim1 * dim2, 0.0f);
    hsize_t slab_dims[3] = {1, dim1, dim2};
    hid_t mem_space = H5Screate_simple(3, slab_dims, nullptr);
    for (hsize_t z = 0; z < dim0; ++z) {
        hsize_t offset[3] = {z, 0, 0};
        H5Sselect_hyperslab(space_id, H5S_SELECT_SET, offset, nullptr,
                            slab_dims, nullptr);
        H5Dwrite(dset_id, H5T_NATIVE_FLOAT, mem_space, space_id,
                 H5P_DEFAULT, slice.data());
    }
    H5Sclose(mem_space);
    H5Dclose(dset_id);
    H5Sclose(space_id);
    H5Fclose(file_id);
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank = 0, size = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    FakeRuntimeConfig config(argc, argv, rank, size);
    FakeDiasporaStream ms(config.driver_type, config.driver_config_file,
                          config.batchsize, rank, size);
    ms.handshake(rank, size);

    auto producer = ms.getProducer("sirt_den", "sirt");
    auto consumer = ms.getConsumer("dist_sirt", "sirt",
                                   {static_cast<size_t>(rank)});

    json tmetadata    = ms.getInfo();
    auto n_blocks     = tmetadata["n_sinograms"].get<int64_t>();
    auto num_cols     = tmetadata["n_rays_per_proj_row"].get<int64_t>();
    auto tn_sinograms = tmetadata["tn_sinograms"].get<int64_t>();

    size_t rank_data_floats = static_cast<size_t>(n_blocks) *
                              static_cast<size_t>(num_cols) *
                              static_cast<size_t>(num_cols);

    bool fin_seen = false;
    for (int passes = 0; ; ++passes) {
        fin_seen = ms.pullStep(config.window_step, consumer);
        if (fin_seen) break;

        // tekapp iterates `window_iter` times on the window here; fake-sirt
        // is a no-op — matching the per-window publish cadence is what
        // affects downstream traffic.
        for (int i = 0; i < config.window_iter; ++i) { /* no-op */ }

        if (config.write_freq > 0 && !(passes % config.write_freq)) {
            std::stringstream iteration_stream;
            iteration_stream << std::setfill('0') << std::setw(6) << passes;
            std::string outputpath = config.kReconOutputDir + "/" +
                                     iteration_stream.str() + "-recon.h5";

            if (rank == 0) {
                write_zero_recon(outputpath, config.kReconDatasetPath,
                                 static_cast<hsize_t>(tn_sinograms),
                                 static_cast<hsize_t>(num_cols),
                                 static_cast<hsize_t>(num_cols));
            }

            hsize_t rank_dims[3] = {static_cast<hsize_t>(n_blocks),
                                    static_cast<hsize_t>(num_cols),
                                    static_cast<hsize_t>(num_cols)};
            hsize_t app_dims[3]  = {static_cast<hsize_t>(tn_sinograms),
                                    static_cast<hsize_t>(num_cols),
                                    static_cast<hsize_t>(num_cols)};
            json md = {{"Type", "DATA"},
                       {"rank", rank},
                       {"iteration_stream", iteration_stream.str()},
                       {"rank_dims", rank_dims},
                       {"app_dims", app_dims},
                       {"recon_slice_data_index", 0}};
            try {
                ms.publishZeros(md, rank_data_floats, producer,
                                static_cast<size_t>(rank));
            } catch (const diaspora::Exception& ex) {
                spdlog::critical("{}", ex.what());
                MPI_Abort(MPI_COMM_WORLD, -1);
            }
            MPI_Barrier(MPI_COMM_WORLD);
        }
    }

    ms.recordTs("FLUSH_START topic=sirt_den");
    producer.flush().wait(-1);
    ms.recordTs("FLUSH_END topic=sirt_den");

    MPI_Barrier(MPI_COMM_WORLD);
    json fin_md = {{"Type", "FIN"}};
    float d = 1;
    ms.recordTs(fmt::format("PUSH_START topic=sirt_den,data_size={}", sizeof(float)));
    producer.push(diaspora::Metadata{fin_md},
                  diaspora::DataView{&d, sizeof(float)},
                  static_cast<size_t>(rank));
    ms.recordTs("PUSH_END topic=sirt_den");
    ms.recordTs("FLUSH_WAIT_START topic=sirt_den");
    producer.flush().wait(-1);
    ms.recordTs("FLUSH_WAIT_END topic=sirt_den");

    ms.writeTs(rank);
    if (rank == 0) std::cout << "fake-sirt: exiting" << std::endl;
    MPI_Finalize();
    return 0;
}
