#ifndef DISP_APPS_RECONSTRUCTION_COMMON_TRACE_DATA_H_
#define DISP_APPS_RECONSTRUCTION_COMMON_TRACE_DATA_H_

#include <string>
#include <cmath>
#include <stdexcept>
#include "data_region_a.h"
#include "data_region_bare_base.h"

typedef struct {
  int num_threads;
  int num_iter;

  int num_req_rays;

  std::string *input_file_path;
  std::string *data_path;
  std::string *theta_path;
  std::string *output_file_path;
  std::string *recon_file;

  float center;
} TraceDataConfig;

class TraceMetadata{
  private:
    float const *theta_;

    int const proj_id_;
    int const slice_id_;
    int const col_id_;
    int const num_total_slices_;

    int const num_projs_;
    int const num_slices_;
    int const num_cols_;
    int const num_grids_;
    float center_;
    float mov_;
    float *gridx_;
    float *gridy_;

    int const num_rays_proj_;
    int const num_rays_slice_;

    size_t const count_;
    ADataRegion<float> *recon_; 
    int const num_neighbor_recon_slices_;


  public:

    TraceMetadata(
        float const *theta,
        int const proj_id,
        int const slice_id,
        int const col_id,
        int const num_total_slices,   /// Total number of slices in whole dataset
        int const num_projs,
        int const num_slices,
        int const num_cols,
        int const num_grids,
        float center,
        /// Number of neighboring reconstruction slices
        int const num_neighbor_recon_slices,
        float const recon_init_val)
    : theta_{theta}
    , proj_id_{proj_id}
    , slice_id_{slice_id}
    , col_id_{col_id}
    , num_total_slices_{num_total_slices}
    , num_projs_{num_projs}
    , num_slices_{num_slices}
    , num_cols_{num_cols}
    , num_grids_{num_grids}
    , center_{center}
    , num_rays_proj_{num_slices*num_cols}
    , num_rays_slice_{num_cols}
    , count_{static_cast<size_t>(num_rays_proj_*num_projs_)}
    /* XXX: Number of reconstruction slices are naively set to the 
     * 2*num_neighbor_slices however this should consider top and 
     * bottom slices where there is no neighbors. Since reconstruction 
     * computation is aware of this, it doesn't create a problem. Still, this 
     * needs to be fixed.
     */
    , num_neighbor_recon_slices_{num_neighbor_recon_slices}
    {
      if (theta_ == nullptr) throw std::invalid_argument("theta ptr is null");
      
      // Set the center
      center_ = (center_<=0.) ? static_cast<float>(num_cols_)/2.+1. : center_;

      // Setup grids and mov
      mov_ = static_cast<float>(num_cols_)/2. - center_;
      if(mov_ - std::ceil(mov_) < 1e-6) mov_ += 1e-6;

      gridx_ = new float[num_grids_+1];
      gridy_ = new float[num_grids_+1];
      for(int i=0; i<=num_grids_; i++){
        gridx_[i] = -num_grids_/2. + i;
        gridy_[i] = -num_grids_/2. + i;
      }
    }

    TraceMetadata(
        float const *theta,
        int const proj_id,
        int const slice_id,
        int const col_id,
        int const num_total_slices,
        int const num_projs,
        int const num_slices,
        int const num_cols,
        int const num_grids,
        float center) 
      : TraceMetadata(
          theta, proj_id, slice_id, col_id, num_total_slices,
          num_projs, num_slices, num_cols, num_grids,
          center, 0, 0.) {}

    ~TraceMetadata() {
      delete [] gridx_;
      delete [] gridy_;
    }

    float const * theta() const { return theta_; };

    /**
     * Relative locations of the projection, slice and row
     */
    int RayProjection(int curr_offset) const { return curr_offset/num_rays_proj_; };
    int RaySlice(int curr_offset) const {
      return
        (curr_offset - RayProjection(curr_offset)*num_rays_proj_) / num_rays_slice_;
    };
    int RayColumn(int curr_offset) const {
      int diff =
        RayProjection(curr_offset)*num_rays_proj_ + 
        RaySlice(curr_offset)*num_rays_slice_;
      return (curr_offset - diff) / num_rays_slice_;
    }
    /* End of relative index calculations */

    int slice_id() const { return slice_id_; };
    int proj_id() const { return proj_id_; };
    int col_id() const { return col_id_; };
    int num_total_slices() const { return num_total_slices_; };

    int num_projs() const { return num_projs_; };
    int num_slices() const { return num_slices_; };
    int num_cols() const { return num_cols_; };
    int num_grids() const { return num_grids_; };
    float center() const { return center_; };
    void center(float c) { 
      center_ = c; 
      mov_ = static_cast<float>(num_cols_)/2. - center_;
      if(mov_ - std::ceil(mov_) < 1e-6) mov_ += 1e-6;
    };

    size_t count() const { return count_; };

    float mov() const { return mov_; };
    float const * gridx() const { return gridx_; };
    float const * gridy() const { return gridy_; };

    ADataRegion<float>& recon() const { return *recon_; };
    void recon(ADataRegion<float>& rcn) { 
      //if ((recon_ != &rcn)) { delete recon_; }
      recon_ = &rcn; 
    };
    int num_neighbor_recon_slices() const { return num_neighbor_recon_slices_; };

    void Print()
    {
      std::cout << "Number of projections=" << num_projs_ << std::endl;
      std::cout << "Number of slices=" << num_slices_ << std::endl;
      std::cout << "Number of columns=" << num_cols_ << std::endl;
      std::cout << "Number of grids=" << num_grids_ << std::endl;
      std::cout << "Center=" << center_ << std::endl;
      std::cout << "Mov=" << mov_ << std::endl;
      std::cout << "Number of rays per projection=" << num_rays_proj_ << std::endl;
      std::cout << "Number of rays per slice=" << num_rays_slice_ << std::endl;
      std::cout << "Total number of rays=" << count_ << std::endl;
      std::cout << "Slice id=" << slice_id_ << std::endl;
      std::cout << "Project id=" << proj_id_ << std::endl;
      std::cout << "Column id=" << col_id_ << std::endl;

      if(gridx_ != nullptr) std::cout << "gridx is allocated." << std::endl;
      if(gridy_ != nullptr) std::cout << "gridy is allocated." << std::endl;
      if(recon_ != nullptr) std::cout << "recon is allocated." << std::endl;
      if(theta_ != nullptr) std::cout << "theta is allocated." << std::endl;
    }
};

#endif    // DISP_APPS_RECONSTRUCTION_COMMON_TRACE_H_
