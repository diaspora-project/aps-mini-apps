/** \file data_region_bare_base.h 
 */
#ifndef DISP_SRC_DISP_DATA_REGION_BARE_BASE_H
#define DISP_SRC_DISP_DATA_REGION_BARE_BASE_H

/**
 * \class DataRegionBareBase
 * \brief Basic implementation of DataRegionA class.
 *
 * Contact: bicer@anl.gov
 */

#include <iostream>
#include <vector>
#include "data_region_a.h"
#include "mirrored_region_bare_base.h"
#include <boost/serialization/access.hpp>
#include <boost/serialization/vector.hpp>

template <typename T> 
class DataRegionBareBase : public ADataRegion<T> {
  private:
    size_t size_;
    std::vector<T> data_;

    friend class boost::serialization::access;

    template<class Archive>
    void serialize(Archive &ar, const unsigned int /*version*/) { // Mark version as unused
        ar & boost::serialization::base_object<ADataRegion<T>>(*this);
        ar & size_;
        ar & data_;
        // ar & boost::serialization::make_array(data_.data(), size_); // Serialize raw array
    }

  protected:
    virtual MirroredRegionBareBase<T>* MirrorRegion(const size_t index, const size_t count)
    {
      if(index+count > ADataRegion<T>::count()) 
        throw std::out_of_range("Mirrored region is out of range!");

      MirroredRegionBareBase<T> *mirrored_region = 
        new MirroredRegionBareBase<T>(this, &ADataRegion<T>::operator[](0)+index, count, index);
      ADataRegion<T>::mirrored_regions_.push_back(mirrored_region);

      return mirrored_region;
    }

  public:

    DataRegionBareBase() : DataRegionBareBase<T>(0) {}

    virtual ADataRegion<T>* Clone(){
      return new DataRegionBareBase<T>(*this);
    };

    // Constructors
    explicit DataRegionBareBase(const size_t count)
      : ADataRegion<T>(count), size_(count), data_(count) // Reordered initialization
    {}

    explicit DataRegionBareBase(T * const data, const size_t count)
      : ADataRegion<T>(data, count), size_(count), data_(data, data + count) // Reordered initialization
    {}

    DataRegionBareBase(const ADataRegion<T> &region)
      : ADataRegion<T>(region), size_(region.count()), data_(region.begin(), region.end()) // Reordered initialization
    {}
    DataRegionBareBase(const ADataRegion<T> &&region)
      : ADataRegion<T>(std::move(region)), size_(region.count()), data_(region.begin(), region.end()) // Reordered initialization
    {}

    // Assignments
    DataRegionBareBase<T>& operator=(const ADataRegion<T> &region)
    {
      ADataRegion<T>::operator=(region);
      data_.assign(region.begin(), region.end());
      size_ = region.count();
    }
    DataRegionBareBase<T>& operator=(const ADataRegion<T> &&region)
    {
      ADataRegion<T>::operator=(std::move(region));
      data_.assign(region.begin(), region.end());
      size_ = region.count();
    }

    virtual MirroredRegionBareBase<T>* NextMirroredRegion(const size_t count)
    {
      if(ADataRegion<T>::index_ >= ADataRegion<T>::count()) 
        return nullptr;

      size_t num_units = 
        (ADataRegion<T>::index_+count > ADataRegion<T>::count()) ? 
          (ADataRegion<T>::count() - ADataRegion<T>::index_) : count;

      auto region = MirrorRegion(ADataRegion<T>::index_, num_units);
      ADataRegion<T>::index_ += num_units;

      return region;
    }

    size_t size() const { return size_; }
    T& operator[](size_t index) { return data_[index]; }
    const T& operator[](size_t index) const { return data_[index]; }
};

#endif    // DISP_SRC_DISP_DATA_REGION_BARE_BASE_H
