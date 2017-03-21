#include "pml.h"

void PMLReconSpace::UpdateRecon(
    //PMLDataRegion &slices,                      // Input slices, metadata, recon
    ADataRegion<float> &recon,                  // Reconstruction object
    float *F, float *G,
    DataRegion2DBareBase<float> &comb_replica)  // Locally combined replica
{
  //auto &recon = slices.metadata().recon();
  size_t rows = comb_replica.rows();
  size_t cols = comb_replica.cols()/2;

  //float *F = slices.F();
  //float *G = slices.G();

  size_t nans=0;
  for(size_t i=0; i<rows; ++i){
    auto replica = comb_replica[i];
    for(size_t j=0; j<cols; ++j){
      size_t index = (i*cols) + j;
      float upd =  (-G[index] + sqrt(G[index]*G[index] - 8*replica[j*2]*F[index])) /
          (4*F[index]);
      if(std::isnan(upd)) {
        nans++; 
        //std::cout << "NaN value: replica[" << j*2 << "]=" << replica[j*2] <<
        //  " / replica[" << j*2+1 << "]=" << replica[j*2+1]  << 
        //  " = " << replica[j*2]/replica[j*2+1] << std::endl;
        continue;
      } 
      else recon[index] = upd;
    }
  }
  //std::cout << "NaNs=" << nans << std::endl;
}

void PMLReconSpace::CalculateFG(
    PMLDataRegion &slices,
    float beta)
{
  int num_slices = slices.metadata().num_slices();
  int num_grids = slices.metadata().num_grids();

  ADataRegion<float> &recon = slices.metadata().recon();
  float *F = slices.F();
  float *G = slices.G();

  int k, n, m, q, i;
  int ind0, indg[8];

  /// (inner region)
  const float *wg = slices.kWeightIn;
  for (k = 0; k < num_slices; k++) {
    for (n = 1; n < num_grids - 1; n++) {
      for (m = 1; m < num_grids - 1; m++) {
        ind0 = m + n * num_grids + k * num_grids * num_grids;

        indg[0] = ind0 + 1;
        indg[1] = ind0 - 1;
        indg[2] = ind0 + num_grids;
        indg[3] = ind0 - num_grids;
        indg[4] = ind0 + num_grids + 1;
        indg[5] = ind0 + num_grids - 1;
        indg[6] = ind0 - num_grids + 1;
        indg[7] = ind0 - num_grids - 1;

        for (q = 0; q < 8; q++) {
          F[ind0] += 2 * beta * wg[q];
          G[ind0] -= 2 * beta * wg[q] * (recon[ind0] + recon[indg[q]]);
        }
      }
    }
  }

  /// top
  wg = slices.kWeightEdges;
  for (k = 0; k < num_slices; k++) {
    for (m = 1; m < num_grids - 1; m++) {
      ind0 = m + k * num_grids * num_grids;

      indg[0] = ind0 + 1;
      indg[1] = ind0 - 1;
      indg[2] = ind0 + num_grids;
      indg[3] = ind0 + num_grids + 1;
      indg[4] = ind0 + num_grids - 1;

      for (q = 0; q < 5; q++) {
        F[ind0] += 2 * beta * wg[q];
        G[ind0] -= 2 * beta * wg[q] * (recon[ind0] + recon[indg[q]]);
      }
    }
  }

  // (bottom)
  for (k = 0; k < num_slices; k++) {
    for (m = 1; m < num_grids - 1; m++) {
      ind0 = m + (num_grids - 1) * num_grids + k * num_grids * num_grids;

      indg[0] = ind0 + 1;
      indg[1] = ind0 - 1;
      indg[2] = ind0 - num_grids;
      indg[3] = ind0 - num_grids + 1;
      indg[4] = ind0 - num_grids - 1;

      for (q = 0; q < 5; q++) {
        F[ind0] += 2 * beta * wg[q];
        G[ind0] -= 2 * beta * wg[q] * (recon[ind0] + recon[indg[q]]);
      }
    }
  }

  // (left)
  for (k = 0; k < num_slices; k++) {
    for (n = 1; n < num_grids - 1; n++) {
      ind0 = n * num_grids + k * num_grids * num_grids;

      indg[0] = ind0 + 1;
      indg[1] = ind0 + num_grids;
      indg[2] = ind0 - num_grids;
      indg[3] = ind0 + num_grids + 1;
      indg[4] = ind0 - num_grids + 1;

      for (q = 0; q < 5; q++) {
        F[ind0] += 2 * beta * wg[q];
        G[ind0] -= 2 * beta * wg[q] * (recon[ind0] + recon[indg[q]]);
      }
    }
  }

  // (right)
  for (k = 0; k < num_slices; k++) {
    for (n = 1; n < num_grids - 1; n++) {
      ind0 = (num_grids - 1) + n * num_grids + k * num_grids * num_grids;

      indg[0] = ind0 - 1;
      indg[1] = ind0 + num_grids;
      indg[2] = ind0 - num_grids;
      indg[3] = ind0 + num_grids - 1;
      indg[4] = ind0 - num_grids - 1;

      for (q = 0; q < 5; q++) {
        F[ind0] += 2 * beta * wg[q];
        G[ind0] -= 2 * beta * wg[q] * (recon[ind0] + recon[indg[q]]);
      }
    }
  }

  // (top-left)
  wg = slices.kWeightCorners;
  for (k = 0; k < num_slices; k++) {
    ind0 = k * num_grids * num_grids;

    indg[0] = ind0 + 1;
    indg[1] = ind0 + num_grids;
    indg[2] = ind0 + num_grids + 1;

    for (q = 0; q < 3; q++) {
      F[ind0] += 2 * beta * wg[q];
      G[ind0] -= 2 * beta * wg[q] * (recon[ind0] + recon[indg[q]]);
    }
  }

  // (top-right)
  for (k = 0; k < num_slices; k++) {
    ind0 = (num_grids - 1) + k * num_grids * num_grids;

    indg[0] = ind0 - 1;
    indg[1] = ind0 + num_grids;
    indg[2] = ind0 + num_grids - 1;

    for (q = 0; q < 3; q++) {
      F[ind0] += 2 * beta * wg[q];
      G[ind0] -= 2 * beta * wg[q] * (recon[ind0] + recon[indg[q]]);
    }
  }

  // (bottom-left)
  for (k = 0; k < num_slices; k++) {
    ind0 = (num_grids - 1) * num_grids + k * num_grids * num_grids;

    indg[0] = ind0 + 1;
    indg[1] = ind0 - num_grids;
    indg[2] = ind0 - num_grids + 1;

    for (q = 0; q < 3; q++) {
      F[ind0] += 2 * beta * wg[q];
      G[ind0] -= 2 * beta * wg[q] * (recon[ind0] + recon[indg[q]]);
    }
  }

  // (bottom-right)
  for (k = 0; k < num_slices; k++) {
    ind0 = (num_grids - 1) + (num_grids - 1) * num_grids +
           k * num_grids * num_grids;

    indg[0] = ind0 - 1;
    indg[1] = ind0 - num_grids;
    indg[2] = ind0 - num_grids - 1;

    for (q = 0; q < 3; q++) {
      F[ind0] += 2 * beta * wg[q];
      G[ind0] -= 2 * beta * wg[q] * (recon[ind0] + recon[indg[q]]);
    }
  }

  int count = num_grids*num_grids;
  for (i=0; i<num_slices; i++) {
    float *suma = &reduction_objects()[i][0];
    for (int j=0; j<count; j++) {
      G[i*count+j] += suma[j*2+1];
    }
  }
}

/// Forward Projection
float PMLReconSpace::CalculateSimdata(
    float *recon,
    int len,
    int *indi,
    float *leng)
{
  float simdata = 0.;
  for(int i=0; i<len-1; ++i){
#ifdef PREFETCHON
    size_t index = indi[i+32];
    __builtin_prefetch(&(recon[index]),1,0);
#endif
    simdata += recon[indi[i]]*leng[i];
  }
  return simdata;
}


void PMLReconSpace::UpdateReconReplica(
    float simdata,
    float ray,
    float *recon,
    int curr_slice,
    int const * const indi,
    float *leng, 
    int len)
{
  auto &slice_t = reduction_objects()[curr_slice];
  auto slice = &slice_t[0];

  float upd = ray/simdata;
  for (int i=0; i <len-1; ++i) {
#ifdef PREFETCHON
    size_t indi2 = indi[i+32];
    size_t index2 = indi2*2;
    __builtin_prefetch(recon+indi2,1,0);
    __builtin_prefetch(slice+index2,1,0);
#endif
    size_t index = indi[i]*2;
    slice[index] -= recon[indi[i]]*leng[i]*upd;
    slice[index+1] += leng[i];
  }
}

void PMLReconSpace::Initialize(int n_grids){
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

void PMLReconSpace::Finalize(){
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

void PMLReconSpace::Reduce(MirroredRegionBareBase<float> &input)
{
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

  /* Reconstruction start */
  for (int proj = curr_proj; proj<=(curr_proj+count_projs); ++proj) {
    float theta_q = theta[proj];
    int quadrant = trace_utils::CalculateQuadrant(theta_q);
    float sinq = sinf(theta_q);
    float cosq = cosf(theta_q);

    int curr_slice = metadata.RaySlice(rays.index());
    int curr_slice_offset = curr_slice*num_grids*num_grids;
    float *recon = (&(metadata.recon()[0])+curr_slice_offset);

    for (int curr_col=0; curr_col<num_cols; ++curr_col) {
      /// Calculate coordinates
      float xi = -1e6;
      float yi = (1-num_cols)/2. + curr_col+mov;
      trace_utils::CalculateCoordinates(
          num_grids, 
          xi, yi, sinq, cosq, 
          gridx, gridy, 
          coordx, coordy);  /// Outputs coordx and coordy

      /// Merge the (coordx, gridy) and (gridx, coordy)
      /// Output alen and after
      int alen, blen;
      trace_utils::MergeTrimCoordinates(
          num_grids, 
          coordx, coordy, 
          gridx, gridy, 
          &alen, &blen, 
          ax, ay, bx, by);

      /// Sort the array of intersection points (ax, ay)
      /// The new sorted intersection points are
      /// stored in (coorx, coory).
      /// if quadrant=1 then a_ind = i; if 0 then a_ind = (alen-1-i)
      trace_utils::SortIntersectionPoints(
          quadrant, 
          alen, blen, 
          ax, ay, bx, by, 
          coorx, coory);

      /// Calculate the distances (leng) between the
      /// intersection points (coorx, coory). Find
      /// the indices of the pixels on the
      /// reconstruction grid (ind_recon).
      int len = alen + blen;
      trace_utils::CalculateDistanceLengths(
          len, 
          num_grids, 
          coorx, coory, 
          leng,
          indi);

      /*******************************************************/
      /* Below is for updating the reconstruction grid and
       * is algorithm specific part.
       */
      /// Forward projection
      float simdata = CalculateSimdata(recon, len, indi, leng);

      /// Update recon
      UpdateReconReplica(
          simdata, 
          rays[curr_col], 
          recon, 
          curr_slice, 
          indi, 
          leng,
          len);
      /*******************************************************/
    }
  }
}
