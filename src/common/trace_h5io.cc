#include <string>
#include <stdexcept>
#include <stdlib.h>
#include "mpi.h"
#include "trace_h5io.h"

void trace_io::DistributeSlices(
    int mpi_rank, int mpi_size,
    int n_dblocks, int &beg_index, int &n_assigned_blocks)
{
  int base = floor(n_dblocks/mpi_size);
  int remain = n_dblocks - (base*mpi_size);

  beg_index =
    (mpi_rank < remain)? (base+1)*mpi_rank :
    ((base+1)*remain) + (base * (mpi_rank-remain));

  int final_slice =
    (mpi_rank < remain) ? beg_index + (base+1) : beg_index + base;

  n_assigned_blocks = final_slice - beg_index;
}

trace_io::H5Metadata* trace_io::ReadMetadata(
    char const *file_path, 
    char const *dataset_path)
{
  hid_t file_id, dataspace_id, dataset_id;

  H5Metadata *h5_metadata = new H5Metadata();
  if(h5_metadata==NULL)
    throw std::runtime_error("Unable to allocate space for H5 metadata!");

  /* Open the file */
  file_id = H5Fopen(file_path, H5F_ACC_RDONLY, H5P_DEFAULT);
  if(file_id <= 0)
    throw std::runtime_error("Unable to open hdf5 file!");

  /* Open the dataset */
  dataset_id = H5Dopen2(file_id, dataset_path, H5P_DEFAULT);
  if(dataset_id <= 0)
    throw std::runtime_error("Unable to open hdf5 dataset!");

  h5_metadata->file_path = file_path;
  h5_metadata->dataset_path = dataset_path;

  /* Get the dataspace id */
  dataspace_id = H5Dget_space(dataset_id);

  /* Get the number of dimensions*/
  int ndims = H5Sget_simple_extent_ndims(dataspace_id);
  if(ndims<=0)
    throw std::runtime_error("Target hdf5 dataset dimensions should not be <=0");
  h5_metadata->ndims = ndims;

  /* Get the dimensions */
  hsize_t *dims, *m_dims;
  dims = (hsize_t*) calloc(ndims, sizeof(*dims));
  m_dims = (hsize_t*) calloc(ndims, sizeof(*m_dims));
  int ret = H5Sget_simple_extent_dims(dataspace_id, dims, m_dims);
  if(ret <= 0)
    throw std::runtime_error("Unable to get dimensions from hdf5 dataset");
  h5_metadata->dims = dims;

  /* Calculate the total size of the data */
  int i;
  size_t elem_cnt=1;
  for(i=0; i<ndims; ++i)
    elem_cnt*=dims[i];

  H5Dclose(dataset_id);
  H5Sclose(dataspace_id);
  H5Fclose(file_id);
  free(m_dims);

  return h5_metadata;
}

trace_io::H5Data* trace_io::ReadTheta(H5Metadata *metadata_p){
  auto &metadata = *metadata_p;

  if(metadata.ndims!=1)
    throw std::runtime_error("Theta file dimension should be 1!");

  hid_t file_id, dataset_id;
  herr_t status;

  /* Open the file */
  file_id = H5Fopen(metadata.file_path.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
  if(file_id<1)
    throw std::runtime_error("Unable to open hdf5 file");

  /* Open the dataset */
  dataset_id = H5Dopen2(file_id, metadata.dataset_path.c_str(), H5P_DEFAULT);
  if(dataset_id<1)
    throw std::runtime_error("Unable to open hdf5 dataset");

  /* Allocate memory */
  hid_t dtype_id = H5Dget_type(dataset_id);
  hid_t mem_type_id = H5Tget_native_type(dtype_id, H5T_DIR_ASCEND);
  size_t ldtype_size = H5Tget_size(mem_type_id);

  H5Data *l_data = new H5Data();
  l_data->metadata = metadata_p;
  l_data->offset = 0;   /* Starting from 0 offset */
  l_data->count = metadata.dims[0];
  l_data->in_memory_type_size = ldtype_size;

  char *dset_data = new char[l_data->count * l_data->in_memory_type_size];
  for(size_t i=0; i<l_data->count * l_data->in_memory_type_size; ++i)
    (static_cast<char *>(dset_data))[0] = 0x00;

  /* Read data into memory */
  status = H5Dread(
      dataset_id, 
      mem_type_id, 
      H5S_ALL, /*hid_t mem_space_id*/
      H5S_ALL, /*hid_t file_space_id*/
      H5S_ALL, /*hid_t xfer_plist_id*/
      dset_data);

  l_data->data = dset_data;

  H5Tclose(dtype_id);
  H5Dclose(dataset_id);
  H5Fclose(file_id);

  return l_data;
}

trace_io::H5Data* trace_io::ReadSlices(
    H5Metadata *metadata_p,
    int beg_slice, int count, 
    int filter_id)
{
  auto &metadata = *metadata_p;

  herr_t status;

  /* Setup filter */
  unsigned int flags = H5Z_FLAG_MANDATORY;
  hid_t dcpl = H5Pcreate (H5P_DATASET_CREATE);
  if(filter_id>0)
    status = H5Pset_filter(dcpl, filter_id, flags, 0, NULL);

  /* Currently only 3 dimensional datasets are supported */
  if(metadata.ndims!=3)
    throw std::runtime_error("Currently only 3D dataset is supporter");

  /* H5F_ACC_RDWR: Fails if exists */
  /* H5F_ACC_TRUNC: Overwrites if exists */
  /* H5F_ACC_RDONLY: Read only */
  hid_t file_id = H5Fopen(metadata.file_path.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
  /* Open an existing dataset. */
  hid_t dataset_id = H5Dopen2(file_id, metadata.dataset_path.c_str(), H5P_DEFAULT);

  /* Setup offset, count, and block for HDF5 hyperslab */
  int ndims = metadata.ndims;
  hsize_t *h_offset = (hsize_t*) calloc(ndims, sizeof(hsize_t)); /* {0, 0, 0} */
  hsize_t *h_count = (hsize_t*) calloc(ndims, sizeof(hsize_t));
  hsize_t *h_block = (hsize_t*) calloc(ndims, sizeof(hsize_t));


  unsigned long long *dims = metadata.dims;
  /* h_offset = {0, slice_id, 0} */
  /* h_offset[1] = w_neighbor_offset; */
  h_offset[1] = beg_slice;
  /* h_count = {1, nslices, 1} */
  h_count[0] = 1;
  /*h_count[1] = w_neighbor_count; */
  h_count[1] = count;
  h_count[2] = 1;
  /* h_block = {#projections, 1, #columns} */
  h_block[0] = dims[0];
  h_block[1] = 1;
  h_block[2] = dims[2];


  hid_t dataspace = H5Dget_space(dataset_id);
  status = H5Sselect_hyperslab(dataspace, H5S_SELECT_SET,
      h_offset, NULL, h_count, h_block);
  /* End of HDF5 data setup */

  /* Setup offset, count, and block for MEMORY */
  hsize_t *dimsm = (hsize_t*) calloc(ndims, sizeof(hsize_t));
  dimsm[0] = dims[0];
  dimsm[1] = count;
  dimsm[2] = dims[2];

  hid_t memspace = H5Screate_simple(ndims,dimsm, NULL);

  hsize_t *m_offset = (hsize_t*) calloc(ndims, sizeof(hsize_t)); /* {0, 0, 0} */
  hsize_t *m_count = (hsize_t*) calloc(ndims, sizeof(hsize_t));
  hsize_t *m_block = (hsize_t*) calloc(ndims, sizeof(hsize_t));

  m_count[0] = 1;
  m_count[1] = count;
  m_count[2] = 1;
  m_block[0] = dims[0];
  m_block[1] = 1;
  m_block[2] = dims[2];

  status = H5Sselect_hyperslab(memspace, H5S_SELECT_SET,
      m_offset, NULL, m_count, m_block);
  /* End of MEMORY hyperslab setup */

  /* Allocate memory for the slices */
  hid_t dtype_id = H5Dget_type(dataset_id);
  if(dtype_id<=0)
    throw std::runtime_error("Data type cannot be recognized.");

  hid_t mem_type_id = H5Tget_native_type(dtype_id, H5T_DIR_ASCEND);
  size_t ldtype_size = H5Tget_size(mem_type_id);

  H5Data *l_data = new H5Data();
  l_data->metadata = metadata_p;
  l_data->offset = beg_slice;
  l_data->count = count;
  l_data->in_memory_type_size = ldtype_size;

  size_t nelem_per_slice = metadata.dims[0] * metadata.dims[2];
  size_t nelem_slices = nelem_per_slice * count;
  char *dset_data = new char[nelem_slices*ldtype_size];
  for(size_t i=0; i<nelem_slices*ldtype_size; ++i)
    (static_cast<char*>(dset_data))[i] = 0x00;

  /* Read data into memory */
  status = H5Dread(dataset_id, mem_type_id, memspace, dataspace,
      H5P_DEFAULT, dset_data);

  l_data->data = dset_data;
  /* Close the dataset and file */
  status = H5Dclose(dataset_id);
  status = H5Sclose(dataspace);
  status = H5Fclose(file_id);
  status = H5Tclose(dtype_id);

  /* Free memories */
  free(m_block);
  free(m_offset);
  free(m_count);
  free(h_block);
  free(h_offset);
  free(h_count);
  free(dimsm);

  return l_data;
}


trace_io::H5Data* trace_io::ReadProjections(H5Metadata *metadata_p,
    int beg_projection, int count, int filter_id){
  auto &metadata = *metadata_p;

  herr_t status;

  /* Setup filter */
  unsigned int flags = H5Z_FLAG_MANDATORY;
  hid_t dcpl = H5Pcreate (H5P_DATASET_CREATE);
  if(filter_id>0)
    status = H5Pset_filter(dcpl, filter_id, flags, 0, NULL);

  /* Currently only 3 dimensional datasets are supported */
  if(metadata.ndims!=3)
    throw std::runtime_error("Currently only 3D dataset is supporter");

  /* H5F_ACC_RDWR: Fails if exists */
  /* H5F_ACC_TRUNC: Overwrites if exists */
  /* H5F_ACC_RDONLY: Read only */
  hid_t file_id = H5Fopen(
      metadata.file_path.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
  /* Open an existing dataset. */

  hid_t dataset_id = H5Dopen2(
      file_id, metadata.dataset_path.c_str(), H5P_DEFAULT);

  /* Setup offset, count, and block for HDF5 hyperslab */
  int ndims = metadata.ndims;
  hsize_t *h_offset = (hsize_t*) calloc(ndims, sizeof(hsize_t)); /* {0, 0, 0} */
  hsize_t *h_count = (hsize_t*) calloc(ndims, sizeof(hsize_t));
  hsize_t *h_block = (hsize_t*) calloc(ndims, sizeof(hsize_t));

  unsigned long long *dims = metadata.dims;
  /* h_offset = {0, slice_id, 0} */
  /* h_offset[1] = w_neighbor_offset; */
  h_offset[0] = beg_projection;
  /* h_count = {1, nslices, 1} */
  h_count[0] = count;
  /*h_count[1] = w_neighbor_count; */
  h_count[1] = 1;
  h_count[2] = 1;
  /* h_block = {#projections, #slices, #columns} */
  h_block[0] = 1;
  h_block[1] = dims[1];
  h_block[2] = dims[2];


  hid_t dataspace = H5Dget_space(dataset_id);
  status = H5Sselect_hyperslab(dataspace, H5S_SELECT_SET,
      h_offset, NULL, h_count, h_block);
  /* End of HDF5 data setup */

  /* Setup offset, count, and block for MEMORY */
  hsize_t *dimsm = (hsize_t*) calloc(ndims, sizeof(hsize_t));
  dimsm[0] = count;
  dimsm[1] = dims[1];
  dimsm[2] = dims[2];

  hid_t memspace = H5Screate_simple(ndims,dimsm, NULL);

  hsize_t *m_offset = (hsize_t*) calloc(ndims, sizeof(hsize_t)); /* {0, 0, 0} */
  hsize_t *m_count = (hsize_t*) calloc(ndims, sizeof(hsize_t));
  hsize_t *m_block = (hsize_t*) calloc(ndims, sizeof(hsize_t));

  m_count[0] = count;
  m_count[1] = 1;
  m_count[2] = 1;
  m_block[0] = 1;

  m_block[1] = dims[1];
  m_block[2] = dims[2];

  status = H5Sselect_hyperslab(memspace, H5S_SELECT_SET,
      m_offset, NULL, m_count, m_block);
  /* End of MEMORY hyperslab setup */

  /* Allocate memory for the slices */
  hid_t dtype_id = H5Dget_type(dataset_id);
  if(dtype_id<=0)
    throw std::runtime_error("Data type cannot be recognized.");

  hid_t mem_type_id = H5Tget_native_type(dtype_id, H5T_DIR_ASCEND);
  size_t ldtype_size = H5Tget_size(mem_type_id);

  H5Data *l_data = new H5Data();
  l_data->metadata = metadata_p;
  l_data->offset = beg_projection;
  l_data->count = count;
  l_data->in_memory_type_size = ldtype_size;

  size_t nelem_per_slice = metadata.dims[1] * metadata.dims[2];
  size_t nelem_slices = nelem_per_slice * count;
  void *dset_data = calloc(nelem_slices, ldtype_size);

  /* Read data into memory */
  status = H5Dread(dataset_id, mem_type_id, memspace, dataspace,
      H5P_DEFAULT, dset_data);

  l_data->data = dset_data;
  /* Close the dataset and file */
  status = H5Dclose(dataset_id);
  status = H5Sclose(dataspace);
  status = H5Fclose(file_id);
  status = H5Tclose(dtype_id);

  /* Free memories */
  free(m_block);
  free(m_offset);
  free(m_count);
  free(h_block);
  free(h_offset);
  free(h_count);
  free(dimsm);

  return l_data;
}

void trace_io::WriteData(
    float *recon, /* Data values */
    hsize_t ndims, hsize_t *dims, /* This process' dimension values */
    int slice_id, /* Starting slice id, for calculating dest. offset address */
    hsize_t dataset_ndims, hsize_t *dataset_dims, /* Dataset dimension values */
    int target_dim,
    char const *file_name, char const *dataset_name,
    MPI_Comm comm, MPI_Info info)
{ 
  if(ndims!=3 || recon==NULL || file_name==NULL || dataset_name==NULL)
    throw std::runtime_error("Wrong input parameter is passed");

  /* Set up file access property list with parallel I/O access */
  hid_t plist_id = H5Pcreate(H5P_FILE_ACCESS);
  H5Pset_fapl_mpio(plist_id, comm, info);


  /* Create a new file collectively and release property list identifier. */
  /* H5F_ACC_TRUNC: Overwrites if exists */
  hid_t file_id = H5Fcreate(file_name, H5F_ACC_TRUNC, H5P_DEFAULT, plist_id);
  H5Pclose(plist_id);

  /* Create the dataspace for the dataset */
  /* Relying on casting data fails in this case!
   * Make sure you pass the correct data type to the HDF5 functions!
   */
  hsize_t *dataset_ddims = (hsize_t *)calloc(dataset_ndims, sizeof(hsize_t));
  for(hsize_t i=0; i<dataset_ndims; ++i) {
    dataset_ddims[i] = dataset_dims[i];
  }
  hid_t filespace = H5Screate_simple(dataset_ndims, dataset_ddims, NULL);

  /* Create the dataset with default properties and close filespace */

  hid_t dset_id =
    H5Dcreate(file_id, dataset_name, H5T_NATIVE_FLOAT, filespace,
        H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  H5Sclose(filespace);

  /* Setup memory space and its hyperslab */
  /*
   *      hsize_t *m_offset = calloc(ndims, sizeof(hsize_t));
   *      hsize_t *m_block = calloc(ndims, sizeof(hsize_t));
   *
   *      m_block[0] = dims[0];
   *      m_block[1] = dims[1];
   *      m_block[2] = dims[2];
   */

  hsize_t *m_count = (hsize_t*) calloc(ndims, sizeof(hsize_t));
  m_count[0] = dims[0];
  m_count[1] = dims[1];
  m_count[2] = dims[2];
  hid_t memspace = H5Screate_simple(ndims, m_count, NULL);

  /* Below might be unnecessary, since we work on the whole memory */
  /*
   * herr_t status = H5Sselect_hyperslab(memspace, H5S_SELECT_SET, 
   * m_offset, NULL, m_count, m_block);
   */
  /* End of memory space and hyberslap selection */

  /* Setup offset addresses and select hyperslab in the file */
  /* {0, 0, 0} */
  hsize_t *d_offset = (hsize_t*) calloc(ndims, sizeof(hsize_t));
  d_offset[target_dim] = slice_id;

  hsize_t *d_count = (hsize_t*) calloc(ndims, sizeof(hsize_t));

  d_count[0] = dims[0];
  d_count[1] = dims[1];
  d_count[2] = dims[2];
  filespace = H5Dget_space(dset_id);
  H5Sselect_hyperslab(filespace, H5S_SELECT_SET, d_offset, NULL,
      d_count, NULL);
  /* End of dataset hyperslab selection */

  /* Create property list for collective dataset write */
  plist_id = H5Pcreate(H5P_DATASET_XFER);
  H5Pset_dxpl_mpio(plist_id, H5FD_MPIO_COLLECTIVE);

  H5Dwrite(dset_id, H5T_NATIVE_FLOAT, memspace, filespace,
      plist_id, recon);

  /* Close/release resources */
  H5Dclose(dset_id);
  H5Sclose(filespace);
  H5Sclose(memspace);
  H5Pclose(plist_id);
  H5Fclose(file_id);

  free(dataset_ddims);
  free(d_offset);
}

void trace_io::WriteRecon(
    TraceMetadata &rank_metadata, /// This rank's in memory data
    H5Metadata &dataset_metadata, /// Metadata of all dataset
    std::string const output_path,     /// Output file path
    std::string const dataset_path)    /// Dataset path in output file
{
  hsize_t ndims = static_cast<hsize_t>(dataset_metadata.ndims);
  hsize_t rank_dims[3] = {
    static_cast<hsize_t>(rank_metadata.num_slices()), 
    static_cast<hsize_t>(rank_metadata.num_cols()), 
    static_cast<hsize_t>(rank_metadata.num_cols())};
  hsize_t app_dims[3] = {
    static_cast<hsize_t>(dataset_metadata.dims[1]),
    static_cast<hsize_t>(dataset_metadata.dims[2]),
    static_cast<hsize_t>(dataset_metadata.dims[2])};
  ADataRegion<float> &recon = rank_metadata.recon();

  WriteData(
      &recon[0],
      ndims, rank_dims,
      rank_metadata.slice_id(),
      ndims, app_dims,
      0,
      output_path.c_str(), dataset_path.c_str(),
      MPI_COMM_WORLD, MPI_INFO_NULL);
}
