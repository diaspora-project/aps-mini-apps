#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "mock_data_acq.h"

void extract_sinogram(char *fp)
{
  int ndims=3;

  FILE *f;
  f = fopen(fp, "rb");
  if(!f){
    printf("Unable to open file!\n");
    exit(0);
  }

  /// Read dims[3]: first three integers
  int *dims = (int*) malloc(ndims*sizeof(*dims));
  int rc = fread(dims, sizeof(*dims), ndims, f);
  if(rc!=ndims){
    printf("Unable to read dimensions!\n");
    exit(0);
  }

  for(int i=0; i<ndims; ++i) printf("dim[%d]=%d\n", i, dims[i]);

  int pcount = 1;
  for(int i=0; i<ndims; ++i) pcount *= dims[i];
  float *pdata = (float*) malloc(pcount*sizeof(*pdata));
  rc = fread(pdata, sizeof(*pdata), pcount, f);
  if(rc!=pcount){
    printf("Unable to read projection data from file!\n");
    exit(0);
  }

  /// Read theta values
  int tcount = dims[0]; 
  float *tdata = (float*) malloc(tcount*sizeof(*tdata));
  rc = fread(tdata, sizeof(*tdata), tcount, f);
  if(rc!=tcount){
    printf("Unable to read theta data from file!\n");
    exit(0);
  }

  FILE *fo = fopen("new_file.data", "wb");
  int odims[3];
  odims[0] = dims[0]; odims[1]=1; odims[2]=dims[2];
  for(int i=0; i<ndims; ++i){
    printf("d=%d\n", odims[i]);
    fwrite(odims+i, sizeof(int), 1, fo);
  }
  for(int i=0; i<dims[0]; ++i){
    for(int j=0; j<1; ++j){
     for(int k=0; k<dims[2]; ++k){
        size_t off = i*dims[1]*dims[2] + j*dims[2] + k;
        //printf("off=%ld; p=%f\n", off, pdata[off]);
        fwrite(pdata+off, sizeof(*pdata), 1, fo);
      }
    }
  }
  //fseek(fo, pcount-dims[0]*dims[2]*sizeof(float), SEEK_CUR);
  for(int i=0; i<dims[0]; ++i){
    //printf("t=%f\n", tdata[i]);
    fwrite(tdata+i, sizeof(float), 1, fo);
  }
  fclose(fo);
  fclose(f);
  exit(0);

}
dacq_file_t* mock_dacq_file(char *fp, 
                            mock_interval_t interval)
{
  int ndims=3;

  FILE *f;
  f = fopen(fp, "rb");
  if(!f){
    printf("Unable to open file!\n");
    return NULL;
  }

  /// Read dims[3]: first three integers
  int *dims = (int*) malloc(ndims*sizeof(*dims));
  int rc = fread(dims, sizeof(*dims), ndims, f);
  if(rc!=ndims){
    printf("Unable to read dimensions!\n");
    return NULL;
  }

  for(int i=0; i<ndims; ++i) printf("dim[%d]=%d\n", i, dims[i]);

  int pcount = 1;
  for(int i=0; i<ndims; ++i) pcount *= dims[i];
  float *pdata = (float*) malloc(pcount*sizeof(*pdata));
  rc = fread(pdata, sizeof(*pdata), pcount, f);
  if(rc!=pcount){
    printf("Unable to read projection data from file!\n");
    return NULL;
  }
  /// Print first ten projection data values
  for(int i=0; i<10; ++i) printf("val[%d]=%.10e;\n", i, pdata[i]);

  /// Read theta values
  int tcount = dims[0]; 
  float *tdata = (float*) malloc(tcount*sizeof(*tdata));
  rc = fread(tdata, sizeof(*tdata), tcount, f);
  if(rc!=tcount){
    printf("Unable to read theta data from file!\n");
    return NULL;
  }
  /// Print first ten theta values
  for(int i=0; i<10; ++i) printf("val[%d]=%f;\n", i, tdata[i]);
  /// This should be the end of file, check this later

  /// Set projection ids, these change when interleaved data acquisition
  /// is simulated.
  int *proj_ids = (int *) malloc(sizeof(*proj_ids)*dims[0]);
  for(int i=0; i<dims[0]; ++i) proj_ids[i]=i;

  /// Prepare data structure to be returned
  dacq_file_t *dacq = (dacq_file_t*) malloc(sizeof(*dacq));
  dacq->projs = pdata;
  dacq->thetas = tdata;
  dacq->dims = dims;
  dacq->interval = interval;
  dacq->curr_proj_index = 0;
  dacq->proj_ids = proj_ids;

  fclose(f);
  return dacq;
}

ts_proj_data_t* mock_dacq_read(dacq_file_t *df)
{
  /// Check if this is the end of data acquisition
  if(df->curr_proj_index==df->dims[0]) return NULL;

  /// Sleep as much as time interval
  nanosleep(&(df->interval), NULL);  // No interrupt is expected

  ts_proj_data_t *proj = (ts_proj_data_t*) malloc(sizeof(*proj));
  proj->dims = &(df->dims[1]);
  proj->data = df->projs + 
                df->curr_proj_index * proj->dims[0] * proj->dims[1]; 
  proj->theta = df->thetas[df->curr_proj_index];
  proj->id = df->proj_ids[df->curr_proj_index++];

  return proj;
}

void mock_dacq_file_delete(dacq_file_t *df)
{
  free(df->projs);
  free(df->thetas);
  free(df->dims);
  free(df->proj_ids);
  free(df);
}

/// Copies n data pointed by src to dest according to index ortanization ix 
void mock_gen_interleaved(int *ix, int n, size_t nitem, 
                          float *src, float *dest)
{
  for(int i=0; i<n; ++i){
    float *curr_src = src + ix[i]*nitem;
    float *curr_dest = dest + i*nitem;
    memcpy((void*)(curr_dest), (void*)curr_src, nitem*sizeof(float));
  }
}

void add_indices(int *indices, int *l_index, float step, float upper){
  //printf("step=%f\n", step);
  int beg=0;
  while(beg<upper){
    int exist = 0;
    for(int i=0; i<*l_index; ++i){
      if(indices[i]==beg) exist=1;
    }
    if(!exist) {
      printf("adding=%d\n", beg);
      indices[(*l_index)++]=beg;
    }
    beg+=step;
  }
}

void print_indices(int *indices, int indices_count){
  for(int i=0; i<indices_count; ++i){
    printf("index[%d]=%d\n", i, indices[i]);
  }
}

dacq_file_t* mock_dacq_interleaved_equally(dacq_file_t *dacqd)
{
  dacq_file_t *new_dacqd = (dacq_file_t*) malloc(sizeof(dacq_file_t));
  int *dims = dacqd->dims;

  /// Copy dimensions
  new_dacqd->dims = (int*) malloc(sizeof(int)*3);
  memcpy((void*)new_dacqd->dims, (void*)dims, 3*sizeof(*dims));


  /// Calculate new indices
  int num_projs=dims[0];
  int *proj_ix = (int*)malloc(sizeof(int)*num_projs);
  int l_index= 0;
  size_t rcp = dims[1]*dims[2];

  float co=num_projs/2.;
  while(((int)co)!=0){
    add_indices(proj_ix, &l_index, co, num_projs);
    //printf("co=%f; l_index=%d\n", co, l_index);
    co = floor(co/2.);
  }
  //assert((l_index)==dims[0]);
  print_indices(proj_ix, l_index);
  //exit(0);

  /// Copy data
  new_dacqd->projs = (float*) malloc(sizeof(float)*num_projs*rcp);
  mock_gen_interleaved(proj_ix, num_projs, rcp, dacqd->projs, new_dacqd->projs);
  /// Copy theta
  new_dacqd->thetas = (float*) malloc(sizeof(float)*num_projs);
  mock_gen_interleaved(proj_ix, num_projs, 1, dacqd->thetas, new_dacqd->thetas);

  new_dacqd->curr_proj_index = 0;
  new_dacqd->interval = dacqd->interval;
  new_dacqd->proj_ids = proj_ix;

  return new_dacqd;
}


dacq_file_t* mock_dacq_interleaved( dacq_file_t *dacqd, 
                                    int nsubsets)
{
  dacq_file_t *new_dacqd = (dacq_file_t*) malloc(sizeof(dacq_file_t));
  int *dims = dacqd->dims;

  /// Copy dimensions
  new_dacqd->dims = (int*) malloc(sizeof(int)*3);
  memcpy((void*)new_dacqd->dims, (void*)dims, 3*sizeof(*dims));

  /// Calculate new indices
  int num_projs = dims[0];
  size_t rcp = dims[1]*dims[2];
  int rem = num_projs%nsubsets;

  int *proj_ix = (int*) malloc(sizeof(int)*num_projs);
  for(int i=0, index=0; i<nsubsets; ++i){
    int count = (num_projs/nsubsets)+(i<rem);
    for(int j=0; j<count; ++j){
      proj_ix[index++]=(j*nsubsets)+i;
    }
  }

  /// Copy data
  new_dacqd->projs = (float*) malloc(sizeof(float)*num_projs*rcp);
  mock_gen_interleaved(proj_ix, num_projs, rcp, dacqd->projs, new_dacqd->projs);
  /// Copy theta
  new_dacqd->thetas = (float*) malloc(sizeof(float)*num_projs);
  mock_gen_interleaved(proj_ix, num_projs, 1, dacqd->thetas, new_dacqd->thetas);

  new_dacqd->curr_proj_index = 0;
  new_dacqd->interval = dacqd->interval;
  new_dacqd->proj_ids = proj_ix;

  return new_dacqd;
}
