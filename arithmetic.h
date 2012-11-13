/*
Copyright (C) 2012 Paul Gardner-Stephen
 
This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
 
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
 
You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

typedef struct range_coder {
  unsigned int low;
  unsigned int high;
  unsigned int value;
  int underflow;
  int errors;
  int decodingP;
  char *debug;

  /* if non-zero, prevents use of underflow/overflow rescaling */
  int norescale;

  double entropy;

  unsigned char *bit_stream;
  int bit_stream_length;  
  unsigned int bits_used;
} range_coder;

int range_coder_reset(struct range_coder *c);
int range_emit_stable_bits(range_coder *c);
int range_encode(range_coder *c,unsigned int p_low,unsigned int p_high);
int range_status(range_coder *c,int decoderP);
int range_encode_symbol(range_coder *c,unsigned int frequencies[],int alphabet_size,int symbol);
int range_encode_equiprobable(range_coder *c,int alphabet_size,int symbol);
int range_decode_equiprobable(range_coder *c,int alphabet_size);
int range_decode_common(range_coder *c,unsigned int p_low,unsigned int p_high,int s);
int range_decode_symbol(range_coder *c,unsigned int frequencies[],int alphabet_size);
int range_decode_getnextbit(range_coder *c);
struct range_coder *range_new_coder(int bytes);
int range_encode_length(range_coder *c,int len);
int range_conclude(range_coder *c);
int range_coder_free(range_coder *c);
range_coder *range_coder_dup(range_coder *in);
int range_rescale(range_coder *c);
int range_unrescale_value(unsigned int v,int underflow_bits);
int range_decode_prefetch(range_coder *c);

int ic_encode_recursive(int *list,
			int list_length,
			int max_value,
			range_coder *c);
int ic_decode_recursive(int *list,
			int list_length,
			int max_value,
			range_coder *c);
