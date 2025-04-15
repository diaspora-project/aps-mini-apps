#ifndef _REDUCTION_SPACE_BASE_A_
#define _REDUCTION_SPACE_BASE_A_

#include "data_region_2d_bare_base.h"
#include "mirrored_region_bare_base.h"
#include <boost/serialization/access.hpp>
#include <boost/serialization/base_object.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/export.hpp>

template <typename CT, typename DT>
class AReductionSpaceBase {
  private:
    DataRegion2DBareBase<DT> *reduction_objects_ = nullptr;

    friend class boost::serialization::access;

    template<class Archive>
    void serialize(Archive &ar, const unsigned int /*version*/) {
        ar & reduction_objects_;
    }

  public:
    void Process(MirroredRegionBareBase<DT> &input) {
      static_cast<CT*>(this)->Reduce(input);
    };

    // Default operation is sum
    virtual void LocalSynchWith(CT &input_reduction_space) {
      auto &ri = input_reduction_space.reduction_objects();
      auto &ro = *reduction_objects_;

      if(ri.num_rows()!=ro.num_rows() || ri.num_cols()!=ro.num_cols())
        throw std::range_error("Local and destination reduction objects have different dimension sizes!");

      for(size_t i=0; i<ro.num_rows(); i++)
        for(size_t j=0; j<ro.num_cols(); j++)
          ro[i][j] += ri[i][j];
    };


    // Derived class can use this function to perform
    // deep copies
    virtual void CopyTo(CT &target)=0;

    virtual CT *Clone() {
      if (reduction_objects_ == nullptr)
        throw std::invalid_argument("reduction objects point to nullptr!");

      auto &red_objs = *reduction_objects_;

      CT *cloned_obj = new CT(red_objs.rows(), red_objs.cols());
      (*cloned_obj).reduction_objects() = red_objs;

      static_cast<CT*>(this)->CopyTo(*cloned_obj);

      return cloned_obj;
    };

    DataRegion2DBareBase<DT>& reduction_objects() {
      return *reduction_objects_; 
    };

    DataRegionBareBase<DT>& operator[](int row){ 
      return (*reduction_objects_)[row]; 
    };

    AReductionSpaceBase(DataRegion2DBareBase<DT> *reduction_objects) :
      reduction_objects_(reduction_objects) {
    };

    AReductionSpaceBase(size_t rows, size_t cols){
      reduction_objects_ = new DataRegion2DBareBase<DT>(rows, cols);
    };

    /// Make sure to call derived class (CT) destructor 
    /// instead of base class (this one) destructor 
    virtual ~AReductionSpaceBase(){ 
      delete reduction_objects_; 
    };
};
#endif
