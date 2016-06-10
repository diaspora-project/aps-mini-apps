#ifndef DISP_APPS_RECONSTRUCTION_MLEM_MLEM_H
#define DISP_APPS_RECONSTRUCTION_MLEM_MLEM_H

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>
#include <chrono>
#include "hdf5.h"
#include "string.h"
#include "trace_data.h"
#include "trace_utils.h"
#include "reduction_space_a.h"
#include "data_region_base.h"

class MLEMReconSpace : 
  public AReductionSpaceBase<MLEMReconSpace, float>
{
  private:
    float *coordx = nullptr;
    float *coordy = nullptr;
    float *ax = nullptr;
    float *ay = nullptr;
    float *bx = nullptr;
    float *by = nullptr;
    float *coorx = nullptr;
    float *coory = nullptr;
    float *leng = nullptr;
    int *indi = nullptr;

    int num_grids;

  protected:
    // Forward projection
    float CalculateSimdata(
        float *recon,
        int len,
        int *indi,
        float *leng);

    void UpdateReconReplica(
        float simdata,
        float ray,
        int curr_slice,
        int const * const indi,
        float *leng, 
        int len);

  public:
    MLEMReconSpace(int rows, int cols) : 
      AReductionSpaceBase(rows, cols) {}

    virtual ~MLEMReconSpace(){
      Finalize();
    }

    void UpdateRecon(
      ADataRegion<float> &recon,                  // Reconstruction object
      DataRegion2DBareBase<float> &comb_replica); // Locally combined replica

    void Reduce(MirroredRegionBareBase<float> &input);

    void Initialize(int n_grids);
    virtual void CopyTo(MLEMReconSpace &target){
      target.Initialize(num_grids);
    }
    void Finalize();
};

#endif    // DISP_APPS_RECONSTRUCTION_MLEM_MLEM_H
