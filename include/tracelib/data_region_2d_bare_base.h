/** \file data_region_2d_bare_base.h 
 */
#ifndef DISP_SRC_DISP_DATA_REGION_2D_BARE_BASE_H
#define DISP_SRC_DISP_DATA_REGION_2D_BARE_BASE_H

/**
 * \class DataRegion2DBareBase
 * \brief This class uses DataRegionBareBase in order to provide 
 * replicated output objects.
 *
 * Contact: bicer@anl.gov
 */

#include <iostream>
#include <vector>
#include "data_region_bare_base.h"
#include <boost/serialization/vector.hpp>
#include <boost/serialization/serialization.hpp>
#include <boost/serialization/split_member.hpp>

template <typename T> 
class DataRegion2DBareBase {
  private:
    std::vector<DataRegionBareBase<T>*> regions_;
    size_t rows_ = 0;
    size_t cols_ = 0;

    friend class boost::serialization::access;

    template<class Archive>
    void save(Archive &ar, const unsigned int /*version*/) const { // Mark version as unused
        ar & rows_;
        ar & cols_;
        for (const auto& region : regions_) {
            ar & *region;
        }
    }

    template<class Archive>
    void load(Archive &ar, const unsigned int /*version*/) { // Mark version as unused
        ar & rows_;
        ar & cols_;
        regions_.clear();
        regions_.reserve(rows_);
        for (size_t i = 0; i < rows_; ++i) {
            auto region = new DataRegionBareBase<T>(cols_);
            ar & *region;
            regions_.push_back(region);
        }
    }

    BOOST_SERIALIZATION_SPLIT_MEMBER()

  public:
    DataRegion2DBareBase() : rows_(0), cols_(0) {} // Default constructor

    DataRegion2DBareBase(size_t rows, size_t cols){
      regions_.reserve(rows);

      for(size_t i=0; i<rows; i++)
        regions_.push_back(new DataRegionBareBase<T>(cols));

      rows_ = rows;
      cols_ = cols;
    }

    ~DataRegion2DBareBase(){
      for(auto &region : regions_)
        delete region;
      rows_ = 0;
      cols_ = 0;
    }

    /* Copy constructor */
    DataRegion2DBareBase(const DataRegion2DBareBase &dr){
      regions_.reserve(dr.rows());

      for(auto t : dr.regions()){
        auto r = new DataRegionBareBase<T>(*t);
        regions_.push_back(r);
      }

      rows_ = dr.rows();
      cols_ = dr.cols();
    }

    /* Copy assignment */
    DataRegion2DBareBase<T>& operator=(const DataRegion2DBareBase &dr){
      if(this==&dr) return *this;

      for(auto rg : regions_)
        delete rg;

      regions_.clear();
      regions_.reserve(dr.rows());

      for(auto rg : dr.regions())
        regions_.push_back(new DataRegionBareBase<T>(*rg));
      rows_ = dr.rows();
      cols_ = dr.cols();

      return *this;
    }

    /* TODO: Move constructor & assignment */

    DataRegionBareBase<T>& operator[](size_t row) const { 
      return *(regions_[row]); 
    }

    const std::vector<DataRegionBareBase<T>*>& regions() const { return regions_; };
    size_t num_rows() const { return rows_; }
    size_t num_cols() const { return cols_; }
    size_t rows() const { return rows_; }
    size_t cols() const { return cols_; }
    size_t count() const { return rows_*cols_; }
    size_t size() const {return rows_*cols_*sizeof(T); }


    T& item(size_t row, size_t col) const {
      if(row >= rows_ || col >= cols_)
        throw std::out_of_range("Tried to access out of range offset!");
      return (*regions_[row])[col];
    }

    void item(size_t row, size_t col, T &val) const {
      if(row >= rows_ || col >= cols_)
        throw std::out_of_range("Tried to access out of range offset!");
      (*regions_[row])[col] = val;
    }

    void ResetAllItems(T &val) const {
      for(size_t i=0; i<rows_; i++)
        for(size_t j=0; j<cols_; j++)
          (*regions_[i])[j] = val;
    }

    void ResetAllMirroredRegions(){
      for(size_t i=0; i<rows_; i++)
        (*regions_[i]).ResetMirroredRegionIter();
    }

    void copy(DataRegion2DBareBase<T> &dr){
      if(dr.rows() != rows_ || dr.cols() != cols_)
        throw std::out_of_range("DataRegions' ranges do not overlap!");

      dr = *this;
    }
};

#endif    // DISP_SRC_DISP_DATA_REGION_2D_BARE_BASE_H
