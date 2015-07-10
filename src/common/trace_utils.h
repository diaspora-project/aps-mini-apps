#ifndef DISP_APPS_RECONSTRUCTION_COMMON_TRACE_UTILS_H_
#define DISP_APPS_RECONSTRUCTION_COMMON_TRACE_UTILS_H_

#include "trace_h5io.h"
#include "data_region_2d_bare_base.h"

namespace trace_utils {
  constexpr float kPI = 3.14159265358979f;

  void DegreeToRadian(trace_io::H5Data &theta);
  void Absolute(float *data, size_t count);

  // Backward projection
  void UpdateRecon(
      ADataRegion<float> &recon,                  // Reconstruction object
      DataRegion2DBareBase<float> &comb_replica); // Locally combined replica

  int CalculateQuadrant(float theta_q);

  void CalculateCoordinates(
      int num_grid,
      float xi, float yi, float sinq, float cosq,
      const float *gridx, const float *gridy, 
      float *coordx, float *coordy);

  void MergeTrimCoordinates(
      int num_grid,
      float *coordx, float *coordy,
      const float *gridx, const float *gridy,
      int *alen, int *blen,
      float *ax, float *ay,
      float *bx, float *by);

  void SortIntersectionPoints(
      int ind_cond,
      int alen, int blen,
      float *ax, float *ay,
      float *bx, float *by,
      float *coorx, float *coory);

  void CalculateDistanceLengths(
      int len, int num_grids,
      float *coorx, float *coory, 
      float *leng, float *leng2, int *indi);

  void CalculateDistanceLengths(
      int len, int num_grids,
      float *coorx, float *coory, 
      float *leng, int *indi);
}
#endif /// DISP_APPS_RECONSTRUCTION_COMMON_TRACE_UTILS_H_
