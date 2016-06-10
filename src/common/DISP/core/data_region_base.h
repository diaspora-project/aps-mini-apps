/** \file data_region_base.h 
 */
#ifndef DISP_SRC_DISP_DATA_REGION_BASE_H
#define DISP_SRC_DISP_DATA_REGION_BASE_H

/**
 * \class DataRegionBase
 * \brief Implementation of abstract DataRegion class with metadata data structure.
 *
 * Contact: bicer@anl.gov
 */

#include <iostream>
#include <vector>
#include "data_region_a.h"
#include "mirrored_region_base.h"

template <typename T, typename I> 
class DataRegionBase : public ADataRegion<T> {
  protected:
    I *metadata_ = nullptr;

    virtual MirroredRegionBase<T, I>* MirrorRegion(const size_t index, const size_t count)
    {
      if(index+count > ADataRegion<T>::count()) 
        throw std::out_of_range("Mirrored region is out of range!");

      MirroredRegionBase<T, I> *mirrored_region = 
        new MirroredRegionBase<T, I>(
            this, 
            &ADataRegion<T>::operator[](0)+index, 
            count, 
            index, 
            metadata_);
      ADataRegion<T>::mirrored_regions_.push_back(mirrored_region);

      return mirrored_region;
    }

  public:

    /* Constructors */
    explicit DataRegionBase(const size_t count, I * const metadata)
      : ADataRegion<T>(count)
      , metadata_{metadata}
    {}

    explicit DataRegionBase(T * const data, const size_t count, I * const metadata)
      : ADataRegion<T>(data, count)
      , metadata_{metadata}
    {}

    DataRegionBase(const ADataRegion<T> &region)
      : ADataRegion<T>(region)
    {
      DataRegionBase<T, I> *dr = dynamic_cast<DataRegionBase<T, I>*>(&region);
      if(dr != nullptr) metadata_ = region.metadata_;
    }

    DataRegionBase(const ADataRegion<T> &&region)
      : ADataRegion<T>(std::move(region))
    {
      DataRegionBase<T, I> *dr = dynamic_cast<DataRegionBase<T, I>*>(&region);
      if(dr != nullptr){
        metadata_ = region.metadata_;
        region.metadata_ = nullptr;
      }
      ADataRegion<T>::operator=(std::move(region));
    }

    /* Assignments */
    DataRegionBase<T, I>& operator=(const ADataRegion<T> &region)
    {
      DataRegionBase<T, I> *dr = dynamic_cast<DataRegionBase<T, I>*>(&region);
      if(dr != nullptr) metadata_ = region.metadata_;
      ADataRegion<T>::operator=(region);

      return *this;
    }
    DataRegionBase<T, I>& operator=(const ADataRegion<T> &&region)
    {
      DataRegionBase<T, I> *dr = dynamic_cast<DataRegionBase<T, I>*>(&region);
      if(dr != nullptr){
        metadata_ = region.metadata_;
        region.metadata_ = nullptr;
      }
      ADataRegion<T>::operator=(std::move(region));

      return *this;
    }

    /* Accessors/Mutators */
    I& metadata() const { return *metadata_; };

    /** 
     * Behavioral functions
     */
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

    virtual ADataRegion<T>* Clone()
    {
      ADataRegion<T> *region = new DataRegionBase<T, I>(*this);
      return region;
    }
};

#endif    // DISP_SRC_DISP_DATA_REGION_BARE_BASE_H
