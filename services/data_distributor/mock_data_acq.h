#ifndef _STREAMER_MOCK_DATA_ACQ_H
#define _STREAMER_MOCK_DATA_ACQ_H

#include <time.h>

typedef struct timespec mock_interval_t;

typedef struct {
  float *projs;             // Projection data
  float *thetas;            // Theta data
  int *dims;                // Always 3 dimensional: #Projs, #Sinogs, #Cols
  int curr_proj_index;      // Current projection index
  int *proj_ids;            // Projection real indices
  mock_interval_t interval; // Data acquisition rate in {secs, nanosecs}
} dacq_file_t;

typedef struct {
  int *dims;                // Projection dimensions, consist of two ints (2D)
  float theta;              // theta value of this projection
  int id;
  float *data;              // ray-sum values of the projection
} ts_proj_data_t;           // Trace stream projection data

/* Reads projection file.
 *
 * Projection file follows following structure:
 *  Initial three data items correspond to dimensions of the data, dims[] (int)
 *  Then, following items are projection data, projs (float *)
 *  Then, following items are theta values, theta (float)
 *
 */
dacq_file_t* mock_dacq_file(char *fp,
                            mock_interval_t);

/* Reorganizes given projection dataset to interleaded one.
 *
 * @param nsubset defines the number of subsets that projections will be
 * distributed to.
 *
 * @return reorganized dacq_file_t pointer.
 */
dacq_file_t* mock_dacq_interleaved( dacq_file_t *dacqd, 
                                    int nsubset);

dacq_file_t* mock_dacq_interleaved_equally(dacq_file_t *dacqd);

/* Simulates data acquisition, returns read data. */
ts_proj_data_t* mock_dacq_read(dacq_file_t *df);

/* Frees resources */
void mock_dacq_file_delete(dacq_file_t *df);

void extract_sinogram(char *fp);



#endif    // _STREAMER_MOCK_DATA_ACQ_H
