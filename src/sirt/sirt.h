#ifndef DISP_APPS_RECONSTRUCTION_SIRT_SIRT_H
#define DISP_APPS_RECONSTRUCTION_SIRT_SIRT_H

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

class SIRTReconSpace : 
  public AReductionSpaceBase<SIRTReconSpace, float>
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
    float *leng2 = nullptr;
    int *indi = nullptr;

    float *temp_buf = nullptr;

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
        float *leng2,
        float *leng, 
        int len,
        int suma_beg_offset);

  public:
    SIRTReconSpace(int rows, int cols) : 
      AReductionSpaceBase<SIRTReconSpace, float>(rows, cols) {}

    virtual ~SIRTReconSpace(){
      Finalize();
    }

    void Reduce(MirroredRegionBareBase<float> &input);
    // Backward Projection
    void UpdateRecon(
        ADataRegion<float> &recon,                  // Reconstruction object
        DataRegion2DBareBase<float> &comb_replica); // Locally combined replica


    void Initialize(int n_grids);
    virtual void CopyTo(SIRTReconSpace &target){
      target.Initialize(num_grids);
    }
    void Finalize();
};

#endif    // DISP_APPS_RECONSTRUCTION_SIRT_SIRT_H
