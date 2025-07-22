#include <iostream>
#include <hdf5.h>
#include <vector>
#include <cmath>
#include <memory>

struct PData {
        std::vector<float> data;
        std::vector<float> theta;
        std::vector<hsize_t> dims;

        void print_info() const {
            std::cout << "Size of the dims: ";
            for (auto d : dims) std::cout << d << " ";
            std::cout << std::endl;
            std::cout << "Size of the data: " << data.size() << "; " << data.size()*4/(1024*1024.) << " MiB" << std::endl;
            std::cout << "Size of the theta: " << theta.size() << "; " << std::endl;
        }
};

std::unique_ptr<PData> prepare_data() {
    herr_t err; 
    // set the file name and path
    const char *file_name = "/workspaces/aps-mini-apps/data/tomo_00058_all_subsampled1p_s1079s1081.h5";

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

    // create unique pointer
    std::unique_ptr<PData> pdata_ptr = std::make_unique<PData>();
    PData &pdata = *pdata_ptr;

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

    return pdata_ptr;
}

//PData prepare_metadata() { }

class TraceMetadata{
    public:
        const std::unique_ptr<PData> pdata_ptr;
        std::vector<float> recon;
        const float center;
        /*const */float mov;

        std::vector<float> gridx_;
        std::vector<float> gridy_;
        size_t num_grids;

        TraceMetadata(
            std::unique_ptr<PData> ipdata,
            size_t num_grids,
            float icenter)
            : pdata_ptr{std::move(ipdata)}
            , num_grids{num_grids}
            , center{icenter} 
        {
            PData &pdata = *pdata_ptr;

            mov = static_cast<float>(pdata.dims[1])/2. - center;
            if(mov - std::ceil(mov) < 1e-6) mov += 1e-6;

            gridx_.resize(num_grids+1);
            gridy_.resize(num_grids+1);
            for(int i=0; i<=num_grids; i++){
                gridx_[i] = -num_grids/2. + i;
                gridy_[i] = -num_grids/2. + i;
            }


            recon.resize(pdata_ptr->dims[1]*pdata_ptr->dims[2]*pdata_ptr->dims[2]);
        }

        void print_info() {
            std::cout << "center: " << center << std::endl;
            std::cout << "mov: " << mov << std::endl;
            std::cout << "gridx.size: " << gridx_.size() << "; " << std::endl;
            std::cout << "gridy.size: " << gridy_.size() << std::endl;
            std::cout << "recon.size: " << recon.size() << "; " << std::endl;
            pdata_ptr->print_info();
        }
};

class SIRTReconSpace{
    private:
        std::vector<float> coordx;
        std::vector<float> coordy;
        std::vector<float> ax;
        std::vector<float> ay;
        std::vector<float> bx;
        std::vector<float> by;
        std::vector<float> coorx;
        std::vector<float> coory;
        std::vector<float> leng;
        std::vector<float> leng2;
        std::vector<int> indi;
        int num_grids;

    public:
        SIRTReconSpace(int n_grids)
        : num_grids(n_grids)
        {
            coordx.resize(num_grids+1);
            coordy.resize(num_grids+1); 
            ax.resize(num_grids+1);
            ay.resize(num_grids+1);
            bx.resize(num_grids+1);
            by.resize(num_grids+1);
            coorx.resize(2*num_grids);
            coory.resize(2*num_grids);
            leng.resize(2*num_grids);
            leng2.resize(2*num_grids);
            indi.resize(2*num_grids);
        }

        int CalculateQuadrant(float theta_q) {
            return ((theta_q >= 0 && theta_q < M_PI/2) || 
                    (theta_q >= M_PI && theta_q < 3* M_PI/2)) ? 1 : 0;

        }

        void CalculateCoordinates(
            int num_grid,
            float xi, float yi, float sinq, float cosq,
            const std::vector<float> &gridx, const std::vector<float> &gridy,
            std::vector<float> &coordx, std::vector<float> &coordy) {
            for (int i=0; i<=num_grid; i++) {
                coordx[i] = xi + i*cosq;
                coordy[i] = yi + i*sinq;
            }
        }

        void Reduce(TraceMetadata &metadata) {
            const std::vector<float> &rays = metadata.pdata_ptr->data;
            const std::vector<float> &theta = metadata.pdata_ptr->theta;
            const std::vector<float> &gridx = metadata.gridx_;
            const std::vector<float> &gridy = metadata.gridy_;
            float mov = metadata.mov;

            size_t num_cols = metadata.pdata_ptr->dims[2];
            size_t num_grids = metadata.num_grids;

            size_t curr_proj = 0;
            size_t count_projs = metadata.pdata_ptr->dims[0];

            for (size_t proj = curr_proj; proj <= (curr_proj+count_projs); ++proj) {
                std::cout << "Current proj=" << proj << "; Theta=" << theta[proj] << std::endl;
                float theta_q = theta[proj];
                int quadrant = CalculateQuadrant(theta_q);
                float sinq = sinf(theta_q);
                float cosq = cosf(theta_q);                

                size_t curr_slice = 0;
                size_t curr_slice_offset = curr_slice*num_grids*num_grids;

                float *recon = metadata.recon.data() + curr_slice_offset;

                for (size_t curr_col = 0; curr_col < num_cols; ++curr_col) {
                    std::cout << "Current col=" << curr_col << std::endl;
                    float xi = -1e6;
                    float yi = (1-num_cols)/2. + curr_col + mov;

                    CalculateCoordinates(
                        num_grids, 
                        xi, yi, 
                        sinq, cosq, 
                        gridx, gridy, 
                        coordx, coordy);

                    /// Merge the (coordx, gridy) and (gridx, coordy)
                    /// Output alen and after
                    int alen, blen;
                    MergeTrimCoordinates(
                        num_grids, 
                        coordx, coordy, 
                        gridx, gridy, 
                        &alen, &blen, 
                        ax, ay, bx, by);

                    /// Sort the array of intersection points (ax, ay)
                    /// The new sorted intersection points are
                    /// stored in (coorx, coory).
                    /// if quadrant=1 then a_ind = i; if 0 then a_ind = (alen-1-i)
                    SortIntersectionPoints(
                        quadrant, 
                        alen, blen, 
                        ax, ay, bx, by, 
                        coorx, coory);
                    
                    /// Calculate the distances (leng) between the
                    /// intersection points (coorx, coory). Find
                    /// the indices of the pixels on the
                    /// reconstruction grid (ind_recon).
                    int len = alen + blen;
                    CalculateDistanceLengths(
                        len, 
                        num_grids, 
                        coorx, coory, 
                        leng, leng2, 
                        indi);

                    

                    /*******************************************************/
                    /* Below is for updating the reconstruction grid and
                    * is algorithm specific part.
                    */
                    /// Forward projection
                    float simdata = CalculateSimdata(recon, len, indi, leng);

                     /// Update recon 
                    UpdateRecon(
                        recon,
                        simdata, 
                        rays[curr_col], 
                        curr_slice, 
                        indi, 
                        leng2, leng,
                        len);
                }
            }
        }

        void UpdateRecon(
            float *recon, float simdata, float ray, int slice, 
            const std::vector<int> &indi, const std::vector<float> &leng2, 
            const std::vector<float> &leng, int len)
        {
            float upd=0., a2=0.;

            for (int i=0; i<len-1; ++i)
                a2 += leng2[i];

            upd = (ray-simdata) / a2;

            int i=0;
            size_t nans=0;
            for (; i<(len-1); ++i) {
                float val = leng[i]*upd;
                if(std::isnan(val)) {
                    ++nans;
                    continue;
                }
                recon[indi[i]] += leng[i]*upd; 
            }
            if(nans > 0) std::cout << "NaNs=" << nans << std::endl;
        }

        float CalculateSimdata(
            float *recon, int len, std::vector<int> &indi, std::vector<float> &leng)
        {
            float simdata = 0.0;
            for (int i=0; i<len-1; ++i) {
                simdata += recon[indi[i]]*leng[i];
            }
            return simdata;
        }

        void CalculateDistanceLengths(
            int len, int num_grids,
            const std::vector<float> &coorx, const std::vector<float> &coory, 
            std::vector<float> &leng, std::vector<float> &leng2, std::vector<int> &indi)
        {
            int x1, x2, i1, i2;
            float diffx, diffy, midx, midy;
            int indx, indy;

            float mgrids = num_grids/2.;

            for (int i=0; i<len-1; ++i) {
                diffx = coorx[i+1]-coorx[i];
                midx = (coorx[i+1]+coorx[i])/2.;
                diffy = coory[i+1]-coory[i];
                midy = (coory[i+1]+coory[i])/2.;
                leng2[i] = diffx*diffx + diffy*diffy;
                leng[i] = sqrt(leng2[i]);

                x1 = midx + mgrids;
                i1 = static_cast<int>(x1);
                indx = i1 - (i1>x1);
                x2 = midy + mgrids;
                i2 = static_cast<int>(x2);
                indy = i2 - (i2>x2);
                indi[i] = indx+(indy*num_grids);
            }
            
        }

        void SortIntersectionPoints(
            int ind_cond,
            int alen, int blen,
            const std::vector<float> &ax, const std::vector<float> &ay,
            const std::vector<float> &bx, const std::vector<float> &by,
            std::vector<float> &coorx, std::vector<float> &coory)
        {
            int i=0, j=0, k=0;
            int a_ind;

            for(int i=0, j=0; i<alen; ++i){
                a_ind = (ind_cond) ? i : (alen-1-i);
                coorx[j] = ax[a_ind];
                coory[j] = ay[a_ind];
                i++; j++;
            }

            while (i < alen && j < blen)
            {
                a_ind = (ind_cond) ? i : (alen-1-i);
                if (ax[a_ind] < bx[j]) {
                    coorx[k] = ax[a_ind];
                    coory[k] = ay[a_ind];
                    i++;
                    k++;
                } else {
                    coorx[k] = bx[j];
                    coory[k] = by[j];
                    j++;
                    k++;
                }
            }
            while (i < alen) {
                a_ind = (ind_cond) ? i : (alen-1-i);
                coorx[k] = ax[a_ind];
                coory[k] = ay[a_ind];
                i++;
                k++;
            }
            while (j < blen) {
                coorx[k] = bx[j];
                coory[k] = by[j];
                j++;
                k++;
            }
        }

        void MergeTrimCoordinates(
            int num_grid,
            const std::vector<float> &coordx, const std::vector<float> &coordy,
            const std::vector<float> &gridx, const std::vector<float> &gridy,
            int *alen, int *blen,
            std::vector<float> &ax, std::vector<float> &ay,
            std::vector<float> &bx, std::vector<float> &by)
        {
            /* Merge the (coordx, gridy) and (gridx, coordy)
            * on a single array of points (ax, ay) and trim
            * the coordinates that are outside the
            * reconstruction grid. 
            */
            *alen = 0;
            *blen = 0;
            for (int i = 0; i <= num_grid; ++i) {
                if (coordx[i] > gridx[0] && 
                    coordx[i] < gridx[num_grid]) {
                ax[*alen] = coordx[i];
                ay[*alen] = gridy[i];
                (*alen)++;
                }
                if (coordy[i] > gridy[0] && 
                    coordy[i] < gridy[num_grid]) {
                bx[*blen] = gridx[i];
                by[*blen] = coordy[i];
                (*blen)++;
                }
            }
        }

        void CalcuateCoordinates(
            int num_grid,
            float xi, float yi, float sinq, float cosq,
            const std::vector<float> &gridx, const std::vector<float> &gridy,
            std::vector<float> &coordx, std::vector<float> &coordy) 
        {
            float srcx, srcy, detx, dety;
            float slope, islope;
            int n;

            /* Find the corresponding source and 
            * detector locations for a given line
            * trajectory of a projection (Projection
            * is specified by sinp and cosp).  
            */
            srcx = xi*cosq-yi*sinq;
            srcy = xi*sinq+yi*cosq;
            detx = -1 * (xi*cosq+yi*sinq);
            dety = -xi*sinq+yi*cosq;

            /* Find the intersection points of the
            * line connecting the source and the detector
            * points with the reconstruction grid. The 
            * intersection points are then defined as: 
            * (coordx, gridy) and (gridx, coordy)
            */
            slope = (srcy-dety)/(srcx-detx);
            islope = 1/slope;
            for (n = 0; n <= num_grid; n++) {
                coordx[n] = islope*(gridy[n]-srcy)+srcx;
                coordy[n] = slope*(gridx[n]-srcx)+srcy;
            }

            for (int i=0; i<=num_grid; i++) {
                coordx[i] = xi + i*cosq;
                coordy[i] = yi + i*sinq;
            }

        }



        ~SIRTReconSpace(){}

        void print_info() {
            std::cout << "num_grids: " << num_grids << std::endl;
            std::cout << "coordx.size: " << coordx.size() << std::endl;
            std::cout << "coordy.size: " << coordy.size() << std::endl;
            std::cout << "ax.size: " << ax.size() << std::endl;
            std::cout << "ay.size: " << ay.size() << std::endl;
            std::cout << "bx.size: " << bx.size() << std::endl;
            std::cout << "by.size: " << by.size() << std::endl;
            std::cout << "coorx.size: " << coorx.size() << std::endl;
            std::cout << "coory.size: " << coory.size() << std::endl;
            std::cout << "leng.size: " << leng.size() << std::endl;
            std::cout << "leng2.size: " << leng2.size() << std::endl;
            std::cout << "indi.size: " << indi.size() << std::endl;
        }
    
};

void write_recon(const std::vector<float> &recon, std::vector<hsize_t> mdims, const std::string &filename) 
{
    hid_t file_id = H5Fcreate(filename.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    if (file_id < 0) std::cerr << "Error creating file" << std::endl;

    hsize_t dims[3] = {mdims[0], mdims[1], mdims[2]};
    hid_t dataspace_id = H5Screate_simple(3, dims, NULL);
    if (dataspace_id < 0) std::cerr << "Error creating dataspace" << std::endl;

    hid_t dataset_id = H5Dcreate(file_id, "/recon", H5T_NATIVE_FLOAT, dataspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    if (dataset_id < 0) std::cerr << "Error creating dataset" << std::endl;

    herr_t err = H5Dwrite(dataset_id, H5T_NATIVE_FLOAT, H5S_ALL, H5S_ALL, H5P_DEFAULT, recon.data());
    if (err < 0) std::cerr << "Error writing dataset" << std::endl;

    H5Dclose(dataset_id);
    H5Sclose(dataspace_id);
    H5Fclose(file_id);
}

int main() {
    std::cout << "Hello" << std::endl;
    std::unique_ptr<PData> pdata_ptr = prepare_data();
    PData &pdata = *pdata_ptr;
    pdata.print_info();

    size_t num_grids = pdata.dims[2]*pdata.dims[2];
    // change the ownership of the smartpointer TraceMetadata
    std::cout << "Creating TraceMetadata" << std::endl;
    TraceMetadata tmetadata(std::move(pdata_ptr), num_grids, 1427.); 
    std::cout << "Done TraceMetadata" << std::endl;

    //pdata.print_info();
    //tmetadata.print_info();

    std::cout << "Creating SIRTReconSpace" << std::endl;
    SIRTReconSpace sirt(num_grids);
    std::cout << "SIRTReconSpace object created" << std::endl;
    for(int i=0; i<2; i++){
        std::cout << "Iteration: " << i << std::endl;
        sirt.Reduce(tmetadata);
    }

    tmetadata.print_info();

    // Write recon to file
    write_recon(tmetadata.recon,{pdata.dims[1], tmetadata.num_grids, tmetadata.num_grids}, "recon.h5");


    return 0;
}
