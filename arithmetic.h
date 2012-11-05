typedef struct range_coder {
  unsigned int low;
  unsigned int high;
  unsigned int value;
  int underflow;

  double entropy;

  unsigned char *bit_stream;
  int bit_stream_length;  
  unsigned int bits_used;
} range_coder;

int range_emitbit(range_coder *c,int b);
int range_emitbits(range_coder *c,int n);
int range_emit_stable_bits(range_coder *c);
int range_encode(range_coder *c,double p_low,double p_high);
int range_status(range_coder *c,int decoderP);
int range_encode_symbol(range_coder *c,double frequencies[],int alphabet_size,int symbol);
int range_decode_symbol(range_coder *c,double frequencies[],int alphabet_size);
int range_decode_getnextbit(range_coder *c);
