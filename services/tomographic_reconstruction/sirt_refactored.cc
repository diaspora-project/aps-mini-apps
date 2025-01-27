#include <iostream>
#include <hdf5.h>

struct PData {
        std::vector<float> data;
        std::vector<float> theta;
        std::vector<unsigned long long> dims;

        void print_info() const {
            std::cout << "Size of the dims: ";
            for (auto d : dims) std::cout << d << " ";
            std::cout << std::endl;
            std::cout << "Size of the data: " << data.size() << "; " << data.size()*4/(1024*1024.) << " MiB" << std::endl;
            std::cout << "Size of the theta: " << theta.size() << "; " << std::endl;
        }
};

PData prepare_data() {
    herr_t err; 
    // set the file name and path
    const char *file_name = "/Users/tbicer/Projects/aps-mini-apps/data/tomo_00058_all_subsampled1p_s1079s1081.h5";

    // open hdff5 file
    hid_t file_id = H5Fopen(file_name, H5F_ACC_RDONLY, H5P_DEFAULT);
    if (file_id < 0) std::cerr << "Error opening file" << std::endl;

    // read the dataset
    const char *dataset_name = "/exchange/data";
    hid_t dataset_id = H5Dopen(file_id, dataset_name, H5P_DEFAULT);
    if (dataset_id < 0) {
        std::cerr << "Error opening dataset" << std::endl;
    }

    // read the dataset dimensions
    hid_t dataspace_id = H5Dget_space(dataset_id);
    int ndims = H5Sget_simple_extent_ndims(dataspace_id);
    if (ndims != 3) // We expect 3D data
        std::cerr << "Error on number of dimensions" << ndims << std::endl;
    PData pdata;
    pdata.dims.resize(ndims);
    H5Sget_simple_extent_dims(dataspace_id, pdata.dims.data(), NULL);
    // print dimensions
    size_t ntotal = 1;
    for (int i = 0; i < pdata.dims.size(); i++) ntotal *= pdata.dims[i];

    // read the dataset
    pdata.data.resize(ntotal); // resizes the vector with default values
    err = H5Dread(dataset_id, H5T_NATIVE_FLOAT, H5S_ALL, H5S_ALL, H5P_DEFAULT, pdata.data.data());
    if (err < 0) std::cerr << "Error reading dataset" << std::endl;

    H5Dclose(dataset_id);

    // read the theta
    const char *theta_name = "/exchange/theta";
    hid_t theta_id = H5Dopen(file_id, theta_name, H5P_DEFAULT);
    if (theta_id < 0) {
        std::cerr << "Error opening theta" << std::endl;
    }
    hid_t theta_dataspace_id = H5Dget_space(theta_id);
    int theta_ndims = H5Sget_simple_extent_ndims(theta_dataspace_id);
    if (theta_ndims != 1) // We expect 1D data
        std::cerr << "Error on number of dimensions" << theta_ndims << std::endl;
    hsize_t theta_dims;
    H5Sget_simple_extent_dims(theta_dataspace_id, &theta_dims, NULL);
    pdata.theta.resize(theta_dims);
    err = H5Dread(theta_id, H5T_NATIVE_FLOAT, H5S_ALL, H5S_ALL, H5P_DEFAULT, pdata.theta.data());
    if (err < 0) std::cerr << "Error reading theta" << std::endl;

    H5Dclose(theta_id);
    H5Fclose(file_id);

    return pdata;
}

//PData prepare_metadata() { }

class TraceMetadata{
    private:
        std::vector<float> gridx_;
        std::vector<float> gridy_;

    public:
        /*const */PData pdata;
        /*const */float center;
        /*const */float mov;

        TraceMetadata(
            PData &ipdata,
            size_t num_grids,
            float icenter) 
        {
            center = icenter;
            pdata = std::move(ipdata);
            std::cout << std::endl;
            pdata.print_info();
            mov = static_cast<float>(pdata.dims[1])/2. - center;
            if(mov - std::ceil(mov) < 1e-6) mov += 1e-6;

            gridx_.resize(num_grids+1);
            gridy_.resize(num_grids+1);
            for(int i=0; i<=num_grids; i++){
                gridx_[i] = -num_grids/2. + i;
                gridy_[i] = -num_grids/2. + i;
            }
        }

        void print_info() {
            std::cout << "center: " << center << std::endl;
            std::cout << "mov: " << mov << std::endl;
            std::cout << "gridx.size: " << gridx_.size() << "; " << std::endl;
            std::cout << "gridy.size: " << gridy_.size() << std::endl;
            pdata.print_info();
        }
};

int main() {
    // make below unique smart pointer
    /*const */PData pdata = std::move(prepare_data()); // Move constructor will be called
    pdata.print_info();

    size_t num_grids = pdata.dims[2]*pdata.dims[2];
    // change the ownership of the smartpointer TraceMetadata
    TraceMetadata tmetadata(pdata, num_grids, 0.0); 

    pdata.print_info();
    tmetadata.print_info();

    return 0;
}