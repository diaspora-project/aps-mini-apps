#ifndef DISP_APPS_RECONSTRUCTION_COMMON_TRACE_UTILS_H_
#define DISP_APPS_RECONSTRUCTION_COMMON_TRACE_UTILS_H_

#include <cmath>
#include "trace_h5io.h"
#include "data_region_2d_bare_base.h"

namespace trace_utils {
  constexpr float kPI = 3.14159265358979f;

  void DegreeToRadian(trace_io::H5Data &theta)
  {
    int num_elem = theta.metadata->dims[0];
    float *buf = new float[num_elem];
    for(int i=0; i<num_elem; ++i) buf[i] = 0.;

    if(buf==NULL)
      throw std::invalid_argument("Unable to allocate buffer."); 

    for(int i=0; i<num_elem; ++i)
      buf[i] = static_cast<float*>(theta.data)[i]*kPI/180.0;

    free(theta.data);
    theta.data = buf;
  }

  // Backward projection
  void UpdateRecon(
      ADataRegion<float> &recon,                  // Reconstruction object
      DataRegion2DBareBase<float> &comb_replica)  // Locally combined replica
  {
    size_t rows = comb_replica.rows();
    size_t cols = comb_replica.cols()/2;
    for(size_t i=0; i<rows; ++i){
      auto replica = comb_replica[i];
      for(size_t j=0; j<cols; ++j)
        recon[i*cols + j] += 
          replica[j] / replica[cols+j];
    }
  }

  int CalculateQuadrant(float theta_q)
  {
    return
      ((theta_q >= 0 && theta_q < kPI/2) || 
       (theta_q >= kPI && theta_q < 3*kPI/2)) ? 1 : 0;
  }

  void CalculateCoordinates(
      int num_grid,
      float xi, float yi, float sinq, float cosq,
      const float *gridx, const float *gridy, 
      float *coordx, float *coordy)
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
  }

  void MergeTrimCoordinates(
      int num_grid,
      float *coordx, float *coordy,
      const float *gridx, const float *gridy,
      int *alen, int *blen,
      float *ax, float *ay,
      float *bx, float *by)
  {
    /* Merge the (coordx, gridy) and (gridx, coordy)
     * on a single array of points (ax, ay) and trim
     * the coordinates that are outside the
     * reconstruction grid. 
     */
    *alen = 0;
    *blen = 0;
    for (int i = 0; i <= num_grid; ++i) {
      if (coordx[i] > gridx[0]) {
        if (coordx[i] < gridx[num_grid]) {
          ax[*alen] = coordx[i];
          ay[*alen] = gridy[i];
          (*alen)++;
        }
      }
      if (coordy[i] > gridy[0]) {
        if (coordy[i] < gridy[num_grid]) {
          bx[*blen] = gridx[i];
          by[*blen] = coordy[i];
          (*blen)++;
        }
      }
    }
  }

  void SortIntersectionPoints(
      int ind_cond,
      int alen, int blen,
      float *ax, float *ay,
      float *bx, float *by,
      float *coorx, float *coory)
  {
    int i=0, j=0, k=0;
    int a_ind;
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

  void CalculateDistanceLengths(
      int len, int num_grids,
      float *coorx, float *coory, 
      float *leng, float *leng2, int *indi)
  {
    int x1, x2, i1, i2;
    float diffx, diffy, midx, midy;
    int indx, indy;

    for (int i = 0; i<len-1; ++i) {
      diffx = coorx[i+1] - coorx[i];
      diffy = coory[i+1] - coory[i];
      leng2[i] = diffx*diffx + diffy*diffy;
      leng[i] = sqrt(leng2[i]);

      /* Calculate the recon locations */
      midx = (coorx[i+1] + coorx[i])/2.;
      /* XXX
       * In some calculations midy value points to the boundary,
       * this situation results in segmentation faults. coory and coorx
       * calculation, therefore, need to be revised.
       */
      midy = (coory[i+1] + coory[i])/2.;

      x1 = midx + num_grids/2.;
      x2 = midy + num_grids/2.;
      i1 = (int)(midx + num_grids/2.);
      i2 = (int)(midy + num_grids/2.);
      indx = i1 - (i1 > x1);
      indy = i2 - (i2 > x2);
      indi[i] = indx+(indy*num_grids);

      /* Below is kept for debugging purposes 
       * For num_gridxnum_grid slice area, it catches the exceeding indices.
        if(indi[n]>num_grid*num_grid){
        printf("indi[%d]=%d; indx=%d; indy=%d; i1=%d; x1=%d; i2=%d; x2=%d;"
        "midx=%.5f; midy=%.5f; coorx[%d+1]=%.5f; coorx=[%d]=%.5f; "
        "coory[%d+1]=%.5f; coory[%d]=%.5f; leng[%d]=%.5f; "
        "leng2[%d]=%.5f\n",
        n, indi[n], indx, indy, i1, x1, i2, x2, midx, midy, 
        n, coorx[n+1], n, coorx[n],   
        n, coory[n+1], n, coory[n], 
        n, leng[n], n, leng2[n]);
        }
       */
    }
  }

}
#endif /// DISP_APPS_RECONSTRUCTION_COMMON_TRACE_UTILS_H_
