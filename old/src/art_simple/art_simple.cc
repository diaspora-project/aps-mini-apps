#include <cmath>    // For std::floor, sqrtf, fmodf, sinf, cosf
#include <cassert>  // For assert
#include <cstring>  // For memset
#include <cstdlib>  // For malloc, free
#include <iostream> // For std::cout, std::cerr, std::endl

void
preprocessing(int ry, int rz, int num_pixels, float center, float* mov, float* gridx,
              float* gridy)
{
    for(int i = 0; i <= ry; ++i)
    {
        gridx[i] = -ry * 0.5f + i;
    }

    for(int i = 0; i <= rz; ++i)
    {
        gridy[i] = -rz * 0.5f + i;
    }

    *mov = ((float) num_pixels - 1) * 0.5f - center;
    if(*mov - floor(*mov) < 0.01f)
    {
        *mov += 0.01f;
    }
    *mov += 0.5;
}

//======================================================================================//

int
calc_quadrant(float theta_p)
{
    // here we cast the float to an integer and rescale the integer to
    // near INT_MAX to retain the precision. This method was tested
    // on 1M random random floating points between -2*pi and 2*pi and
    // was found to produce a speed up of:
    //
    //  - 14.5x (Intel i7 MacBook)
    //  - 2.2x  (NERSC KNL)
    //  - 1.5x  (NERSC Edison)
    //  - 1.7x  (NERSC Haswell)
    //
    // with a 0.0% incorrect quadrant determination rate
    //
    const int32_t ipi_c   = 340870420;
    int32_t       theta_i = (int32_t)(theta_p * ipi_c);
    theta_i += (theta_i < 0) ? (2.0f * M_PI * ipi_c) : 0;

    return ((theta_i >= 0 && theta_i < 0.5f * M_PI * ipi_c) ||
            (theta_i >= 1.0f * M_PI * ipi_c && theta_i < 1.5f * M_PI * ipi_c))
               ? 1
               : 0;
}

//======================================================================================//

void
calc_coords(int ry, int rz, float xi, float yi, float sin_p, float cos_p,
            const float* gridx, const float* gridy, float* coordx, float* coordy)
{
    float srcx   = xi * cos_p - yi * sin_p;
    float srcy   = xi * sin_p + yi * cos_p;
    float detx   = -xi * cos_p - yi * sin_p;
    float dety   = -xi * sin_p + yi * cos_p;
    float slope  = (srcy - dety) / (srcx - detx);
    float islope = (srcx - detx) / (srcy - dety);

#pragma omp simd
    for(int n = 0; n <= rz; ++n)
    {
        coordx[n] = islope * (gridy[n] - srcy) + srcx;
    }
#pragma omp simd
    for(int n = 0; n <= ry; ++n)
    {
        coordy[n] = slope * (gridx[n] - srcx) + srcy;
    }
}

//======================================================================================//

void
trim_coords(int ry, int rz, const float* coordx, const float* coordy, const float* gridx,
            const float* gridy, int* asize, float* ax, float* ay, int* bsize, float* bx,
            float* by)
{
    *asize         = 0;
    *bsize         = 0;
    float gridx_gt = gridx[0] + 0.01f;
    float gridx_le = gridx[ry] - 0.01f;

    for(int n = 0; n <= rz; ++n)
    {
        if(coordx[n] >= gridx_gt && coordx[n] <= gridx_le)
        {
            ax[*asize] = coordx[n];
            ay[*asize] = gridy[n];
            ++(*asize);
        }
    }

    float gridy_gt = gridy[0] + 0.01f;
    float gridy_le = gridy[rz] - 0.01f;

    for(int n = 0; n <= ry; ++n)
    {
        if(coordy[n] >= gridy_gt && coordy[n] <= gridy_le)
        {
            bx[*bsize] = gridx[n];
            by[*bsize] = coordy[n];
            ++(*bsize);
        }
    }
}

//======================================================================================//

void
sort_intersections(int ind_condition, int asize, const float* ax, const float* ay,
                   int bsize, const float* bx, const float* by, int* csize, float* coorx,
                   float* coory)
{
    int i = 0, j = 0, k = 0;
    if(ind_condition == 0)
    {
        while(i < asize && j < bsize)
        {
            if(ax[asize - 1 - i] < bx[j])
            {
                coorx[k] = ax[asize - 1 - i];
                coory[k] = ay[asize - 1 - i];
                ++i;
            }
            else
            {
                coorx[k] = bx[j];
                coory[k] = by[j];
                ++j;
            }
            ++k;
        }

        while(i < asize)
        {
            coorx[k] = ax[asize - 1 - i];
            coory[k] = ay[asize - 1 - i];
            ++i;
            ++k;
        }
        while(j < bsize)
        {
            coorx[k] = bx[j];
            coory[k] = by[j];
            ++j;
            ++k;
        }

        (*csize) = asize + bsize;
    }
    else
    {
        while(i < asize && j < bsize)
        {
            if(ax[i] < bx[j])
            {
                coorx[k] = ax[i];
                coory[k] = ay[i];
                ++i;
            }
            else
            {
                coorx[k] = bx[j];
                coory[k] = by[j];
                ++j;
            }
            ++k;
        }

        while(i < asize)
        {
            coorx[k] = ax[i];
            coory[k] = ay[i];
            ++i;
            ++k;
        }
        while(j < bsize)
        {
            coorx[k] = bx[j];
            coory[k] = by[j];
            ++j;
            ++k;
        }
        (*csize) = asize + bsize;
    }
}

//======================================================================================//

void
calc_dist(int ry, int rz, int csize, const float* coorx, const float* coory, int* indi,
          float* dist)
{
    if(csize < 2)
        return;
    const int _size = csize - 1;

    //------------------------------------------------------------------------//
    //              calculate dist
    //------------------------------------------------------------------------//
    {
        float* _diffx = (float*)malloc(_size * sizeof(float));
        float* _diffy = (float*)malloc(_size * sizeof(float));

#pragma omp simd
        for(int n = 0; n < _size; ++n)
        {
            _diffx[n] = (coorx[n + 1] - coorx[n]) * (coorx[n + 1] - coorx[n]);
        }

#pragma omp simd
        for(int n = 0; n < _size; ++n)
        {
            _diffy[n] = (coory[n + 1] - coory[n]) * (coory[n + 1] - coory[n]);
        }

#pragma omp simd
        for(int n = 0; n < _size; ++n)
        {
            dist[n] = sqrtf(_diffx[n] + _diffy[n]);
        }

        free(_diffx);
        free(_diffy);
    }

    //------------------------------------------------------------------------//
    //              calculate indi
    //------------------------------------------------------------------------//

    int* _indx = (int*)malloc(_size * sizeof(int));
    int* _indy = (int*)malloc(_size * sizeof(int));

#pragma omp simd
    for(int n = 0; n < _size; ++n)
    {
        float _midx = 0.5f * (coorx[n + 1] + coorx[n]);
        float _x1   = _midx + 0.5f * ry;
        float _i1   = (int) (_midx + 0.5f * ry);
        _indx[n]    = _i1 - (_i1 > _x1);
    }
#pragma omp simd
    for(int n = 0; n < _size; ++n)
    {
        float _midy = 0.5f * (coory[n + 1] + coory[n]);
        float _x2   = _midy + 0.5f * rz;
        float _i2   = (int) (_midy + 0.5f * rz);
        _indy[n]    = _i2 - (_i2 > _x2);
    }
#pragma omp simd
    for(int n = 0; n < _size; ++n)
    {
        indi[n] = _indy[n] + (_indx[n] * rz);
    }

    free(_indx);
    free(_indy);
}

//======================================================================================//

void
calc_dist2(int ry, int rz, int csize, const float* coorx, const float* coory, int* indx,
           int* indy, float* dist)
{
#pragma omp simd
    for(int n = 0; n < csize - 1; ++n)
    {
        float diffx = coorx[n + 1] - coorx[n];
        float diffy = coory[n + 1] - coory[n];
        dist[n]     = sqrt(diffx * diffx + diffy * diffy);
    }

#pragma omp simd
    for(int n = 0; n < csize - 1; ++n)
    {
        float midx = (coorx[n + 1] + coorx[n]) * 0.5f;
        float midy = (coory[n + 1] + coory[n]) * 0.5f;
        float x1   = midx + ry * 0.5f;
        float x2   = midy + rz * 0.5f;
        int   i1   = (int) (midx + ry * 0.5f);
        int   i2   = (int) (midy + rz * 0.5f);
        indx[n]    = i1 - (i1 > x1);
        indy[n]    = i2 - (i2 > x2);
    }
}

//======================================================================================//

void
calc_simdata(int s, int p, int d, int ry, int rz, int dt, int dx, int csize,
             const int* indi, const float* dist, const float* model, float* simdata)
{
    int index_model = s * ry * rz;
    int index_data  = d + p * dx + s * dt * dx;
    for(int n = 0; n < csize - 1; ++n)
    {
        simdata[index_data] += model[indi[n] + index_model] * dist[n];
    }
}

//======================================================================================//

void
calc_simdata2(int s, int p, int d, int ry, int rz, int dt, int dx, int csize,
              const int* indx, const int* indy, const float* dist, float vx, float vy,
              const float* modelx, const float* modely, float* simdata)
{
    int n;

    for(n = 0; n < csize - 1; n++)
    {
        simdata[d + p * dx + s * dt * dx] +=
            (modelx[indy[n] + indx[n] * rz + s * ry * rz] * vx +
             modely[indy[n] + indx[n] * rz + s * ry * rz] * vy) *
            dist[n];
    }
}

//======================================================================================//

void
calc_simdata3(int s, int p, int d, int ry, int rz, int dt, int dx, int csize,
              const int* indx, const int* indy, const float* dist, float vx, float vy,
              const float* modelx, const float* modely, const float* modelz, int axis,
              float* simdata)
{
    int n;

    if(axis == 0)
    {
        for(n = 0; n < csize - 1; n++)
        {
            simdata[d + p * dx + s * dt * dx] +=
                (modelx[indy[n] + indx[n] * rz + s * ry * rz] * vx +
                 modely[indy[n] + indx[n] * rz + s * ry * rz] * vy) *
                dist[n];
        }
    }
    else if(axis == 1)
    {
        for(n = 0; n < csize - 1; n++)
        {
            simdata[d + p * dx + s * dt * dx] +=
                (modely[s + indx[n] * rz + indy[n] * ry * rz] * vx +
                 modelz[s + indx[n] * rz + indy[n] * ry * rz] * vy) *
                dist[n];
        }
    }
    else if(axis == 2)
    {
        for(n = 0; n < csize - 1; n++)
        {
            simdata[d + p * dx + s * dt * dx] +=
                (modelx[indx[n] + s * rz + indy[n] * ry * rz] * vx +
                 modelz[indx[n] + s * rz + indy[n] * ry * rz] * vy) *
                dist[n];
        }
    }
}

//======================================================================================//

void
art(const float* data, int dy, int dt, int dx, const float* center, const float* theta,
    float* recon, int ngridx, int ngridy, int num_iter)
{
    if(dy == 0 || dt == 0 || dx == 0)
        return;

    float* gridx   = (float*) malloc((ngridx + 1) * sizeof(float));
    float* gridy   = (float*) malloc((ngridy + 1) * sizeof(float));
    float* coordx  = (float*) malloc((ngridy + 1) * sizeof(float));
    float* coordy  = (float*) malloc((ngridx + 1) * sizeof(float));
    float* ax      = (float*) malloc((ngridx + ngridy) * sizeof(float));
    float* ay      = (float*) malloc((ngridx + ngridy) * sizeof(float));
    float* bx      = (float*) malloc((ngridx + ngridy) * sizeof(float));
    float* by      = (float*) malloc((ngridx + ngridy) * sizeof(float));
    float* coorx   = (float*) malloc((ngridx + ngridy) * sizeof(float));
    float* coory   = (float*) malloc((ngridx + ngridy) * sizeof(float));
    float* dist    = (float*) malloc((ngridx + ngridy) * sizeof(float));
    int*   indi    = (int*) malloc((ngridx + ngridy) * sizeof(int));
    float* simdata = (float*) malloc((dy * dt * dx) * sizeof(float));

    assert(coordx != NULL && coordy != NULL && ax != NULL && ay != NULL && by != NULL &&
           bx != NULL && coorx != NULL && coory != NULL && dist != NULL && indi != NULL &&
           simdata != NULL);

    int   s, p, d, i, n;
    int   quadrant;
    float theta_p, sin_p, cos_p;
    float mov, xi, yi;
    int   asize, bsize, csize;
    float upd;
    int   ind_data, ind_recon;

    for(i = 0; i < num_iter; i++)
    {

        std::cout << "Iteration: " << i << std::endl;

        // initialize simdata to zero
        memset(simdata, 0, dy * dt * dx * sizeof(float));

        preprocessing(ngridx, ngridy, dx, center[0], &mov, gridx,
                      gridy);  // Outputs: mov, gridx, gridy

        // For each projection angle
        for(p = 0; p < dt; p++)
        {

            // Calculate the sin and cos values
            // of the projection angle and find
            // at which quadrant on the cartesian grid.
            theta_p  = fmodf(theta[p], 2.0f * (float) M_PI);
            quadrant = calc_quadrant(theta_p);
            sin_p    = sinf(theta_p);
            cos_p    = cosf(theta_p);

            // For each detector pixel
            for(d = 0; d < dx; d++)
            {
                // Calculate coordinates
                xi = -ngridx - ngridy;
                yi = 0.5f * (1 - dx) + d + mov;
                calc_coords(ngridx, ngridy, xi, yi, sin_p, cos_p, gridx, gridy, coordx,
                            coordy);

                // Merge the (coordx, gridy) and (gridx, coordy)
                trim_coords(ngridx, ngridy, coordx, coordy, gridx, gridy, &asize, ax, ay,
                            &bsize, bx, by);

                // Sort the array of intersection points (ax, ay) and
                // (bx, by). The new sorted intersection points are
                // stored in (coorx, coory). Total number of points
                // are csize.
                sort_intersections(quadrant, asize, ax, ay, bsize, bx, by, &csize, coorx,
                                   coory);

                // Calculate the distances (dist) between the
                // intersection points (coorx, coory). Find the
                // indices of the pixels on the reconstruction grid.
                calc_dist(ngridx, ngridy, csize, coorx, coory, indi, dist);

                // Calculate dist*dist
                float sum_dist2 = 0.0f;
                for(n = 0; n < csize - 1; n++)
                {
                    sum_dist2 += dist[n] * dist[n];
                }

                if(sum_dist2 != 0.0f)
                {
                    // For each slice
                    for(s = 0; s < dy; s++)
                    {
                        // Calculate simdata
                        calc_simdata(s, p, d, ngridx, ngridy, dt, dx, csize, indi, dist,
                                     recon,
                                     simdata);  // Output: simdata

                        // Update
                        ind_data  = d + p * dx + s * dt * dx;
                        ind_recon = s * ngridx * ngridy;
                        upd       = (data[ind_data] - simdata[ind_data]) / sum_dist2;
                        for(n = 0; n < csize - 1; n++)
                        {
                            recon[indi[n] + ind_recon] += upd * dist[n];
                        }
                    }
                }
            }
        }
    }
    free(gridx);
    free(gridy);
    free(coordx);
    free(coordy);
    free(ax);
    free(ay);
    free(bx);
    free(by);
    free(coorx);
    free(coory);
    free(dist);
    free(indi);
    free(simdata);
}
