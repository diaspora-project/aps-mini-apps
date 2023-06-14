#ifndef server_h
#define server_h
int test_float(float *mydata, int myn, int a, int b);

int init_tmq ();
int finalize_tmq();
int done_image();
int push_image(float *data, int n, int row, int col, float theta, int id, float center);
int handshake(char *bindip, int port, int row, int col);
int setup_mock_data(char *fp, int nsubsets);
int whatsup();
#endif
