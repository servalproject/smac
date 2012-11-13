extern long long total_alpha_bits;
extern long long total_nonalpha_bits;
extern long long total_case_bits;
extern long long total_model_bits;
extern long long total_length_bits;
extern long long total_finalisation_bits;

int stats3_compress(range_coder *c,unsigned char *m);
int stats3_decompress(range_coder *c,unsigned char m[1025],int *len);
