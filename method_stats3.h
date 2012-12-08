extern long long total_alpha_bits;
extern long long total_nonalpha_bits;
extern long long total_case_bits;
extern long long total_model_bits;
extern long long total_length_bits;
extern long long total_finalisation_bits;

int stats3_compress(unsigned char *in,int inlen,unsigned char *out, int *outlen,
		    stats_handle *h);
int stats3_compress_bits(range_coder *c,unsigned char *m,int len,stats_handle *h,
			 double *entropyLog);
int stats3_decompress(unsigned char *in,int inlen,unsigned char *out, int *outlen,
		      stats_handle *h);
int stats3_decompress_bits(range_coder *c,unsigned char m[1025],int *len_out,
			   stats_handle *h,double *entropyLog);


