#include <cmath>
#include "trace_utils.h"

void trace_utils::Absolute(float *data, size_t count)
{
  for(size_t i=0; i<count; ++i)
    data[i] = std::fabs(data[i]);
}

void trace_utils::DegreeToRadian(trace_io::H5Data &theta)
{
  int num_elem = theta.metadata->dims[0];
  float *buf = new float[num_elem];
  for(int i=0; i<num_elem; ++i) buf[i] = 0.;

  if(buf==NULL)
    throw std::invalid_argument("Unable to allocate buffer."); 

  for(int i=0; i<num_elem; ++i)
    buf[i] = static_cast<float*>(theta.data)[i]*kPI/180.0;

  free(theta.data);
  theta.data = buf;
}

// Backward projection (for SIRT)
void trace_utils::UpdateRecon(
    ADataRegion<float> &recon,                  // Reconstruction object
    DataRegion2DBareBase<float> &comb_replica)  // Locally combined replica
{
  size_t rows = comb_replica.rows();
  size_t cols = comb_replica.cols()/2;
  for(size_t i=0; i<rows; ++i){
    auto replica = comb_replica[i];
    for(size_t j=0; j<cols; ++j)
      recon[i*cols + j] += 
        replica[j] / replica[cols+j];
  }
}

int trace_utils::CalculateQuadrant(float theta_q)
{
  return
    ((theta_q >= 0 && theta_q < kPI/2) || 
     (theta_q >= kPI && theta_q < 3*kPI/2)) ? 1 : 0;
}

void trace_utils::CalculateCoordinates(
    int num_grid,
    float xi, float yi, float sinq, float cosq,
    const float *gridx, const float *gridy, 
    float *coordx, float *coordy)
{ 
  float srcx, srcy, detx, dety;
  float slope, islope;
  int n;

  /* Find the corresponding source and 
   * detector locations for a given line
   * trajectory of a projection (Projection
   * is specified by sinp and cosp).  
   */
  srcx = xi*cosq-yi*sinq;
  srcy = xi*sinq+yi*cosq;
  detx = -1 * (xi*cosq+yi*sinq);
  dety = -xi*sinq+yi*cosq;

  /* Find the intersection points of the
   * line connecting the source and the detector
   * points with the reconstruction grid. The 
   * intersection points are then defined as: 
   * (coordx, gridy) and (gridx, coordy)
   */
  slope = (srcy-dety)/(srcx-detx);
  islope = 1/slope;
  for (n = 0; n <= num_grid; n++) {
    coordx[n] = islope*(gridy[n]-srcy)+srcx;
    coordy[n] = slope*(gridx[n]-srcx)+srcy;
  }
}

void trace_utils::MergeTrimCoordinates(
    int num_grid,
    float *coordx, float *coordy,
    const float *gridx, const float *gridy,
    int *alen, int *blen,
    float *ax, float *ay,
    float *bx, float *by)
{
  /* Merge the (coordx, gridy) and (gridx, coordy)
   * on a single array of points (ax, ay) and trim
   * the coordinates that are outside the
   * reconstruction grid. 
   */
  *alen = 0;
  *blen = 0;
  for (int i = 0; i <= num_grid; ++i) {
    if (coordx[i] > gridx[0] && 
        coordx[i] < gridx[num_grid]) {
      ax[*alen] = coordx[i];
      ay[*alen] = gridy[i];
      (*alen)++;
    }
    if (coordy[i] > gridy[0] && 
        coordy[i] < gridy[num_grid]) {
      bx[*blen] = gridx[i];
      by[*blen] = coordy[i];
      (*blen)++;
    }
  }
}

void trace_utils::SortIntersectionPoints(
    int ind_cond,
    int alen, int blen,
    float *ax, float *ay,
    float *bx, float *by,
    float *coorx, float *coory)
{
  int i=0, j=0, k=0;
  int a_ind;

  for(int i=0, j=0; i<alen; ++i){
    a_ind = (ind_cond) ? i : (alen-1-i);
    coorx[j] = ax[a_ind];
    coory[j] = ay[a_ind];
    i++; j++;
  }

  while (i < alen && j < blen)
  {
    a_ind = (ind_cond) ? i : (alen-1-i);
    if (ax[a_ind] < bx[j]) {
      coorx[k] = ax[a_ind];
      coory[k] = ay[a_ind];
      i++;
      k++;
    } else {
      coorx[k] = bx[j];
      coory[k] = by[j];
      j++;
      k++;
    }
  }
  while (i < alen) {
    a_ind = (ind_cond) ? i : (alen-1-i);
    coorx[k] = ax[a_ind];
    coory[k] = ay[a_ind];
    i++;
    k++;
  }
  while (j < blen) {
    coorx[k] = bx[j];
    coory[k] = by[j];
    j++;
    k++;
  }
}

void trace_utils::CalculateDistanceLengths(
    int len, int num_grids,
    float *coorx, float *coory, 
    float *leng, float *leng2, int *indi)
{
  int x1, x2, i1, i2;
  float diffx, diffy, midx, midy;
  int indx, indy;

  float mgrids = num_grids/2.;

  for (int i=0; i<len-1; ++i) {
    diffx = coorx[i+1]-coorx[i];
    midx = (coorx[i+1]+coorx[i])/2.;
    diffy = coory[i+1]-coory[i];
    midy = (coory[i+1]+coory[i])/2.;
    leng2[i] = diffx*diffx + diffy*diffy;
    leng[i] = sqrt(leng2[i]);

    x1 = midx + mgrids;
    i1 = static_cast<int>(x1);
    indx = i1 - (i1>x1);
    x2 = midy + mgrids;
    i2 = static_cast<int>(x2);
    indy = i2 - (i2>x2);
    indi[i] = indx+(indy*num_grids);
  }
}

void trace_utils::CalculateDistanceLengths(
    int len, int num_grids,
    float *coorx, float *coory, 
    float *leng, int *indi)
{
  int x1, x2, i1, i2;
  float diffx, diffy, midx, midy;
  int indx, indy;

  float mgrids = num_grids/2.;

  for (int i=0; i<len-1; ++i) {
    diffx = coorx[i+1]-coorx[i];
    midx = (coorx[i+1]+coorx[i])/2.;
    diffy = coory[i+1]-coory[i];
    midy = (coory[i+1]+coory[i])/2.;
    float sq = diffx*diffx + diffy*diffy;
    leng[i] = sqrt(sq);

    x1 = midx + mgrids;
    i1 = static_cast<int>(x1);
    indx = i1 - (i1>x1);
    x2 = midy + mgrids;
    i2 = static_cast<int>(x2);
    indy = i2 - (i2>x2);
    indi[i] = indx+(indy*num_grids);
  }
}
