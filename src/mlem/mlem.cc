#include "mlem.h"

void MLEMReconSpace::UpdateRecon(
    ADataRegion<float> &recon,                  // Reconstruction object
    DataRegion2DBareBase<float> &comb_replica)  // Locally combined replica
{
  size_t rows = comb_replica.rows();
  size_t cols = comb_replica.cols()/2;
  for(size_t i=0; i<rows; ++i){
    auto replica = comb_replica[i];
    for(size_t j=0; j<cols; ++j)
      recon[i*cols + j] *=
        replica[j] / replica[cols+j];
  }
}

/// Forward Projection
float MLEMReconSpace::CalculateSimdata(
    float *recon,
    int len,
    int *indi,
    float *leng)
{
  float simdata = 0.;
  for(int i=0; i<len-1; ++i)
    simdata += recon[indi[i]]*leng[i];
  return simdata;
}

void MLEMReconSpace::UpdateReconReplica(
    float simdata,
    float ray,
    int curr_slice,
    int const * const indi,
    float *leng, 
    int len,
    int suma_beg_offset)
{
  float upd;

  auto &slice_t = reduction_objects()[curr_slice];
  auto slice = &slice_t[0] + suma_beg_offset;

  start_replica = std::chrono::system_clock::now();
  for (int i=0; i<len-1; ++i) {
    if (indi[i] >= suma_beg_offset) continue;
    slice[indi[i]] += leng[i];
  }
  slice -= suma_beg_offset;
  end_replica = std::chrono::system_clock::now();
  timer_update_replica_loop1 += end_replica-start_replica;

  upd = (ray/simdata);
  start_replica = std::chrono::system_clock::now();
  for (int i=0; i <len-1; ++i) {
    if (indi[i] >= suma_beg_offset) continue;
    slice[indi[i]] += leng[i]*upd;
  }
  end_replica = std::chrono::system_clock::now();
  timer_update_replica_loop2 += end_replica-start_replica;
}


void MLEMReconSpace::Initialize(int n_grids){
  num_grids = n_grids; 

  coordx = new float[num_grids+1]; 
  coordy = new float[num_grids+1];
  ax = new float[num_grids+1];
  ay = new float[num_grids+1];
  bx = new float[num_grids+1];
  by = new float[num_grids+1];
  coorx = new float[2*num_grids];
  coory = new float[2*num_grids];
  leng = new float[2*num_grids];
  indi = new int[2*num_grids];
}

void MLEMReconSpace::Finalize(){
  delete [] coordx;
  delete [] coordy;
  delete [] ax;
  delete [] ay;
  delete [] bx;
  delete [] by;
  delete [] coorx;
  delete [] coory;
  delete [] leng;
  delete [] indi;
}

/**********************/
/* Execution Profiler */
void MLEMReconSpace::PrintProfileInfo(){
  std::cout << 
    "Total time: " << timer_all.count() << std::endl <<
    "--Coordinate calculation: " << timer_coordinates.count() << std::endl <<
    "--Sort intersections: " << timer_sort_int.count() << std::endl <<
    "--Merge trim coordinates: " << timer_merge_trim.count() << std::endl <<
    "--Distance calculation: " << timer_dist.count() << std::endl <<
    "--Simdata calculation: " << timer_simdata.count() << std::endl <<
    "--Updating replica: " << timer_update_replica.count() << std::endl <<
    "---Updating replica loop1: " << timer_update_replica_loop1.count() <<  std::endl <<
    "---Updating replica loop2: " << timer_update_replica_loop2.count() << std::endl;
}

/**********************/

void MLEMReconSpace::Reduce(MirroredRegionBareBase<float> &input)
{
  start_all = std::chrono::system_clock::now();

  auto &rays = *(static_cast<MirroredRegionBase<float, TraceMetadata>*>(&input));
  auto &metadata = rays.metadata();

  const float *theta = metadata.theta();
  const float *gridx = metadata.gridx();
  const float *gridy = metadata.gridy();
  float mov = metadata.mov();

  /* In-memory values */
  int num_cols = metadata.num_cols();
  int num_grids = metadata.num_cols();

  int curr_proj = metadata.RayProjection(rays.index());
  int count_projs = 
    metadata.RayProjection(rays.index()+rays.count()-1) - curr_proj;

  /*
     int rank;
     MPI_Comm_rank(MPI_COMM_WORLD, &rank); 
     if(rank==0){
     std::cout << "Ray index=" << rays.index() << std::endl;
     std::cout << "Ray count=" << rays.count() << std::endl;
     std::cout << "Beginning projection=" << curr_proj << std::endl;
     std::cout << "Number of projection=" << count_projs << std::endl;
     }
     */

  /* Reconstruction start */
  for (int proj = curr_proj; proj<=(curr_proj+count_projs); ++proj) {
    float theta_q = theta[proj];
    int quadrant = trace_utils::CalculateQuadrant(theta_q);
    float sinq = sinf(theta_q);
    float cosq = cosf(theta_q);

    int curr_slice = metadata.RaySlice(rays.index());
    int curr_slice_offset = curr_slice*num_grids*num_grids;
    float *recon = (&(metadata.recon()[0])+curr_slice_offset);

    /*
       if(rank==0){
       std::cout << "Current projection=" << proj << std::endl;
       std::cout << "Current projection slice=" << curr_slice << std::endl;
       std::cout << "Current slice offset=" << curr_slice_offset << std::endl;
       std::cout << std::endl;
       }
       */

    for (int curr_col=0; curr_col<num_cols; ++curr_col) {
      /// Calculate coordinates
      float xi = -1e6;
      float yi = (1-num_cols)/2. + curr_col+mov;
      start = std::chrono::system_clock::now();
      trace_utils::CalculateCoordinates(
          num_grids, 
          xi, yi, sinq, cosq, 
          gridx, gridy, 
          coordx, coordy);  /// Outputs coordx and coordy
      end = std::chrono::system_clock::now();
      timer_coordinates += end-start;

      /// Merge the (coordx, gridy) and (gridx, coordy)
      /// Output alen and after
      int alen, blen;
      start = std::chrono::system_clock::now();
      trace_utils::MergeTrimCoordinates(
          num_grids, 
          coordx, coordy, 
          gridx, gridy, 
          &alen, &blen, 
          ax, ay, bx, by);
      end = std::chrono::system_clock::now();
      timer_merge_trim += end-start;

      /// Sort the array of intersection points (ax, ay)
      /// The new sorted intersection points are
      /// stored in (coorx, coory).
      /// if quadrant=1 then a_ind = i; if 0 then a_ind = (alen-1-i)
      start = std::chrono::system_clock::now();
      trace_utils::SortIntersectionPoints(
          quadrant, 
          alen, blen, 
          ax, ay, bx, by, 
          coorx, coory);
      end = std::chrono::system_clock::now();
      timer_sort_int += end-start;

      /// Calculate the distances (leng) between the
      /// intersection points (coorx, coory). Find
      /// the indices of the pixels on the
      /// reconstruction grid (ind_recon).
      int len = alen + blen;
      start = std::chrono::system_clock::now();
      trace_utils::CalculateDistanceLengths(
          len, 
          num_grids, 
          coorx, coory, 
          leng,
          indi);
      end = std::chrono::system_clock::now();
      timer_dist += end-start;

      /*******************************************************/
      /* Below is for updating the reconstruction grid and
       * is algorithm specific part.
       */
      /// Forward projection
      start = std::chrono::system_clock::now();
      float simdata = CalculateSimdata(recon, len, indi, leng);
      end = std::chrono::system_clock::now();
      timer_simdata += end-start;

      /// Update recon 
      start = std::chrono::system_clock::now();
      int suma_beg_offset = num_grids*num_grids;
      UpdateReconReplica(
          simdata, 
          rays[curr_col], 
          curr_slice, 
          indi, 
          leng,
          len, 
          suma_beg_offset);
      end = std::chrono::system_clock::now();
      timer_update_replica += end-start;
      /*******************************************************/
    }
  }
  end_all = std::chrono::system_clock::now();
  timer_all += end_all-start_all;
}
