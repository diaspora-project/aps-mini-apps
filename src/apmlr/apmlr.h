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

class APMLRDataRegion : public DataRegionBase<float, TraceMetadata>{
  private:
    float *F_ = nullptr;
    float *G_ = nullptr;

  public:
    APMLRDataRegion(
        float *data, 
        size_t count, 
        TraceMetadata *metadata): 
      DataRegionBase<float, TraceMetadata>(data, count, metadata) 
    {
      size_t count_i = 
        metadata->num_slices()*metadata->num_grids()*metadata->num_grids();
      F_ = new float[count_i];
      G_ = new float[count_i];
      SetFG(0.);
    }

    virtual ~APMLRDataRegion(){
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

class APMLRReconSpace : 
  public AReductionSpaceBase<APMLRReconSpace, float>
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

    void CalculateFGInner(
        float *recon, float *F, float *G,
        float beta, float beta1, float delta, float delta1, float regw,
        int num_slices, int num_grids);
    void CalculateFGTop(
        float * recon, float *F, float *G,
        float beta, float beta1, float delta, float delta1, float regw, 
        int num_grids);
    void CalculateFGBottom(
        float * recon, float *F, float *G,
        float beta, float beta1, float delta, float delta1, float regw, 
        int num_grids);



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
        int len);


  public:
    APMLRReconSpace(int rows, int cols) : 
      AReductionSpaceBase<APMLRReconSpace, float>(rows, cols) {}

    virtual ~APMLRReconSpace(){
      Finalize();
    }

    void UpdateRecon(
        APMLRDataRegion &slices, // Reconstruction object
        DataRegion2DBareBase<float> &comb_replica); // Locally combined replica

    void CalculateFG(
        APMLRDataRegion &slices,
        float beta, float beta1,
        float delta, float delta1,
        float regw);

    void Reduce(MirroredRegionBareBase<float> &input);

    void Initialize(int n_grids);
    virtual void CopyTo(APMLRReconSpace &target){
      target.Initialize(num_grids);
    }
    void Finalize();
};

#endif    // DISP_APPS_RECONSTRUCTION_MLEM_MLEM_H
