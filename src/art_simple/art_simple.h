// Create header file for art_simple.cc

#ifndef ART_SIMPLE_H
#define ART_SIMPLE_H

void art(const float* data, 
         int dy, int dt, int dx, 
         const float* center, 
         const float* theta, 
         float* recon, 
         int ngridx, int ngridy, 
         int num_iter);

#endif // ART_SIMPLE_H
