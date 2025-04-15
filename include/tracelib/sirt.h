#ifndef DISP_APPS_RECONSTRUCTION_SIRT_SIRT_H
#define DISP_APPS_RECONSTRUCTION_SIRT_SIRT_H

#include <boost/serialization/access.hpp>
#include <boost/serialization/base_object.hpp>
#include <boost/serialization/array.hpp>
#include <boost/serialization/export.hpp>
#include "reduction_space_a.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>
#include <chrono>
#include "hdf5.h"
#include "string.h"
#include "trace_data.h"
#include "trace_utils.h"
#include "data_region_base.h"

class SIRTReconSpace : public AReductionSpaceBase<SIRTReconSpace, float> {
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

    int num_grids = 0;

    friend class boost::serialization::access;

    template<class Archive>
    void serialize(Archive &ar, const unsigned int /*version*/) {
        ar & boost::serialization::base_object<AReductionSpaceBase<SIRTReconSpace, float>>(*this);
        ar & num_grids;

        if (Archive::is_loading::value) {
            // Allocate memory for arrays during deserialization
            Finalize(); // Clean up any existing memory
            coordx = new float[num_grids + 1];
            coordy = new float[num_grids + 1];
            ax = new float[num_grids + 1];
            ay = new float[num_grids + 1];
            bx = new float[num_grids + 1];
            by = new float[num_grids + 1];
            coorx = new float[2 * num_grids];
            coory = new float[2 * num_grids];
            leng = new float[2 * num_grids];
            leng2 = new float[2 * num_grids];
            indi = new int[2 * num_grids];
        }

        // Serialize/deserialize arrays
        ar & boost::serialization::make_array(coordx, num_grids + 1);
        ar & boost::serialization::make_array(coordy, num_grids + 1);
        ar & boost::serialization::make_array(ax, num_grids + 1);
        ar & boost::serialization::make_array(ay, num_grids + 1);
        ar & boost::serialization::make_array(bx, num_grids + 1);
        ar & boost::serialization::make_array(by, num_grids + 1);
        ar & boost::serialization::make_array(coorx, 2 * num_grids);
        ar & boost::serialization::make_array(coory, 2 * num_grids);
        ar & boost::serialization::make_array(leng, 2 * num_grids);
        ar & boost::serialization::make_array(leng2, 2 * num_grids);
        ar & boost::serialization::make_array(indi, 2 * num_grids);
    }

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
        int len);

  public:
    SIRTReconSpace() : AReductionSpaceBase<SIRTReconSpace, float>(0, 0) {} // Default constructor

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
    void Finalize() {
        delete[] coordx;
        delete[] coordy;
        delete[] ax;
        delete[] ay;
        delete[] bx;
        delete[] by;
        delete[] coorx;
        delete[] coory;
        delete[] leng;
        delete[] leng2;
        delete[] indi;

        coordx = coordy = ax = ay = bx = by = coorx = coory = leng = leng2 = nullptr;
        indi = nullptr;
    }

    virtual SIRTReconSpace *Clone() override {
      auto &red_objs = this->reduction_objects();

      SIRTReconSpace *cloned_obj = new SIRTReconSpace(red_objs.rows(), red_objs.cols());
      cloned_obj->Initialize(num_grids); // Ensure Initialize is called
      cloned_obj->reduction_objects() = red_objs;

      this->CopyTo(*cloned_obj);

      return cloned_obj;
    }
};

// Register the derived class
BOOST_CLASS_EXPORT_KEY(SIRTReconSpace)

#endif    // DISP_APPS_RECONSTRUCTION_SIRT_SIRT_H
