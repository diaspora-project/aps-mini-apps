%module tracemq
%{
#define SWIG_FILE_WITH_INIT
#include "server.h"
%}
%include "numpy.i"
%init %{
import_array();
%}


%apply (float* IN_ARRAY1, int DIM1) {(float* mydata, int myn)};
extern int test_float(float *mydata, int myn, int a, int b);

extern int init_tmq ();
extern int finalize_tmq();
extern int done_image();

%apply (float* IN_ARRAY1, int DIM1) {(float* data, int n)};
extern int push_image(float *data, int n, int row, int col, float theta, int id, float center);
extern int handshake(char *bindip, int port, int row, int col);
extern int setup_mock_data(char *fp, int nsubsets);
extern int get_num_workers();
extern int whatsup();
