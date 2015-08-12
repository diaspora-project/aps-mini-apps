#include "apmlr.h"

void APMLRReconSpace::UpdateRecon(
    APMLRDataRegion &slices,                    // Input slices, metadata, recon
    DataRegion2DBareBase<float> &comb_replica)  // Locally combined replica
{
  int slice_count = 
    slices.metadata().num_neighbor_recon_slices() *
    slices.metadata().num_grids() * slices.metadata().num_grids();
  float *recon = &slices.metadata().recon()[slice_count];
  size_t rows = comb_replica.rows();
  size_t cols = comb_replica.cols()/2;

  float *F = slices.F();
  float *G = slices.G();

  for(size_t i=0; i<rows; ++i){
    auto replica = comb_replica[i];
    for(size_t j=0; j<cols; ++j){
      size_t index = (i*cols) + j;
      recon[index] =
        (-G[index] + sqrt(G[index]*G[index] - 8*replica[j]*F[index])) /
          (4*F[index]);
    }
  }
}

void APMLRReconSpace::CalculateFGInner(
  float * recon, float *F, float *G, 
  float beta, float beta1, float delta, float delta1, float regw, 
  int num_slices, int num_grids)
{
  int k, n, m, q;
  int ind0, indg[10];
  float rg[10];
  float mg[10];
  float wg[10];
  float gammag[10];
   
  // Weights for inner neighborhoods.
  float totalwg = 4+4/sqrt(2)+2*regw;
  wg[0] = 1/totalwg;
  wg[1] = 1/totalwg;
  wg[2] = 1/totalwg;
  wg[3] = 1/totalwg;
  wg[4] = 1/sqrt(2)/totalwg;
  wg[5] = 1/sqrt(2)/totalwg;
  wg[6] = 1/sqrt(2)/totalwg;
  wg[7] = 1/sqrt(2)/totalwg;
  wg[8] = regw/totalwg;
  wg[9] = regw/totalwg;

  // (inner region)
  //for (k = 1; k < num_slices-1; k++) {
  for (k = 0; k < num_slices; k++) {
    for (n = 1; n < num_grids-1; n++) {
      for (m = 1; m < num_grids-1; m++) {
        ind0 = m + n*num_grids + k*num_grids*num_grids;

        indg[0] = ind0+1;
        indg[1] = ind0-1;
        indg[2] = ind0+num_grids;
        indg[3] = ind0-num_grids;
        indg[4] = ind0+num_grids+1; 
        indg[5] = ind0+num_grids-1;
        indg[6] = ind0-num_grids+1;
        indg[7] = ind0-num_grids-1;
        indg[8] = ind0+num_grids*num_grids;
        indg[9] = ind0-num_grids*num_grids;

        for (q = 0; q < 8; q++) {
          mg[q] = recon[ind0]+recon[indg[q]];
          rg[q] = recon[ind0]-recon[indg[q]];
          gammag[q] = 1/(1+fabs(rg[q]/delta));
          F[ind0] += 2*beta*wg[q]*gammag[q];
          G[ind0] -= 2*beta*wg[q]*gammag[q]*mg[q];
        }

        for (q = 8; q < 10; q++) {
          mg[q] = recon[ind0]+recon[indg[q]];
          rg[q] = recon[ind0]-recon[indg[q]];
          gammag[q] = 1/(1+fabs(rg[q]/delta1));
          F[ind0] += 2*beta1*wg[q]*gammag[q];
          G[ind0] -= 2*beta1*wg[q]*gammag[q]*mg[q];
        }
      }
    }
  }

  // Weights for edges.
  totalwg = 3+2/sqrt(2)+2*regw;
  wg[0] = 1/totalwg;
  wg[1] = 1/totalwg;
  wg[2] = 1/totalwg;
  wg[3] = 1/sqrt(2)/totalwg;
  wg[4] = 1/sqrt(2)/totalwg;
  wg[5] = regw/totalwg;
  wg[6] = regw/totalwg;

  // (top)
  //for (k = 1; k < num_slices-1; k++) {
  for (k = 0; k < num_slices; k++) {
    for (m = 1; m < num_grids-1; m++) {
      ind0 = m + k*num_grids*num_grids;

      indg[0] = ind0+1;
      indg[1] = ind0-1;
      indg[2] = ind0+num_grids;
      indg[3] = ind0+num_grids+1; 
      indg[4] = ind0+num_grids-1;
      indg[5] = ind0+num_grids*num_grids;
      indg[6] = ind0-num_grids*num_grids;

      for (q = 0; q < 5; q++) {
        mg[q] = recon[ind0]+recon[indg[q]];
        rg[q] = recon[ind0]-recon[indg[q]];
        gammag[q] = 1/(1+fabs(rg[q]/delta));
        F[ind0] += 2*beta*wg[q]*gammag[q];
        G[ind0] -= 2*beta*wg[q]*gammag[q]*mg[q];
      }

      for (q = 5; q < 7; q++) {
        mg[q] = recon[ind0]+recon[indg[q]];
        rg[q] = recon[ind0]-recon[indg[q]];
        gammag[q] = 1/(1+fabs(rg[q]/delta1));
        F[ind0] += 2*beta1*wg[q]*gammag[q];
        G[ind0] -= 2*beta1*wg[q]*gammag[q]*mg[q];
      }
    }
  }

  // (bottom)
  //for (k = 1; k < num_slices-1; k++) {
  for (k = 0; k < num_slices; k++) {
    for (m = 1; m < num_grids-1; m++) {
      ind0 = m + (num_grids-1)*num_grids + k*num_grids*num_grids;

      indg[0] = ind0+1;
      indg[1] = ind0-1;
      indg[2] = ind0-num_grids;
      indg[3] = ind0-num_grids+1;
      indg[4] = ind0-num_grids-1;
      indg[5] = ind0+num_grids*num_grids;
      indg[6] = ind0-num_grids*num_grids;

      for (q = 0; q < 5; q++) {
        mg[q] = recon[ind0]+recon[indg[q]];
        rg[q] = recon[ind0]-recon[indg[q]];
        gammag[q] = 1/(1+fabs(rg[q]/delta));
        F[ind0] += 2*beta*wg[q]*gammag[q];
        G[ind0] -= 2*beta*wg[q]*gammag[q]*mg[q];
      }

      for (q = 5; q < 7; q++) {
        mg[q] = recon[ind0]+recon[indg[q]];
        rg[q] = recon[ind0]-recon[indg[q]];
        gammag[q] = 1/(1+fabs(rg[q]/delta1));
        F[ind0] += 2*beta1*wg[q]*gammag[q];
        G[ind0] -= 2*beta1*wg[q]*gammag[q]*mg[q];
      }
    }
  }

  // (left)  
  //for (k = 1; k < num_slices-1; k++) {
  for (k = 0; k < num_slices; k++) {
    for (n = 1; n < num_grids-1; n++) {
      ind0 = n*num_grids + k*num_grids*num_grids;

      indg[0] = ind0+1;
      indg[1] = ind0+num_grids;
      indg[2] = ind0-num_grids;
      indg[3] = ind0+num_grids+1; 
      indg[4] = ind0-num_grids+1;
      indg[5] = ind0+num_grids*num_grids;
      indg[6] = ind0-num_grids*num_grids;

      for (q = 0; q < 5; q++) {
        mg[q] = recon[ind0]+recon[indg[q]];
        rg[q] = recon[ind0]-recon[indg[q]];
        gammag[q] = 1/(1+fabs(rg[q]/delta));
        F[ind0] += 2*beta*wg[q]*gammag[q];
        G[ind0] -= 2*beta*wg[q]*gammag[q]*mg[q];
      }

      for (q = 5; q < 7; q++) {
        mg[q] = recon[ind0]+recon[indg[q]];
        rg[q] = recon[ind0]-recon[indg[q]];
        gammag[q] = 1/(1+fabs(rg[q]/delta1));
        F[ind0] += 2*beta1*wg[q]*gammag[q];
        G[ind0] -= 2*beta1*wg[q]*gammag[q]*mg[q];
      }
    }
  }

  // (right)                
  //for (k = 1; k < num_slices-1; k++) {
  for (k = 0; k < num_slices; k++) {
    for (n = 1; n < num_grids-1; n++) {
      ind0 = (num_grids-1) + n*num_grids + k*num_grids*num_grids;

      indg[0] = ind0-1;
      indg[1] = ind0+num_grids;
      indg[2] = ind0-num_grids;
      indg[3] = ind0+num_grids-1;
      indg[4] = ind0-num_grids-1;
      indg[5] = ind0+num_grids*num_grids;
      indg[6] = ind0-num_grids*num_grids;

      for (q = 0; q < 5; q++) {
        mg[q] = recon[ind0]+recon[indg[q]];
        rg[q] = recon[ind0]-recon[indg[q]];
        gammag[q] = 1/(1+fabs(rg[q]/delta));
        F[ind0] += 2*beta*wg[q]*gammag[q];
        G[ind0] -= 2*beta*wg[q]*gammag[q]*mg[q];
      }

      for (q = 5; q < 7; q++) {
        mg[q] = recon[ind0]+recon[indg[q]];
        rg[q] = recon[ind0]-recon[indg[q]];
        gammag[q] = 1/(1+fabs(rg[q]/delta1));
        F[ind0] += 2*beta1*wg[q]*gammag[q];
        G[ind0] -= 2*beta1*wg[q]*gammag[q]*mg[q];
      }
    }
  }

  // Weights for corners.
  totalwg = 2+1/sqrt(2)+2*regw;
  wg[0] = 1/totalwg;
  wg[1] = 1/totalwg;
  wg[2] = 1/sqrt(2)/totalwg;
  wg[3] = regw/totalwg;
  wg[4] = regw/totalwg;

  // (top-left)
  //for (k = 1; k < num_slices-1; k++) {     
  for (k = 0; k < num_slices; k++) {     
    ind0 = k*num_grids*num_grids;

    indg[0] = ind0+1;
    indg[1] = ind0+num_grids;
    indg[2] = ind0+num_grids+1; 
    indg[3] = ind0+num_grids*num_grids;
    indg[4] = ind0-num_grids*num_grids;

    for (q = 0; q < 3; q++) {
      mg[q] = recon[ind0]+recon[indg[q]];
      rg[q] = recon[ind0]-recon[indg[q]];
      gammag[q] = 1/(1+fabs(rg[q]/delta));
      F[ind0] += 2*beta*wg[q]*gammag[q];
      G[ind0] -= 2*beta*wg[q]*gammag[q]*mg[q];
    }

    for (q = 3; q < 5; q++) {
      mg[q] = recon[ind0]+recon[indg[q]];
      rg[q] = recon[ind0]-recon[indg[q]];
      gammag[q] = 1/(1+fabs(rg[q]/delta1));
      F[ind0] += 2*beta1*wg[q]*gammag[q];
      G[ind0] -= 2*beta1*wg[q]*gammag[q]*mg[q];
    }
  }

  // (top-right)
  //for (k = 1; k < num_slices-1; k++) {     
  for (k = 0; k < num_slices; k++) {     
    ind0 = (num_grids-1) + k*num_grids*num_grids;

    indg[0] = ind0-1;
    indg[1] = ind0+num_grids;
    indg[2] = ind0+num_grids-1;
    indg[3] = ind0+num_grids*num_grids;
    indg[4] = ind0-num_grids*num_grids;

    for (q = 0; q < 3; q++) {
      mg[q] = recon[ind0]+recon[indg[q]];
      rg[q] = recon[ind0]-recon[indg[q]];
      gammag[q] = 1/(1+fabs(rg[q]/delta));
      F[ind0] += 2*beta*wg[q]*gammag[q];
      G[ind0] -= 2*beta*wg[q]*gammag[q]*mg[q];
    }

    for (q = 3; q < 5; q++) {
      mg[q] = recon[ind0]+recon[indg[q]];
      rg[q] = recon[ind0]-recon[indg[q]];
      gammag[q] = 1/(1+fabs(rg[q]/delta1));
      F[ind0] += 2*beta1*wg[q]*gammag[q];
      G[ind0] -= 2*beta1*wg[q]*gammag[q]*mg[q];
    }
  }

  // (bottom-left)
  //for (k = 1; k < num_slices-1; k++) {     
  for (k = 0; k < num_slices; k++) {     
    ind0 = (num_grids-1)*num_grids + k*num_grids*num_grids;

    indg[0] = ind0+1;
    indg[1] = ind0-num_grids;
    indg[2] = ind0-num_grids+1;
    indg[3] = ind0+num_grids*num_grids;
    indg[4] = ind0-num_grids*num_grids;

    for (q = 0; q < 3; q++) {
      mg[q] = recon[ind0]+recon[indg[q]];
      rg[q] = recon[ind0]-recon[indg[q]];
      gammag[q] = 1/(1+fabs(rg[q]/delta));
      F[ind0] += 2*beta*wg[q]*gammag[q];
      G[ind0] -= 2*beta*wg[q]*gammag[q]*mg[q];
    }

    for (q = 3; q < 5; q++) {
      mg[q] = recon[ind0]+recon[indg[q]];
      rg[q] = recon[ind0]-recon[indg[q]];
      gammag[q] = 1/(1+fabs(rg[q]/delta1));
      F[ind0] += 2*beta1*wg[q]*gammag[q];
      G[ind0] -= 2*beta1*wg[q]*gammag[q]*mg[q];
    }
  }

  // (bottom-right)        
  //for (k = 1; k < num_slices-1; k++) {     
  for (k = 0; k < num_slices; k++) {     
    ind0 = (num_grids-1) + (num_grids-1)*num_grids + k*num_grids*num_grids;

    indg[0] = ind0-1;
    indg[1] = ind0-num_grids;
    indg[2] = ind0-num_grids-1;
    indg[3] = ind0+num_grids*num_grids;
    indg[4] = ind0-num_grids*num_grids;

    for (q = 0; q < 3; q++) {
      mg[q] = recon[ind0]+recon[indg[q]];
      rg[q] = recon[ind0]-recon[indg[q]];
      gammag[q] = 1/(1+fabs(rg[q]/delta));
      F[ind0] += 2*beta*wg[q]*gammag[q];
      G[ind0] -= 2*beta*wg[q]*gammag[q]*mg[q];
    }

    for (q = 3; q < 5; q++) {
      mg[q] = recon[ind0]+recon[indg[q]];
      rg[q] = recon[ind0]-recon[indg[q]];
      gammag[q] = 1/(1+fabs(rg[q]/delta1));
      F[ind0] += 2*beta1*wg[q]*gammag[q];
      G[ind0] -= 2*beta1*wg[q]*gammag[q]*mg[q];
    }
  }
}

void APMLRReconSpace::CalculateFGTop(
   float *recon, float *F, float *G,
   float beta, float beta1, float delta, float delta1, float regw,
   int num_grids)
{
  int n, m, q;
  int ind0, indg[10];
  float rg[10];
  float mg[10];
  float wg[10];
  float gammag[10];

  // Weights for inner neighborhoods.
  float totalwg = 4+4/sqrt(2)+regw;
  wg[0] = 1/totalwg;
  wg[1] = 1/totalwg;
  wg[2] = 1/totalwg;
  wg[3] = 1/totalwg;
  wg[4] = 1/sqrt(2)/totalwg;
  wg[5] = 1/sqrt(2)/totalwg;
  wg[6] = 1/sqrt(2)/totalwg;
  wg[7] = 1/sqrt(2)/totalwg;
  wg[8] = regw/totalwg;

  // Upper-most (only for rank=0)
  // (inner region)
  for (n = 1; n < num_grids-1; n++) {
    for (m = 1; m < num_grids-1; m++) {
      ind0 = m + n*num_grids;

      indg[0] = ind0+1;
      indg[1] = ind0-1;
      indg[2] = ind0+num_grids;
      indg[3] = ind0-num_grids;
      indg[4] = ind0+num_grids+1; 
      indg[5] = ind0+num_grids-1;
      indg[6] = ind0-num_grids+1;
      indg[7] = ind0-num_grids-1;
      indg[8] = ind0+num_grids*num_grids;

      for (q = 0; q < 8; q++) {
        mg[q] = recon[ind0]+recon[indg[q]];
        rg[q] = recon[ind0]-recon[indg[q]];
        gammag[q] = 1/(1+fabs(rg[q]/delta));
        F[ind0] += 2*beta*wg[q]*gammag[q];
        G[ind0] -= 2*beta*wg[q]*gammag[q]*mg[q];
      }

      for (q = 8; q < 9; q++) {
        mg[q] = recon[ind0]+recon[indg[q]];
        rg[q] = recon[ind0]-recon[indg[q]];
        gammag[q] = 1/(1+fabs(rg[q]/delta1));
        F[ind0] += 2*beta1*wg[q]*gammag[q];
        G[ind0] -= 2*beta1*wg[q]*gammag[q]*mg[q];
      }
    }
  }

  // Weights for edges.
  totalwg = 3+2/sqrt(2)+regw;
  wg[0] = 1/totalwg;
  wg[1] = 1/totalwg;
  wg[2] = 1/totalwg;
  wg[3] = 1/sqrt(2)/totalwg;
  wg[4] = 1/sqrt(2)/totalwg;
  wg[5] = regw/totalwg;

  // (top)
  for (m = 1; m < num_grids-1; m++) {
    ind0 = m;

    indg[0] = ind0+1;
    indg[1] = ind0-1;
    indg[2] = ind0+num_grids;
    indg[3] = ind0+num_grids+1; 
    indg[4] = ind0+num_grids-1;
    indg[5] = ind0+num_grids*num_grids;

    for (q = 0; q < 5; q++) {
      mg[q] = recon[ind0]+recon[indg[q]];
      rg[q] = recon[ind0]-recon[indg[q]];
      gammag[q] = 1/(1+fabs(rg[q]/delta));
      F[ind0] += 2*beta*wg[q]*gammag[q];
      G[ind0] -= 2*beta*wg[q]*gammag[q]*mg[q];
    }

    for (q = 5; q < 6; q++) {
      mg[q] = recon[ind0]+recon[indg[q]];
      rg[q] = recon[ind0]-recon[indg[q]];
      gammag[q] = 1/(1+fabs(rg[q]/delta1));
      F[ind0] += 2*beta1*wg[q]*gammag[q];
      G[ind0] -= 2*beta1*wg[q]*gammag[q]*mg[q];
    }
  }

  // (bottom)
  for (m = 1; m < num_grids-1; m++) {
    ind0 = m + (num_grids-1)*num_grids;

    indg[0] = ind0+1;
    indg[1] = ind0-1;
    indg[2] = ind0-num_grids;
    indg[3] = ind0-num_grids+1;
    indg[4] = ind0-num_grids-1;
    indg[5] = ind0+num_grids*num_grids;

    for (q = 0; q < 5; q++) {
      mg[q] = recon[ind0]+recon[indg[q]];
      rg[q] = recon[ind0]-recon[indg[q]];
      gammag[q] = 1/(1+fabs(rg[q]/delta));
      F[ind0] += 2*beta*wg[q]*gammag[q];
      G[ind0] -= 2*beta*wg[q]*gammag[q]*mg[q];
    }

    for (q = 5; q < 6; q++) {
      mg[q] = recon[ind0]+recon[indg[q]];
      rg[q] = recon[ind0]-recon[indg[q]];
      gammag[q] = 1/(1+fabs(rg[q]/delta1));
      F[ind0] += 2*beta1*wg[q]*gammag[q];
      G[ind0] -= 2*beta1*wg[q]*gammag[q]*mg[q];
    }
  }

  // (left)  
  for (n = 1; n < num_grids-1; n++) {
    ind0 = n*num_grids;

    indg[0] = ind0+1;
    indg[1] = ind0+num_grids;
    indg[2] = ind0-num_grids;
    indg[3] = ind0+num_grids+1; 
    indg[4] = ind0-num_grids+1;
    indg[5] = ind0+num_grids*num_grids;

    for (q = 0; q < 5; q++) {
      mg[q] = recon[ind0]+recon[indg[q]];
      rg[q] = recon[ind0]-recon[indg[q]];
      gammag[q] = 1/(1+fabs(rg[q]/delta));
      F[ind0] += 2*beta*wg[q]*gammag[q];
      G[ind0] -= 2*beta*wg[q]*gammag[q]*mg[q];
    }

    for (q = 5; q < 6; q++) {
      mg[q] = recon[ind0]+recon[indg[q]];
      rg[q] = recon[ind0]-recon[indg[q]];
      gammag[q] = 1/(1+fabs(rg[q]/delta1));
      F[ind0] += 2*beta1*wg[q]*gammag[q];
      G[ind0] -= 2*beta1*wg[q]*gammag[q]*mg[q];
    }
  }

  // (right)                
  for (n = 1; n < num_grids-1; n++) {
    ind0 = (num_grids-1) + n*num_grids;

    indg[0] = ind0-1;
    indg[1] = ind0+num_grids;
    indg[2] = ind0-num_grids;
    indg[3] = ind0+num_grids-1;
    indg[4] = ind0-num_grids-1;
    indg[5] = ind0+num_grids*num_grids;

    for (q = 0; q < 5; q++) {
      mg[q] = recon[ind0]+recon[indg[q]];
      rg[q] = recon[ind0]-recon[indg[q]];
      gammag[q] = 1/(1+fabs(rg[q]/delta));
      F[ind0] += 2*beta*wg[q]*gammag[q];
      G[ind0] -= 2*beta*wg[q]*gammag[q]*mg[q];
    }

    for (q = 5; q < 6; q++) {
      mg[q] = recon[ind0]+recon[indg[q]];
      rg[q] = recon[ind0]-recon[indg[q]];
      gammag[q] = 1/(1+fabs(rg[q]/delta1));
      F[ind0] += 2*beta1*wg[q]*gammag[q];
      G[ind0] -= 2*beta1*wg[q]*gammag[q]*mg[q];
    }
  }

  // Weights for corners.
  totalwg = 2+1/sqrt(2)+regw;
  wg[0] = 1/totalwg;
  wg[1] = 1/totalwg;
  wg[2] = 1/sqrt(2)/totalwg;
  wg[3] = regw/totalwg;

  // (top-left)   
  ind0 = 0;

  indg[0] = ind0+1;
  indg[1] = ind0+num_grids;
  indg[2] = ind0+num_grids+1; 
  indg[3] = ind0+num_grids*num_grids;

  for (q = 0; q < 3; q++) {
    mg[q] = recon[ind0]+recon[indg[q]];
    rg[q] = recon[ind0]-recon[indg[q]];
    gammag[q] = 1/(1+fabs(rg[q]/delta));
    F[ind0] += 2*beta*wg[q]*gammag[q];
    G[ind0] -= 2*beta*wg[q]*gammag[q]*mg[q];
  }

  for (q = 3; q < 4; q++) {
    mg[q] = recon[ind0]+recon[indg[q]];
    rg[q] = recon[ind0]-recon[indg[q]];
    gammag[q] = 1/(1+fabs(rg[q]/delta1));
    F[ind0] += 2*beta1*wg[q]*gammag[q];
    G[ind0] -= 2*beta1*wg[q]*gammag[q]*mg[q];
  }

  // (top-right)    
  ind0 = (num_grids-1);

  indg[0] = ind0-1;
  indg[1] = ind0+num_grids;
  indg[2] = ind0+num_grids-1;
  indg[3] = ind0+num_grids*num_grids;

  for (q = 0; q < 3; q++) {
    mg[q] = recon[ind0]+recon[indg[q]];
    rg[q] = recon[ind0]-recon[indg[q]];
    gammag[q] = 1/(1+fabs(rg[q]/delta));
    F[ind0] += 2*beta*wg[q]*gammag[q];
    G[ind0] -= 2*beta*wg[q]*gammag[q]*mg[q];
  }

  for (q = 3; q < 4; q++) {
    mg[q] = recon[ind0]+recon[indg[q]];
    rg[q] = recon[ind0]-recon[indg[q]];
    gammag[q] = 1/(1+fabs(rg[q]/delta1));
    F[ind0] += 2*beta1*wg[q]*gammag[q];
    G[ind0] -= 2*beta1*wg[q]*gammag[q]*mg[q];
  }

  // (bottom-left) 
  ind0 = (num_grids-1)*num_grids;

  indg[0] = ind0+1;
  indg[1] = ind0-num_grids;
  indg[2] = ind0-num_grids+1;
  indg[3] = ind0+num_grids*num_grids;

  for (q = 0; q < 3; q++) {
    mg[q] = recon[ind0]+recon[indg[q]];
    rg[q] = recon[ind0]-recon[indg[q]];
    gammag[q] = 1/(1+fabs(rg[q]/delta));
    F[ind0] += 2*beta*wg[q]*gammag[q];
    G[ind0] -= 2*beta*wg[q]*gammag[q]*mg[q];
  }

  for (q = 3; q < 4; q++) {
    mg[q] = recon[ind0]+recon[indg[q]];
    rg[q] = recon[ind0]-recon[indg[q]];
    gammag[q] = 1/(1+fabs(rg[q]/delta1));
    F[ind0] += 2*beta1*wg[q]*gammag[q];
    G[ind0] -= 2*beta1*wg[q]*gammag[q]*mg[q];
  }

  // (bottom-right)         
  ind0 = (num_grids-1) + (num_grids-1)*num_grids;

  indg[0] = ind0-1;
  indg[1] = ind0-num_grids;
  indg[2] = ind0-num_grids-1;
  indg[3] = ind0+num_grids*num_grids;

  for (q = 0; q < 3; q++) {
    mg[q] = recon[ind0]+recon[indg[q]];
    rg[q] = recon[ind0]-recon[indg[q]];
    gammag[q] = 1/(1+fabs(rg[q]/delta));
    F[ind0] += 2*beta*wg[q]*gammag[q];
    G[ind0] -= 2*beta*wg[q]*gammag[q]*mg[q];
  }

  for (q = 3; q < 4; q++) {
    mg[q] = recon[ind0]+recon[indg[q]];
    rg[q] = recon[ind0]-recon[indg[q]];
    gammag[q] = 1/(1+fabs(rg[q]/delta1));
    F[ind0] += 2*beta1*wg[q]*gammag[q];
    G[ind0] -= 2*beta1*wg[q]*gammag[q]*mg[q];
  }
}

void APMLRReconSpace::CalculateFGBottom(
    float *recon, float *F, float *G,
    float beta, float beta1, float delta, float delta1, float regw, 
    int num_grids)
{
  int n, m, q;
  int ind0, indg[10];
  float rg[10];
  float mg[10];
  float wg[10];
  float gammag[10];

  // Weights for inner neighborhoods.
  float totalwg = 4+4/sqrt(2)+regw;
  wg[0] = 1/totalwg;
  wg[1] = 1/totalwg;
  wg[2] = 1/totalwg;
  wg[3] = 1/totalwg;
  wg[4] = 1/sqrt(2)/totalwg;
  wg[5] = 1/sqrt(2)/totalwg;
  wg[6] = 1/sqrt(2)/totalwg;
  wg[7] = 1/sqrt(2)/totalwg;
  wg[8] = regw/totalwg;

  // Down-most (only for rank=mpi_size)
  // (inner region)
  for (n = 1; n < num_grids-1; n++) {
    for (m = 1; m < num_grids-1; m++) {
      ind0 = m + n*num_grids; //+ (num_slices-1)*num_grids*num_grids;

      indg[0] = ind0+1;
      indg[1] = ind0-1;
      indg[2] = ind0+num_grids;
      indg[3] = ind0-num_grids;
      indg[4] = ind0+num_grids+1; 
      indg[5] = ind0+num_grids-1;
      indg[6] = ind0-num_grids+1;
      indg[7] = ind0-num_grids-1;
      indg[8] = ind0-num_grids*num_grids;

      for (q = 0; q < 8; q++) {
        mg[q] = recon[ind0]+recon[indg[q]];
        rg[q] = recon[ind0]-recon[indg[q]];
        gammag[q] = 1/(1+fabs(rg[q]/delta));
        F[ind0] += 2*beta*wg[q]*gammag[q];
        G[ind0] -= 2*beta*wg[q]*gammag[q]*mg[q];
      }

      for (q = 8; q < 9; q++) {
        mg[q] = recon[ind0]+recon[indg[q]];
        rg[q] = recon[ind0]-recon[indg[q]];
        gammag[q] = 1/(1+fabs(rg[q]/delta1));
        F[ind0] += 2*beta1*wg[q]*gammag[q];
        G[ind0] -= 2*beta1*wg[q]*gammag[q]*mg[q];
      }
    }
  }

  // Weights for edges.
  totalwg = 3+2/sqrt(2)+regw;
  wg[0] = 1/totalwg;
  wg[1] = 1/totalwg;
  wg[2] = 1/totalwg;
  wg[3] = 1/sqrt(2)/totalwg;
  wg[4] = 1/sqrt(2)/totalwg;
  wg[5] = regw/totalwg;

  // (top)
  for (m = 1; m < num_grids-1; m++) {
    ind0 = m; //+ (num_slices-1)*num_grids*num_grids;

    indg[0] = ind0+1;
    indg[1] = ind0-1;
    indg[2] = ind0+num_grids;
    indg[3] = ind0+num_grids+1; 
    indg[4] = ind0+num_grids-1;
    indg[5] = ind0-num_grids*num_grids;

    for (q = 0; q < 5; q++) {
      mg[q] = recon[ind0]+recon[indg[q]];
      rg[q] = recon[ind0]-recon[indg[q]];
      gammag[q] = 1/(1+fabs(rg[q]/delta));
      F[ind0] += 2*beta*wg[q]*gammag[q];
      G[ind0] -= 2*beta*wg[q]*gammag[q]*mg[q];
    }

    for (q = 5; q < 6; q++) {
      mg[q] = recon[ind0]+recon[indg[q]];
      rg[q] = recon[ind0]-recon[indg[q]];
      gammag[q] = 1/(1+fabs(rg[q]/delta1));
      F[ind0] += 2*beta1*wg[q]*gammag[q];
      G[ind0] -= 2*beta1*wg[q]*gammag[q]*mg[q];
    }
  }

  // (bottom)
  for (m = 1; m < num_grids-1; m++) {
    ind0 = m + (num_grids-1)*num_grids; //+ (num_slices-1)*num_grids*num_grids;

    indg[0] = ind0+1;
    indg[1] = ind0-1;
    indg[2] = ind0-num_grids;
    indg[3] = ind0-num_grids+1;
    indg[4] = ind0-num_grids-1;
    indg[5] = ind0-num_grids*num_grids;

    for (q = 0; q < 5; q++) {
      mg[q] = recon[ind0]+recon[indg[q]];
      rg[q] = recon[ind0]-recon[indg[q]];
      gammag[q] = 1/(1+fabs(rg[q]/delta));
      F[ind0] += 2*beta*wg[q]*gammag[q];
      G[ind0] -= 2*beta*wg[q]*gammag[q]*mg[q];
    }

    for (q = 5; q < 6; q++) {
      mg[q] = recon[ind0]+recon[indg[q]];
      rg[q] = recon[ind0]-recon[indg[q]];
      gammag[q] = 1/(1+fabs(rg[q]/delta1));
      F[ind0] += 2*beta1*wg[q]*gammag[q];
      G[ind0] -= 2*beta1*wg[q]*gammag[q]*mg[q];
    }
  }

  // (left)  
  for (n = 1; n < num_grids-1; n++) {
    ind0 = n*num_grids; //+ (num_slices-1)*num_grids*num_grids;

    indg[0] = ind0+1;
    indg[1] = ind0+num_grids;
    indg[2] = ind0-num_grids;
    indg[3] = ind0+num_grids+1; 
    indg[4] = ind0-num_grids+1;
    indg[5] = ind0-num_grids*num_grids;

    for (q = 0; q < 5; q++) {
      mg[q] = recon[ind0]+recon[indg[q]];
      rg[q] = recon[ind0]-recon[indg[q]];
      gammag[q] = 1/(1+fabs(rg[q]/delta));
      F[ind0] += 2*beta*wg[q]*gammag[q];
      G[ind0] -= 2*beta*wg[q]*gammag[q]*mg[q];
    }

    for (q = 5; q < 6; q++) {
      mg[q] = recon[ind0]+recon[indg[q]];
      rg[q] = recon[ind0]-recon[indg[q]];
      gammag[q] = 1/(1+fabs(rg[q]/delta1));
      F[ind0] += 2*beta1*wg[q]*gammag[q];
      G[ind0] -= 2*beta1*wg[q]*gammag[q]*mg[q];
    }
  }

  // (right)                
  for (n = 1; n < num_grids-1; n++) {
    ind0 = (num_grids-1) + n*num_grids; //+ (num_slices-1)*num_grids*num_grids;

    indg[0] = ind0-1;
    indg[1] = ind0+num_grids;
    indg[2] = ind0-num_grids;
    indg[3] = ind0+num_grids-1;
    indg[4] = ind0-num_grids-1;
    indg[5] = ind0-num_grids*num_grids;

    for (q = 0; q < 5; q++) {
      mg[q] = recon[ind0]+recon[indg[q]];
      rg[q] = recon[ind0]-recon[indg[q]];
      gammag[q] = 1/(1+fabs(rg[q]/delta));
      F[ind0] += 2*beta*wg[q]*gammag[q];
      G[ind0] -= 2*beta*wg[q]*gammag[q]*mg[q];
    }

    for (q = 5; q < 6; q++) {
      mg[q] = recon[ind0]+recon[indg[q]];
      rg[q] = recon[ind0]-recon[indg[q]];
      gammag[q] = 1/(1+fabs(rg[q]/delta1));
      F[ind0] += 2*beta1*wg[q]*gammag[q];
      G[ind0] -= 2*beta1*wg[q]*gammag[q]*mg[q];
    }
  }

  // Weights for corners.
  totalwg = 2+1/sqrt(2)+regw;
  wg[0] = 1/totalwg;
  wg[1] = 1/totalwg;
  wg[2] = 1/sqrt(2)/totalwg;
  wg[3] = regw/totalwg;

  // (top-left)
  ind0 = 0;// +(num_slices-1)*num_grids*num_grids;

  indg[0] = ind0+1;
  indg[1] = ind0+num_grids;
  indg[2] = ind0+num_grids+1; 
  indg[3] = ind0-num_grids*num_grids;

  for (q = 0; q < 3; q++) {
    mg[q] = recon[ind0]+recon[indg[q]];
    rg[q] = recon[ind0]-recon[indg[q]];
    gammag[q] = 1/(1+fabs(rg[q]/delta));
    F[ind0] += 2*beta*wg[q]*gammag[q];
    G[ind0] -= 2*beta*wg[q]*gammag[q]*mg[q];
  }

  for (q = 3; q < 4; q++) {
    mg[q] = recon[ind0]+recon[indg[q]];
    rg[q] = recon[ind0]-recon[indg[q]];
    gammag[q] = 1/(1+fabs(rg[q]/delta1));
    F[ind0] += 2*beta1*wg[q]*gammag[q];
    G[ind0] -= 2*beta1*wg[q]*gammag[q]*mg[q];
  }

  // (top-right)   
  ind0 = (num_grids-1); //+ (num_slices-1)*num_grids*num_grids;

  indg[0] = ind0-1;
  indg[1] = ind0+num_grids;
  indg[2] = ind0+num_grids-1;
  indg[3] = ind0-num_grids*num_grids;

  for (q = 0; q < 3; q++) {
    mg[q] = recon[ind0]+recon[indg[q]];
    rg[q] = recon[ind0]-recon[indg[q]];
    gammag[q] = 1/(1+fabs(rg[q]/delta));
    F[ind0] += 2*beta*wg[q]*gammag[q];
    G[ind0] -= 2*beta*wg[q]*gammag[q]*mg[q];
  }

  for (q = 3; q < 4; q++) {
    mg[q] = recon[ind0]+recon[indg[q]];
    rg[q] = recon[ind0]-recon[indg[q]];
    gammag[q] = 1/(1+fabs(rg[q]/delta1));
    F[ind0] += 2*beta1*wg[q]*gammag[q];
    G[ind0] -= 2*beta1*wg[q]*gammag[q]*mg[q];
  }

  // (bottom-left)   
  ind0 = (num_grids-1)*num_grids; //+ (num_slices-1)*num_grids*num_grids;

  indg[0] = ind0+1;
  indg[1] = ind0-num_grids;
  indg[2] = ind0-num_grids+1;
  indg[3] = ind0-num_grids*num_grids;

  for (q = 0; q < 3; q++) {
    mg[q] = recon[ind0]+recon[indg[q]];
    rg[q] = recon[ind0]-recon[indg[q]];
    gammag[q] = 1/(1+fabs(rg[q]/delta));
    F[ind0] += 2*beta*wg[q]*gammag[q];
    G[ind0] -= 2*beta*wg[q]*gammag[q]*mg[q];
  }

  for (q = 3; q < 4; q++) {
    mg[q] = recon[ind0]+recon[indg[q]];
    rg[q] = recon[ind0]-recon[indg[q]];
    gammag[q] = 1/(1+fabs(rg[q]/delta1));
    F[ind0] += 2*beta1*wg[q]*gammag[q];
    G[ind0] -= 2*beta1*wg[q]*gammag[q]*mg[q];
  }

  // (bottom-right)          
  ind0 = (num_grids-1) + (num_grids-1)*num_grids; //+ (num_slices-1)*num_grids*num_grids;

  indg[0] = ind0-1;
  indg[1] = ind0-num_grids;
  indg[2] = ind0-num_grids-1;
  indg[3] = ind0-num_grids*num_grids;

  for (q = 0; q < 3; q++) {
    mg[q] = recon[ind0]+recon[indg[q]];
    rg[q] = recon[ind0]-recon[indg[q]];
    gammag[q] = 1/(1+fabs(rg[q]/delta));
    F[ind0] += 2*beta*wg[q]*gammag[q];
    G[ind0] -= 2*beta*wg[q]*gammag[q]*mg[q];
  }

  for (q = 3; q < 4; q++) {
    mg[q] = recon[ind0]+recon[indg[q]];
    rg[q] = recon[ind0]-recon[indg[q]];
    gammag[q] = 1/(1+fabs(rg[q]/delta1));
    F[ind0] += 2*beta1*wg[q]*gammag[q];
    G[ind0] -= 2*beta1*wg[q]*gammag[q]*mg[q];
  }


}

/* If there is any performance bottleneck here, then turn it 
 * into OpenMP code. */
void APMLRReconSpace::CalculateFG(
    APMLRDataRegion &slices,
    float beta, float beta1, 
    float delta, float delta1, 
    float regw)
{
  float *F = slices.F();
  float *G = slices.G();

  TraceMetadata &metadata = slices.metadata();

  int num_slices = metadata.num_slices();
  int slice_beg_index = metadata.slice_id();    
  int slice_end_index = metadata.slice_id() + num_slices-1;
  /// Final index of the last slice in whole dataset
  int slice_final_index = slice_beg_index + metadata.num_total_slices()-1;
  int num_grids = metadata.num_grids();


  int recon_slice_data_index =
    metadata.num_neighbor_recon_slices()*
    metadata.num_grids() * metadata.num_grids();
  float *recon = &slices.metadata().recon()[recon_slice_data_index];
  int slice_count = metadata.num_cols() * metadata.num_cols();
  float *suma = &reduction_objects()[0][slice_count];

  float *init_recon_ptr = recon;
  float *init_F_ptr = F;
  float *init_G_ptr = G;
  int remaining_slices = num_slices;
  if(slice_beg_index==0){
    CalculateFGTop(
        init_recon_ptr, init_F_ptr, init_G_ptr, 
        beta, beta1, delta, delta1, regw, 
        num_grids);
    init_recon_ptr = recon + num_grids*num_grids;
    init_F_ptr = F + num_grids*num_grids;
    init_G_ptr = G + num_grids*num_grids;

    --remaining_slices;
  }
  if(slice_final_index==slice_end_index && slice_final_index!=0){
    float *final_slice_ptr = recon + (num_slices-1)*num_grids*num_grids;
    float *final_F_ptr = F + (num_slices-1)*num_grids*num_grids;
    float *final_G_ptr = G + (num_slices-1)*num_grids*num_grids;
    CalculateFGBottom(
        final_slice_ptr, final_F_ptr, final_G_ptr, 
        beta, beta1, delta, delta1, regw, 
        num_grids);
    --remaining_slices;
  }
  CalculateFGInner(
      init_recon_ptr, init_F_ptr, init_G_ptr, 
      beta, beta1, delta, delta1, regw, 
      remaining_slices, num_grids);

  // Calculate G
  int slice_item_count = num_grids*num_grids;
  for (int i=0; i<num_slices; i++) {
    for (int j=0; j<slice_item_count; j++) {
      G[i*slice_item_count+j] += suma[j];
    }
  }

}

/// Forward Projection
float APMLRReconSpace::CalculateSimdata(
    float *recon,
    int len,
    int *indi,
    float *leng)
{
  float simdata = 0.;
  for(int i=0; i<len-1; ++i)
    simdata += recon[indi[i]]*leng[i];
  return simdata;
}


void APMLRReconSpace::UpdateReconReplica(
    float simdata,
    float ray,
    float *recon,
    int curr_slice,
    int const * const indi,
    float *leng, 
    int len,
    int suma_beg_offset)
{
  auto &slice_t = reduction_objects()[curr_slice];
  auto slice = &slice_t[0] + suma_beg_offset;

  for (int i=0; i<len-1; ++i) {
    if (indi[i] >= suma_beg_offset) continue;
    slice[indi[i]] += leng[i];
  }
  slice -= suma_beg_offset;

  float upd = ray/simdata;
  for (int i=0; i <len-1; ++i) {
    if (indi[i] >= suma_beg_offset) continue;
    slice[indi[i]] -= recon[indi[i]]*leng[i]*upd;
  }
}

void APMLRReconSpace::Initialize(int n_grids){
  num_grids = n_grids; 

  coordx = new float[num_grids+1]; 
  coordy = new float[num_grids+1];
  ax = new float[num_grids+1];
  ay = new float[num_grids+1];
  bx = new float[num_grids+1];
  by = new float[num_grids+1];
  coorx = new float[2*num_grids];
  coory = new float[2*num_grids];
  leng = new float[2*num_grids];
  indi = new int[2*num_grids];
}

void APMLRReconSpace::Finalize(){
  delete [] coordx;
  delete [] coordy;
  delete [] ax;
  delete [] ay;
  delete [] bx;
  delete [] by;
  delete [] coorx;
  delete [] coory;
  delete [] leng;
  delete [] indi;
}

void APMLRReconSpace::Reduce(MirroredRegionBareBase<float> &input)
{
  auto &rays = *(static_cast<MirroredRegionBase<float, TraceMetadata>*>(&input));
  auto &metadata = rays.metadata();

  const float *theta = metadata.theta();
  const float *gridx = metadata.gridx();
  const float *gridy = metadata.gridy();
  float mov = metadata.mov();

  /* In-memory values */
  int num_cols = metadata.num_cols();
  int num_grids = metadata.num_cols();

  int curr_proj = metadata.RayProjection(rays.index());
  int count_projs = 
    metadata.RayProjection(rays.index()+rays.count()-1) - curr_proj;

  /* Reconstruction start */
  for (int proj = curr_proj; proj<=(curr_proj+count_projs); ++proj) {
    float theta_q = theta[proj];
    int quadrant = trace_utils::CalculateQuadrant(theta_q);
    float sinq = sinf(theta_q);
    float cosq = cosf(theta_q);

    int curr_slice = metadata.RaySlice(rays.index());
    int curr_slice_offset = curr_slice*num_grids*num_grids;
    float *recon = (&(metadata.recon()[0])+curr_slice_offset);

    for (int curr_col=0; curr_col<num_cols; ++curr_col) {
      /// Calculate coordinates
      float xi = -1e6;
      float yi = (1-num_cols)/2. + curr_col+mov;
      trace_utils::CalculateCoordinates(
          num_grids, 
          xi, yi, sinq, cosq, 
          gridx, gridy, 
          coordx, coordy);  /// Outputs coordx and coordy

      /// Merge the (coordx, gridy) and (gridx, coordy)
      /// Output alen and after
      int alen, blen;
      trace_utils::MergeTrimCoordinates(
          num_grids, 
          coordx, coordy, 
          gridx, gridy, 
          &alen, &blen, 
          ax, ay, bx, by);

      /// Sort the array of intersection points (ax, ay)
      /// The new sorted intersection points are
      /// stored in (coorx, coory).
      /// if quadrant=1 then a_ind = i; if 0 then a_ind = (alen-1-i)
      trace_utils::SortIntersectionPoints(
          quadrant, 
          alen, blen, 
          ax, ay, bx, by, 
          coorx, coory);

      /// Calculate the distances (leng) between the
      /// intersection points (coorx, coory). Find
      /// the indices of the pixels on the
      /// reconstruction grid (ind_recon).
      int len = alen + blen;
      trace_utils::CalculateDistanceLengths(
          len, 
          num_grids, 
          coorx, coory, 
          leng,
          indi);

      /*******************************************************/
      /* Below is for updating the reconstruction grid and
       * is algorithm specific part.
       */
      /// Forward projection
      float simdata = CalculateSimdata(recon, len, indi, leng);

      /// Update recon
      int suma_beg_offset = num_grids*num_grids;
      UpdateReconReplica(
          simdata, 
          rays[curr_col], 
          recon, 
          curr_slice, 
          indi, 
          leng,
          len, 
          suma_beg_offset);
      /*******************************************************/
    }
  }
}
