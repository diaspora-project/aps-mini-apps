#ifndef DISP_APPS_RECONSTRUCTION_PML_PML_H
#define DISP_APPS_RECONSTRUCTION_PML_PML_H

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

class PMLDataRegion : public DataRegionBase<float, TraceMetadata>{
  private:
    float *F_ = nullptr;
    float *G_ = nullptr;

  public:
    const float kWeightIn[8] {
      0.1464466094, 0.1464466094, 0.1464466094, 0.1464466094,
        0.10355339059, 0.10355339059, 0.10355339059, 0.10355339059 };
    const float kWeightEdges[5] {
      0.226540919667, 0.226540919667, 0.226540919667,
        0.1601886205, 0.1601886205 };
    const float kWeightCorners[3] {
      0.36939806251, 0.36939806251, 0.26120387496 };

    PMLDataRegion(
        float *data, 
        size_t count, 
        TraceMetadata *metadata): 
      DataRegionBase(data, count, metadata) 
    {
      size_t count_i = 
        metadata->num_slices()*metadata->num_grids()*metadata->num_grids();
      F_ = new float[count_i];
      G_ = new float[count_i];
      SetFG(0.);
    }

    virtual ~PMLDataRegion(){
      delete [] F_;
      delete [] G_;
    }

    void SetFG(float val)
    {
      size_t count = 
        metadata().num_slices()*metadata().num_grids()*metadata().num_grids();
      for(size_t i=0; i<count; ++i){
        F_[i]=val;
        G_[i]=val;
      }
    }

    float* F() const { return F_; };
    float* G() const { return G_; };
};

class PMLReconSpace : 
  public AReductionSpaceBase<PMLReconSpace, float>
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
        float *recon,
        int curr_slice,
        int const * const indi,
        float *leng, 
        int len,
        int suma_beg_offset);


  public:
    PMLReconSpace(int rows, int cols) : 
      AReductionSpaceBase(rows, cols) {}

    virtual ~PMLReconSpace(){
      Finalize();
    }

    void UpdateRecon(
        PMLDataRegion &slices, // Reconstruction object
        DataRegion2DBareBase<float> &comb_replica); // Locally combined replica

    void CalculateFG(
        PMLDataRegion &slices,
        float beta);

    void Reduce(MirroredRegionBareBase<float> &input);

    void Initialize(int n_grids);
    virtual void CopyTo(PMLReconSpace &target){
      target.Initialize(num_grids);
    }
    void Finalize();
};

#endif    // DISP_APPS_RECONSTRUCTION_PML_PML_H
