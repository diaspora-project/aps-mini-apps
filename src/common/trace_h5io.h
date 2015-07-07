#ifndef DISP_APPS_RECONSTRUCTION_COMMON_H5IO_H
#define DISP_APPS_RECONSTRUCTION_COMMON_H5IO_H 

#include "hdf5.h"
#include "trace_data.h"

namespace trace_io {

  typedef struct {
    std::string file_path;
    std::string dataset_path;
    int ndims;
    hsize_t *dims;
    hsize_t data_size;
  } H5Metadata;

  typedef struct {
    void *data;
    int offset;
    hsize_t count;
    size_t in_memory_type_size;
    H5Metadata *metadata;
  } H5Data;

  void DistributeSlices(
      int mpi_rank, int mpi_size,
      int n_dblocks, int &beg_index, int &n_assigned_blocks);

  H5Metadata* ReadMetadata(char const *file_path, char const *dataset_path);

  H5Data* ReadTheta(H5Metadata *metadata_p);

  H5Data* ReadSlices(
      H5Metadata *metadata_p,
      int beg_slice, int count, 
      int filter_id);

  H5Data* ReadProjections(H5Metadata *metadata_p,
      int beg_projection, int count, int filter_id);

  void WriteData(
      float *recon, /* Data values */
      hsize_t ndims, hsize_t *dims, /* This process' dimension values */
      int slice_id, /* Starting slice id, for calculating dest. offset address */
      hsize_t dataset_ndims, hsize_t *dataset_dims, /* Dataset dimension values */
      int target_dim,
      char const *file_name, char const *dataset_name,
      MPI_Comm comm, MPI_Info info);

  void WriteRecon(
      TraceMetadata &rank_metadata, /// This rank's in memory data
      H5Metadata &dataset_metadata, /// Metadata of all dataset
      std::string const output_path,     /// Output file path
      std::string const dataset_path);   /// Dataset path in output file
}

#endif /// DISP_APPS_RECONSTRUCTION_COMMON_H5IO_H
