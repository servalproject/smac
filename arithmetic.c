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


#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <math.h>
#include "arithmetic.h"

#define MAXVALUE 0xffffff
#define MAXVALUEPLUS1 (MAXVALUE+1)
#define MSBVALUE 0x800000
#define MSBVALUEMINUS1 (MSBVALUE-1)
#define HALFMSBVALUE (MSBVALUE>>1)
#define HALFMSBVALUEMINUS1 (HALFMSBVALUE-1)
#define SIGNIFICANTBITS 24
#define SHIFTUPBITS (32LL-SIGNIFICANTBITS)

int range_calc_new_range(range_coder *c,
			 unsigned int p_low, unsigned int p_high,
			 unsigned int *new_low,unsigned int *new_high);
int range_emitbit(range_coder *c,int b);

int bits2bytes(int b)
{
  int extra=0;
  if (b&7) extra=1;
  return (b>>3)+extra;
}

int range_encode_length(range_coder *c,int len)
{
  int bits=0,i;
  while((1<<bits)<len) {
    range_encode_equiprobable(c,2,1);
    bits++;
  }
  range_encode_equiprobable(c,2,0);
  /* MSB must be 1, so we don't need to output it, 
     just the lower order bits. */
  for(i=bits-1;i>=0;i--) range_encode_equiprobable(c,2,(len>>i)&1);
  return 0;
}

int range_decode_getnextbit(range_coder *c)
{
  /* return 0s once we have used all bits */
  if (c->bit_stream_length<=c->bits_used) {
    c->bits_used++;
    return 0;
  }

  int bit=c->bit_stream[c->bits_used>>3]&(1<<(7-(c->bits_used&7)));
  c->bits_used++;
  if (bit) return 1;
  return 0;
}

int range_emitbit(range_coder *c,int b)
{
  if (c->bits_used>=(c->bit_stream_length)) {
    printf("out of bits\n");
    exit(-1);
    return -1;
  }
  int bit=(c->bits_used&7)^7;
  if (bit==7) c->bit_stream[c->bits_used>>3]=0;
  if (b) c->bit_stream[c->bits_used>>3]|=(b<<bit);
  else c->bit_stream[c->bits_used>>3]&=~(b<<bit);
  c->bits_used++;
  return 0;
}

int range_emit_stable_bits(range_coder *c)
{
  range_check(c,__LINE__);
  /* look for actually stable bits, i.e.,msb of low and high match */
  while (!((c->low^c->high)&0x80000000))
    {
      int msb=c->low>>31;
      if (0)
	printf("emitting stable bit = %d @ bit %d\n",msb,c->bits_used);

      if (!c->decodingP) if (range_emitbit(c,msb)) return -1;
      if (c->underflow) {
	int u;
	if (msb) u=0; else u=1;
	while (c->underflow-->0) {	  
	  if (0)
	    printf("emitting underflow bit = %d @ bit %d\n",u,c->bits_used);

	  if (!c->decodingP) if (range_emitbit(c,u)) return -1;
	}
	c->underflow=0;
      }
      if (c->decodingP) {
	// printf("value was 0x%08x (low=0x%08x, high=0x%08x)\n",c->value,c->low,c->high);
      }
      c->low=c->low<<1;
      c->high=c->high<<1;      
      c->high|=1;
      if (c->decodingP) {
	c->value=c->value<<1;
	int nextbit=range_decode_getnextbit(c);
	c->value|=nextbit;
	// printf("value became 0x%08x (low=0x%08x, high=0x%08x), nextbit=%d\n",c->value,c->low,c->high,nextbit);
      }
      range_check(c,__LINE__);
    }

  /* Now see if we have underflow, and need to count the number of underflowed
     bits. */
  if (!c->norescale) range_rescale(c);

  return 0;
}

int range_rescale(range_coder *c) {
  
  /* While:
           c->low = 01<rest of bits>
      and c->high = 10<rest of bits>

     shift out the 2nd bit, so that we are left with:

           c->low = 0<rest of bits>0
	  c->high = 1<rest of bits>1
  */
  while (((c->low>>30)==0x1)&&((c->high>>30)==0x2))
    {
      c->underflow++;
      if (0)
	printf("underflow bit added @ bit %d\n",c->bits_used);

      unsigned int new_low=c->low<<1;
      new_low&=0x7fffffff;
      unsigned int new_high=c->high<<1;
      new_high|=1;
      new_high|=0x80000000;
      if (new_low>=new_high) { 
	fprintf(stderr,"oops\n");
	exit(-1);
      }
      if (c->debug)
	printf("%s: rescaling: old=[0x%08x,0x%08x], new=[0x%08x,0x%08x]\n",
	       c->debug,c->low,c->high,new_low,new_high);

      if (c->decodingP) {
	unsigned int value_bits=((c->value<<1)&0x7ffffffe);
	if (c->debug)
	  printf("value was 0x%08x (low=0x%08x, high=0x%08x), keepbits=0x%08x\n",c->value,c->low,c->high,value_bits);
	c->value=(c->value&0x80000000)|value_bits;
	c->value|=range_decode_getnextbit(c);
      }
      c->low=new_low;
      c->high=new_high;
      if (c->decodingP&&c->debug)
	printf("value became 0x%08x (low=0x%08x, high=0x%08x)\n",c->value,c->low,c->high);
      range_check(c,__LINE__);
    }
  return 0;
}


/* If there are underflow bits, squash them back into 
   the encoder/decoder state.  This is primarily for
   debugging problems with the handling of underflow 
   bits. */
int range_unrescale_value(unsigned int v,int underflow_bits)
{
  int i;
  unsigned int msb=v&0x80000000;
  unsigned int o=msb|((v&0x7fffffff)>>underflow_bits);
  if (!msb) {
    for(i=0;i<underflow_bits;i++) {
      o|=0x40000000>>i;
    }
  }
  if (0)
    printf("0x%08x+%d underflows flattens to 0x%08x\n",
	   v,underflow_bits,o);

  return o;
}
int range_unrescale(range_coder *c)
{
  if(c->underflow) {
    c->low=range_unrescale_value(c->low,c->underflow);
    c->value=range_unrescale_value(c->value,c->underflow);
    c->high=range_unrescale_value(c->high,c->underflow);
    c->underflow=0;
  }
  return 0;
}

int range_emitbits(range_coder *c,int n)
{
  int i;
  for(i=0;i<n;i++)
    {
      if (range_emitbit(c,(c->low>>31))) return -1;
      c->low=c->low<<1;
      c->high=c->high<<1;
      c->high|=1;
    }
  return 0;
}


char bitstring[33];
char *asbits(unsigned int v)
{
  int i;
  bitstring[32]=0;
  for(i=0;i<32;i++)
    if ((v>>(31-i))&1) bitstring[i]='1'; else bitstring[i]='0';
  return bitstring;
}

unsigned long long range_space(range_coder *c)
{
  return ((unsigned long long)c->high-(unsigned long long)c->low)&0xffffff00;
}

int range_encode(range_coder *c,unsigned int p_low,unsigned int p_high)
{
  if (p_low>p_high) {
    fprintf(stderr,"range_encode() called with p_low>p_high: p_low=%u, p_high=%u\n",
	    p_low,p_high);
    exit(-1);
  }
  if (p_low>MAXVALUE||p_high>MAXVALUEPLUS1) {
    fprintf(stderr,"range_encode() called with p_low or p_high >=0x%x: p_low=0x%x, p_high=0x%x\n",
	    MAXVALUE,p_low,p_high);
    exit(-1);
  }

  unsigned int new_low,new_high;
  range_calc_new_range(c,p_low,p_high,&new_low,&new_high);
  
  range_check(c,__LINE__);
  c->low=new_low;
  c->high=new_high;
  range_check(c,__LINE__);

  if (c->debug) {
    printf("%s: space=0x%08llx[%s], new_low=0x%08x, new_high=0x%08x\n",
	   c->debug,range_space(c),asbits(range_space(c)),new_low,new_high);
  }

  unsigned long long p_diff=p_high-p_low;
  unsigned long long p_range=(unsigned long long)p_diff<<(long long)SHIFTUPBITS;
  double p=((double)p_range)/(double)0x100000000;
  double this_entropy=-log(p)/log(2);
  if (0)
    printf("%s: entropy of range 0x%llx(p_low=0x%x, p_high=0x%x, p_diff=0x%llx) = %f, shiftupbits=%lld\n",
	   c->debug,p_range,p_low,p_high,p_diff,this_entropy,SHIFTUPBITS);
  if (this_entropy<0) {
    fprintf(stderr,"entropy of symbol is negative! (p=%f, e=%f)\n",p,this_entropy);
    exit(-1);
  }
  c->entropy+=this_entropy;

  range_check(c,__LINE__);
  if (range_emit_stable_bits(c)) return -1;

  if (c->debug) {
    unsigned long long space=range_space(c);
    printf("%s: after rescale: space=0x%08llx[%s], low=0x%08x, high=0x%08x\n",
	   c->debug,space,asbits(space),c->low,c->high);
  }

  return 0;
}

int range_encode_equiprobable(range_coder *c,int alphabet_size,int symbol)
{
  if (alphabet_size>0xffffff) {
    fprintf(stderr,"%s() passed alphabet_size>0xffffff\n",__FUNCTION__);
    c->errors++;
    exit(-1);
  }
  unsigned int p_low=((symbol+0LL)<<SIGNIFICANTBITS)/alphabet_size;
  unsigned int p_high=((symbol+1LL)<<SIGNIFICANTBITS)/alphabet_size;
  if (symbol==alphabet_size-1) p_high=MAXVALUEPLUS1;
  return range_encode(c,p_low,p_high);
}

char bitstring2[8193];
char *range_coder_lastbits(range_coder *c,int count)
{
  if (count>c->bits_used) {
    count=c->bits_used;
  }
  if (count>8192) count=8192;
  int i;
  int l=0;

  for(i=(c->bits_used-count);i<c->bits_used;i++)
    {
      int byte=i>>3;
      int bit=(i&7)^7;
      bit=c->bit_stream[byte]&(1<<bit);
      if (bit) bitstring2[l++]='1'; else bitstring2[l++]='0';
    }
  bitstring2[l]=0;
  return bitstring2;
}

int range_status(range_coder *c,int decoderP)
{
  unsigned int value = decoderP?c->value:(((unsigned long long)c->high+(unsigned long long)c->low)>>1LL);
  unsigned long long space=range_space(c);
  if (!c) return -1;
  char *prefix=range_coder_lastbits(c,90);
  char spaces[8193];
  int i;
  for(i=0;prefix[i];i++) spaces[i]=' '; 
  if (decoderP&&(i>=32)) i-=32;
  spaces[i]=0; prefix[i]=0;

  printf("range  low: %s%s (offset=%d bits)\n",spaces,asbits(c->low),c->bits_used);
  printf("     value: %s%s (0x%08x/0x%08llx = 0x%08llx)\n",
	 range_coder_lastbits(c,90),decoderP?"":asbits(value),
	 (value-c->low),space,
	 (((unsigned long long)value-(unsigned long long)c->low)<<32LL)/space);
  printf("range high: %s%s\n",spaces,asbits(c->high));
  return 0;
}

/* No more symbols, so just need to output enough bits to indicate a position
   in the current range */
int range_conclude(range_coder *c)
{
  int bits;
  unsigned int v;
  unsigned int mean=((c->high-c->low)/2)+c->low;

  range_check(c,__LINE__);

  /* wipe out hopefully irrelevant bits from low part of range */
  v=0;
  int mask=0xffffffff;
  bits=0;
  while((v<c->low)||((v+mask)>c->high))
    {
      bits++;
      if (bits>=32) {
	fprintf(stderr,"Could not conclude coder:\n");
	fprintf(stderr,"  low=0x%08x, high=0x%08x\n",c->low,c->high);
      }
      v=(mean>>(32-bits))<<(32-bits);
      mask=0xffffffff>>bits;
    }
  /* Actually, apparently 2 bits is always the correct answer, because normalisation
     means that we always have 2 uncommitted bits in play, excepting for underflow
     bits, which we handle separately. */
  // if (bits<2) bits=2;

  v=(mean>>(32-bits))<<(32-bits);
  v|=0xffffffff>>bits;
  
  if (0) {
    c->value=v;
    printf("%d bits to conclude 0x%08x (low=%08x, mean=%08x, high=%08x\n",
	   bits,v,c->low,mean,c->high);
    range_status(c,0);
    c->value=0;
  }

  int i,msb=(v>>31)&1;

  /* output msb and any deferred underflow bits. */
  if (0) printf("conclude emit: %d\n",msb);
  if (range_emitbit(c,msb)) return -1;
  if (c->underflow>0) if (0) printf("  plus %d underflow bits.\n",c->underflow);
  while(c->underflow-->0) if (range_emitbit(c,msb^1)) return -1;

  /* now push bits until we know we have enough to unambiguously place the value
     within the final probability range. */
  for(i=1;i<bits;i++) {
    int b=(v>>(31-i))&1;
    if (0) printf("  %d\n",b);
    if (range_emitbit(c,b)) return -1;
  }
  //  printf(" (of %s)\n",asbits(mean));
  return 0;
}

int range_coder_reset(struct range_coder *c)
{
  c->low=0;
  c->high=0xffffffff;
  c->entropy=0;
  c->bits_used=0;
  c->underflow=0;
  c->errors=0;
  return 0;
}

struct range_coder *range_new_coder(int bytes)
{
  struct range_coder *c=calloc(sizeof(struct range_coder),1);
  c->bit_stream=malloc(bytes);
  c->bit_stream_length=bytes*8;
  range_coder_reset(c);
  return c;
}



/* Assumes probabilities are cumulative */
int range_encode_symbol(range_coder *c,unsigned int frequencies[],int alphabet_size,int symbol)
{
  if (c->errors) return -1;
  range_check(c,__LINE__);

  unsigned int p_low=0;
  if (symbol>0) p_low=frequencies[symbol-1];
  unsigned int p_high=MAXVALUEPLUS1;
  if (symbol<(alphabet_size-1)) p_high=frequencies[symbol];
  // range_status(c,0);
  // printf("symbol=%d, p_low=%u, p_high=%u\n",symbol,p_low,p_high);
  return range_encode(c,p_low,p_high);
}

int range_check(range_coder *c,int line)
{
  if (c->low>=c->high) 
    {
      fprintf(stderr,"c->low >= c->high at line %d\n",line);
      exit(-1);
    }
  if (!c->decodingP) return 0;

  if (c->value>c->high||c->value<c->low) {
    fprintf(stderr,"c->value out of bounds %d\n",line);
    fprintf(stderr,"  low=0x%08x, value=0x%08x, high=0x%08x\n",
	    c->low,c->value,c->high);
    range_status(c,1);
    exit(-1);
  }
  return 0;
}

int range_decode_equiprobable(range_coder *c,int alphabet_size)
{
  unsigned int s;
  unsigned long long space=range_space(c);
  unsigned int v=c->value-c->low;
  unsigned int step=space/alphabet_size;
  int allow_refine=1;
  s=v/step;
 refines:
  if (s==alphabet_size) s=alphabet_size-1;
  unsigned int p_low=((s+0LL)<<SIGNIFICANTBITS)/alphabet_size;
  unsigned int p_high=((s+1LL)<<SIGNIFICANTBITS)/alphabet_size;
  if (s==alphabet_size) p_high=MAXVALUEPLUS1;  

  /* Check that binning is correct (some rounding errors
     and edge cases can trip it up) */
  {
    unsigned int new_low,new_high;
    range_calc_new_range(c,p_low,p_high,&new_low,&new_high);
    if (new_low>c->value||new_high<c->value) {
      // printf("original calculation: s=%d (alphabet_size=%d)\n",
      //     s,alphabet_size);
      while (new_low>c->value||new_high<c->value) {
	if (new_low>c->value) s--;
	if (new_high<c->value) s++;

	p_low=((s+0LL)<<SIGNIFICANTBITS)/alphabet_size;
	p_high=((s+1LL)<<SIGNIFICANTBITS)/alphabet_size;
	range_calc_new_range(c,p_low,p_high,&new_low,&new_high);
	if (new_low<=c->value&&new_high>=c->value)
	  {
	    //	    printf("final calculation: s=%d\n",s);
	    break;
	  }
      }
      if (!allow_refine) {
	printf("%s(): Could not determine symbol\n",__FUNCTION__);
	exit(-1);
      }
      allow_refine=0;
      goto refines;
    }
  }

  if (0) {
    printf("%s(): space=0x%08llx, low=0x%08x, value=0x%08x, high=0x%08x, v=0x%08x, step=0x%08x, s=%d\n",
	   __FUNCTION__,space,c->low,c->value,c->high,v,step,s);
    printf("  p_low=0x%x, p_high=0x%x\n",p_low,p_high);
  }
  
  range_decode_common(c,p_low,p_high,s);
  if (0) printf("  low=0x%08x, value=0x%08x, high=0x%08x\n",c->low,c->value,c->high);
  return s;
}

int range_decode_symbol(range_coder *c,unsigned int frequencies[],int alphabet_size)
{
  c->decodingP=1;
  range_check(c,__LINE__);
  c->decodingP=0;
  int s;
  unsigned long long space=range_space(c);
  //  unsigned long long v=(((unsigned long long)(c->value-c->low))<<24LL)/space;
  
  if (c->debug) printf(" decode: value=0x%08x; ",c->value);
  // range_status(c);
  
  for(s=0;s<(alphabet_size-1);s++) {
    unsigned int boundary=c->low+((frequencies[s]*space)>>(32LL-SHIFTUPBITS));
    if (c->value<boundary) {
      if (c->debug) {
	printf("value(0x%x) < frequencies[%d](boundary = 0x%x)\n",
	       c->value,s,boundary);
	if (s>0) {
	  boundary=c->low+((frequencies[s-1]*space)>>(32LL-SHIFTUPBITS));
	  printf("  previous boundary @ 0x%08x\n",boundary);
	}
      }
      break;
    } else {
      if (0&&c->debug)
	printf("value(0x%x) >= frequencies[%d](boundary = 0x%x)\n",
	       c->value,s,boundary);
    }
  }
  
  unsigned int p_low=0;
  if (s>0) p_low=frequencies[s-1];
  unsigned int p_high=MAXVALUEPLUS1;
  if (s<alphabet_size-1) p_high=frequencies[s];

  if (c->debug) printf("s=%d, value=0x%08x, p_low=0x%08x, p_high=0x%08x\n",
		       s,c->value,p_low,p_high);
  // range_status(c);

  // printf("in decode_symbol() about to call decode_common()\n");
  // range_status(c,1);
    return range_decode_common(c,p_low,p_high,s);
}

int range_calc_new_range(range_coder *c,
			 unsigned int p_low, unsigned int p_high,
			 unsigned int *new_low,unsigned int *new_high)
{
  unsigned long long space=range_space(c);

  if (space<MAXVALUEPLUS1) {
    c->errors++;
    if (c->debug) printf("%s : ERROR: space(0x%08llx)<0x%08x\n",c->debug,space,MAXVALUEPLUS1);
    return -1;
  }

  *new_low=c->low+((p_low*space)>>(32LL-SHIFTUPBITS));
  *new_high=c->low+(((p_high)*space)>>(32LL-SHIFTUPBITS))-1;
  if (p_high>=MAXVALUEPLUS1) *new_high=c->high;

  return 0;
}

int range_decode_common(range_coder *c,unsigned int p_low,unsigned int p_high,int s)
{
  unsigned int new_low,new_high;

  range_calc_new_range(c,p_low,p_high,&new_low,&new_high);

  if (new_high>0xffffffff) {
    printf("new_high=0x%08x\n",new_high);
    new_high=0xffffffff;
  }

  if (0) {
    printf("rdc: low=0x%08x, value=0x%08x, high=0x%08x\n",c->low,c->value,c->high);
    printf("rdc: p_low=0x%08x, p_high=0x%08x, space=%08llx, s=%d\n",
	   p_low,p_high,range_space(c),s);  
  }

  c->decodingP=1;
  range_check(c,__LINE__);

  /* work out how many bits are still significant */
  c->low=new_low;
  c->high=new_high;
  range_check(c,__LINE__);
  
  if (c->debug) printf("%s: after decode: low=0x%08x, high=0x%08x\n",
		       c->debug,c->low,c->high);

  // printf("after decode before renormalise:\n");
  // range_status(c,1);

  range_emit_stable_bits(c);
  c->decodingP=0;
  range_check(c,__LINE__);

  if (c->debug) printf("%s: after rescale: low=0x%08x, high=0x%08x\n",
		       c->debug,c->low,c->high);

  return s;
}

int range_decode_prefetch(range_coder *c)
{
  c->low=0;
  c->high=0xffffffff;
  c->value=0;
  int i;
  for(i=0;i<32;i++)
    c->value=(c->value<<1)|range_decode_getnextbit(c);
  return 0;
}

int cmp_uint(const void *a,const void *b)
{
  unsigned int *aa=(unsigned int *)a;
  unsigned int *bb=(unsigned int *)b;

  if (*aa<*bb) return -1;
  if (*aa>*bb) return 1;
  return 0;
}

range_coder *range_coder_dup(range_coder *in)
{
  range_coder *out=calloc(sizeof(range_coder),1);
  if (!out) {
    fprintf(stderr,"allocation of range_coder in range_coder_dup() failed.\n");
    return NULL;
  }
  bcopy(in,out,sizeof(range_coder));
  out->bit_stream=malloc(bits2bytes(out->bit_stream_length));
  if (!out->bit_stream) {
    fprintf(stderr,"range_coder_dup() failed\n");
    free(out);
    return NULL;
  }
  if (out->bits_used>out->bit_stream_length) {
    fprintf(stderr,"bits_used>bit_stream_length in range_coder_dup()\n");
    fprintf(stderr,"  bits_used=%d, bit_stream_length=%d\n",
	    out->bits_used,out->bit_stream_length);
    exit(-1);
  }
  bcopy(in->bit_stream,out->bit_stream,bits2bytes(out->bits_used));
  return out;
}

int range_coder_free(range_coder *c)
{
  if (!c->bit_stream) {
    fprintf(stderr,"range_coder_free() asked to free apparently already freed context.\n");
    exit(-1);
  }
  free(c->bit_stream); c->bit_stream=NULL;
  bzero(c,sizeof(range_coder));
  free(c);
  return 0;
}

#ifdef STANDALONE
int main() {
  struct range_coder *c=range_new_coder(8192);

  //  test_rescale(c);
  //  test_rescale2(c);
  test_verify(c);

  return 0;
}

int test_rescale(range_coder *c)
{
  int i;
  srandom(0);

  c->debug=NULL;

  /* Test underflow rescaling */
  printf("Testing range coder rescaling functions.\n");
  for(i=0;i<10000000;i++)
    {
      range_coder_reset(c);
      /* Generate pair of values with non-matching MSBs. 
	 There is a 50% of one or more underflow bits */
      c->low=random()&0x7fffffff;
      c->high=random()|0x80000000;
      unsigned int low_before=c->low;
      unsigned int high_before=c->high;
      range_check(c,__LINE__);
      range_emit_stable_bits(c);
      unsigned int low_flattened=range_unrescale_value(c->low,c->underflow);
      unsigned int high_flattened=range_unrescale_value(c->high,c->underflow);
      unsigned int low_diff=low_before^low_flattened;
      unsigned int high_diff=high_before^high_flattened;
      if (low_diff||high_diff) {
	printf(">>> Range-coder rescaling test #%d failed:\n",i);
	printf("low: before=0x%08x, after=0x%08x, reflattened=0x%08x, diff=0x%08x  underflows=%d\n",
	       low_before,c->low,low_flattened,low_diff,c->underflow);
	printf("     before as bits=%s\n",asbits(low_before));
	printf("      after as bits=%s\n",asbits(c->low));
	printf("reflattened as bits=%s\n",asbits(low_flattened));
	printf("high: before=0x%08x, after=0x%08x, reflattended=0x%08x, diff=0x%08x\n",
	       high_before,c->high,high_flattened,high_diff);
	printf("     before as bits=%s\n",asbits(high_before));
	printf("      after as bits=%s\n",asbits(c->high));
	printf("reflattened as bits=%s\n",asbits(high_flattened));
	return -1;
      }
    }
  printf("  -- passed\n");
  return 0;
}

int test_rescale2(range_coder *c)
{
  int i,test;
  unsigned int frequencies[1024];
  int sequence[1024];
  int alphabet_size;
  int length;

  printf("Testing rescaling in actual use (50 random symbols from 3-symbol alphabet with ~49.5%%:1.5%%:49%% split)\n");

  for(test=0;test<1024;test++)
  {
    printf("   Test #%d ...\n",test);
    /* Make a nice sequence that sits close to 0.5 so that we build up some underflow */
    frequencies[0]=0x7ffff0;
    frequencies[1]=0x810000;
    length=50;
    alphabet_size=3;

    range_coder *c2=range_new_coder(8192);
    c2->errors=1;
    
    c->debug=NULL;
    c2->debug=NULL;

    /* Keep going until we find a sequence that doesn't have too many underflows
       in a row that we cannot encode it successfully. */
    int tries=100;
    while(c2->errors&&(tries--)) {
      range_coder_reset(c2);
      c2->norescale=1;
      for(i=0;i<50;i++) sequence[i]=random()%3;

      /* Now encode with rescaling enabled */
      range_coder_reset(c);
      for(i=0;i<length;i++) range_encode_symbol(c,frequencies,alphabet_size,sequence[i]);
      range_conclude(c);
      
      /* Then repeat without rescaling */

      /* repeat, but this time don't use underflow rescaling.
	 the output should be the same (or at least very nearly so). */
      for(i=0;i<length;i++) range_encode_symbol(c2,frequencies,alphabet_size,sequence[i]);
      range_conclude(c2);
      printf("%d errors\n",c2->errors);
    }
    if (tries<1) {
      printf("Repeatedly failed to generate a valid test.\n");
      continue;
    }

    char c_bits[8193];
    char c2_bits[8193];

    sprintf(c_bits,"%s",range_coder_lastbits(c,8192));
    sprintf(c2_bits,"%s",range_coder_lastbits(c2,8192));

    /* Find minimum number of symbols to encode to produce
       the error. */
    if (strcmp(c_bits,c2_bits)) {
      for(length=1;length<50;length++) 
	{
	  range_coder_reset(c2);
	  c2->norescale=1;

	  /* Now encode with rescaling enabled */
	  range_coder_reset(c);
	  for(i=0;i<length;i++) range_encode_symbol(c,frequencies,alphabet_size,sequence[i]);
	  range_conclude(c);
	  /* Then repeat without rescaling */
	  
	  /* repeat, but this time don't use underflow rescaling.
	     the output should be the same (or at least very nearly so). */
	  for(i=0;i<length;i++) range_encode_symbol(c2,frequencies,alphabet_size,sequence[i]);
	  range_conclude(c2);

	  sprintf(c_bits,"%s",range_coder_lastbits(c,8192));
	  sprintf(c2_bits,"%s",range_coder_lastbits(c2,8192));
	  if(strcmp(c_bits,c2_bits)) break;
	}   

      printf("Test #%d failed -- bitstreams generated with and without rescaling differ (symbol count = %d).\n",test,length);
      printf("   With underflow rescaling: ");
      if (c->errors) printf("<too many underflows, preventing encoding>\n");
      else printf(" %s\n",c_bits);
      printf("Without underflow rescaling: ");
      if (c2->errors) printf("<too many underflows, preventing encoding>\n");
      else printf(" %s\n",c2_bits);
      printf("                Differences: ");
      int i;
      int len=strlen(c2_bits);
      if (len>strlen(c_bits)) len=strlen(c_bits);
      for(i=0;i<len;i++)
	{
	  if (c_bits[i]!=c2_bits[i]) printf("X"); else printf(" ");
	}
      printf("\n");

      /* Display information about the state of encoding with and without rescaling to help figure out what is going on */
      printf("Progressive coder states with and without rescaling:\n");
      range_coder_reset(c2);
      c2->norescale=1;
      range_coder_reset(c);
      printf("##  : With rescaling                        : flattened values                : without rescaling\n");
      i=0;
      printf("%02db : low=0x%08x, high=0x%08x, uf=%d : low=0x%08x, high=0x%08x : low=0x%08x, high=0x%08x\n",
	     i,
	     c->low,c->high,c->underflow,
	     range_unrescale_value(c->low,c->underflow),
	     range_unrescale_value(c->high,c->underflow),
	     c2->low,c2->high);
      c->debug="with rescale"; c2->debug="  no rescale";
      for(i=0;i<length;i++)
	{
	  range_encode_symbol(c,frequencies,alphabet_size,sequence[i]);
	  range_encode_symbol(c2,frequencies,alphabet_size,sequence[i]);
	  printf("%02da : low=0x%08x, high=0x%08x, uf=%d : low=0x%08x, high=0x%08x : low=0x%08x, high=0x%08x %s\n",
		 i,
		 c->low,c->high,c->underflow,
		 range_unrescale_value(c->low,c->underflow),
		 range_unrescale_value(c->high,c->underflow),
		 c2->low,c2->high,
		 (range_unrescale_value(c->low,c->underflow)!=c2->low)||(range_unrescale_value(c->high,c->underflow)!=c2->high)?"!=":"  "
		 );
	}

      c->debug=NULL; c2->debug=NULL;
      return -1;
    }

    range_coder_free(c2);
    range_coder_reset(c);
  }
  printf("   -- passed.\n");
  return 0;
}

int test_verify(range_coder *c)
{
  unsigned int frequencies[1024];
  int sequence[1024];
  int alphabet_size;
  int length;

  int test,i;
  int testCount=102400;

  fflush(stderr);
  fflush(stdout);
  printf("Testing encoding/decoding: %d sequences with seq(0,1023,1) symbol alphabets.\n",testCount);

  srandom(0);
  for(test=0;test<testCount;test++)
    {
      if ((test%1024)==1023) fprintf(stderr,"  running test %d\n",test);
      /* Pick a random alphabet size */
      // alphabet_size=1+random()%1023;
      alphabet_size=1+test%1024;
 
      /* Generate incremental probabilities.
         Start out with randomly selected probabilities, then sort them.
	 It may make sense later on to use some non-uniform distributions
	 as well.

	 We only need n-1 probabilities for alphabet of size n, because the
	 probabilities are fences between the symbols, and p=0 and p=1 are implied
	 at each end.
      */
    newalphabet:
      for(i=0;i<alphabet_size-1;i++)
	frequencies[i]=random()&MAXVALUE;
      frequencies[alphabet_size-1]=0;

      qsort(frequencies,alphabet_size-1,sizeof(unsigned int),cmp_uint);
      for(i=0;i<alphabet_size-1;i++)
	if (frequencies[i]==frequencies[i+1]) {
	  goto newalphabet;
	}

      /* now generate random string to compress */
      length=1+random()%1023;
      for(i=0;i<length;i++) sequence[i]=random()%alphabet_size;

      int norescale=0;

      if (0)
	printf("Test #%d : %d symbols, with %d symbol alphabet, norescale=%d\n",
	       test,length,alphabet_size,norescale);
      
      /* Quick test. If it works, no need to go into more thorough examination. */
      range_coder_reset(c);
      c->norescale=norescale;
      for(i=0;i<length;i++) {
	if (range_encode_symbol(c,frequencies,alphabet_size,sequence[i]))
	  {
	    fprintf(stderr,"Error encoding symbol #%d of %d (out of space?)\n",
		    i,length);
	    return -1;
	  }
      }
      range_conclude(c);

      /* Now convert encoder state into a decoder state */
      int error=0;
      range_coder *vc=range_coder_dup(c);
      vc->bit_stream_length=vc->bits_used;
      vc->bits_used=0;
      range_decode_prefetch(vc);
      for(i=0;i<length;i++) {
	// printf("decode symbol #%d\n",i);
	// range_status(vc,1);
	if (range_decode_symbol(vc,frequencies,alphabet_size)!=sequence[i])
	  { error++; break; }
      }
      
      /* go to next test if this one passes. */
      if (!error) {
	if(0)
	  printf("Test #%d passed: encoded and verified %d symbols in %d bits (%f bits of entropy)\n",
		 test,length,c->bits_used,c->entropy);
	continue;
      }
      
      int k;
      fprintf(stderr,"Test #%d failed: verify error of symbol %d\n",test,i);
      printf("symbol probability steps: ");
      for(k=0;k<alphabet_size-1;k++) printf(" %f[0x%08x]",frequencies[k]*1.0/MAXVALUEPLUS1,frequencies[k]);
      printf("\n");
      printf("symbol list: ");
      for(k=0;k<=length;k++) printf(" %d#%d",sequence[k],k);
      printf(" ...\n");
      	  	  
      /* Encode the random symbols */
      range_coder_reset(c);
      c->norescale=norescale;
      c->debug="encode";

      vc->bits_used=0;
      vc->low=0; vc->high=0xffffffff;
      range_decode_prefetch(vc);
      vc->debug="decode";

      for(k=0;k<=i;k++) {
	range_coder *dup=range_coder_dup(c);
	dup->debug="encode";
	printf("Encoding symbol #%d = %d\n",k,sequence[k]);
	if (range_encode_symbol(c,frequencies,alphabet_size,sequence[k]))
	  {
	    fprintf(stderr,"Error encoding symbol #%d of %d (out of space?)\n",
		    i,length);
	    return -1;
	  }

	/* Display relevent decode line with encoder state */
	if (range_decode_symbol(vc,frequencies,alphabet_size)
	    !=sequence[k])
	  break;

	printf("\n");

	if (c->low!=vc->low||c->high!=vc->high) {
	  printf("encoder and decoder have diverged.\n");
	  break;
	}

      }
      range_conclude(c);
      range_coder_free(vc);
      printf("bit sequence: %s\n",range_coder_lastbits(c,8192));

      return -1;
    }
  printf("   -- passed.\n");
  return 0;
}
#endif

