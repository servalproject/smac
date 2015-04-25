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
#include <assert.h>
#include "arithmetic.h"

#define MAXVALUE 0xffffff
#define MAXVALUEPLUS1 (MAXVALUE+1)
#define MSBVALUE 0x800000
#define MSBVALUEMINUS1 (MSBVALUE-1)
#define HALFMSBVALUE (MSBVALUE>>1)
#define HALFMSBVALUEMINUS1 (HALFMSBVALUE-1)
#define SIGNIFICANTBITS 24
#define SHIFTUPBITS (32LL-SIGNIFICANTBITS)

int range_check(range_coder *c,int line);
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

  if (c->debug) fprintf(stderr,"Calculating new_low and new_high from p_low=0x%x, p_high=0x%x\n",
			p_low,p_high);
  if (range_calc_new_range(c,p_low,p_high,&new_low,&new_high))
    {
      fprintf(stderr,"range_calc_new_range() failed.\n");
      exit(-1);
    }
  
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
  double p=((double)p_range)/(double)0x100000000LL;
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

int range_equiprobable_range(range_coder *c,int alphabet_size,int symbol,unsigned int *p_low,unsigned int *p_high)
{
  *p_low=(((unsigned long long)symbol)*0xffffff)/(unsigned long long)alphabet_size;
  *p_high=(((1LL+(unsigned long long)symbol)*0xffffff)/(unsigned long long)alphabet_size);
  if (symbol==alphabet_size-1) *p_high=0xffffff;
  return 0;
}

int range_encode_equiprobable(range_coder *c,int alphabet_size,int symbol)
{
  if (alphabet_size>=0x400000) {
    /* For bigger alphabet sizes, split it */
    range_encode_equiprobable(c,1+(alphabet_size/0x10000),symbol/0x10000);
    range_encode_equiprobable(c,0x10000,symbol&0xffff);
    return 0;
  }

  if (alphabet_size>=0x400000) {
    fprintf(stderr,"%s() passed alphabet_size>0x400000\n",__FUNCTION__);
    c->errors++;
    exit(-1);
  }
  if (alphabet_size<1) return 0;

  unsigned int p_low,p_high;
  range_equiprobable_range(c,alphabet_size,symbol,&p_low,&p_high);
  if (c->debug)
    fprintf(stderr,"Encoding %d/%d: p_low=0x%x, p_high=0x%x\n",
	    symbol,alphabet_size,p_low,p_high);

  return range_encode(c,p_low,p_high);
}

int range_decode_equiprobable(range_coder *c,int alphabet_size)
{  
  if (alphabet_size>=0x400000) {
    unsigned int high=range_decode_equiprobable(c,1+(alphabet_size/0x10000));
    unsigned int low=range_decode_equiprobable(c,0x10000);
    return low|(high<<16);
  }

  if (alphabet_size<1) return 0;
  unsigned long long space=range_space(c);
  unsigned long long v=c->value-c->low;
  // unsigned long long p=0xffffff*v/space;
  unsigned int s=v*alphabet_size/space;
  
  if (c->debug)
    fprintf(stderr,"decoding: alphabet size = %d, estimating s=%d (0x%x)\n",
	    alphabet_size,s,s);

  int symbol;

  for(symbol=s?s-1:0;symbol<s+2&&symbol<alphabet_size;symbol++)
    {
      unsigned int p_low,p_high;
      range_equiprobable_range(c,alphabet_size,symbol,&p_low,&p_high);
      if (!range_decode_common(c,p_low,p_high,symbol)) {
	if (c->debug) 
	  fprintf(stderr,"Decoding %d/%d p_low=0x%x, p_high=0x%x\n",
		  symbol,alphabet_size,p_low,p_high);
	return symbol;     
      }
    }

  fprintf(stderr,"Internal error in range_decode_equiprobable().\n");
  
  s=v*alphabet_size/space;
  fprintf(stderr,"Estimated s=%d (0x%x)\n",s,s);
  for(symbol=s?s-1:0;symbol<s+2&&symbol<alphabet_size;symbol++)
    {
      unsigned int p_low,p_high;
      c->debug="here";
      range_equiprobable_range(c,alphabet_size,symbol,&p_low,&p_high);
      fprintf(stderr,"tried symbol=%d (0x%x) : p_low=0x%x, p_high=0x%x\n",
	      symbol,symbol,p_low,p_high);
      if (range_decode_common(c,p_low,p_high,symbol)) {
	fprintf(stderr,"  and range_decode_common() failed.\n");
      }
    } 

  exit(-1);
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
	 (((unsigned long long)value-(unsigned long long)c->low)<<32LL)/
	 (space?space:1));
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

  int i,msb=(mean>>31)&1;

  /* output msb and any deferred underflow bits. */
  if (c->debug) printf("conclude emit: %d\n",msb);
  if (range_emitbit(c,msb)) return -1;
  if (c->underflow>0) if (c->debug) printf("  plus %d underflow bits.\n",c->underflow);
  while(c->underflow-->0) if (range_emitbit(c,msb^1)) return -1;

  /* shift out msb */
  c->low=(c->low<<1)&0x7fffffff;
  c->high=(c->high<<1)|0x80000001;
  if (c->debug) {
    fprintf(stderr,"after shifting out msb and underflow bits: low=0x%x, high=0x%x\n",
	    c->low,c->high);
    range_status(c,0);
  }

  /* work out new mean */
  mean=((c->high-c->low)/2)+c->low;

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
	exit(-1);
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
  
  if (c->debug) {
    c->value=v;
    printf("%d bits to conclude 0x%08x (low=%08x, mean=%08x, high=%08x\n",
	   bits,v,c->low,mean,c->high);
    range_status(c,0);
    c->value=0;
  }

  /* now push bits until we know we have enough to unambiguously place the value
     within the final probability range. */
  for(i=0;i<bits;i++) {
    int b=(v>>(31-i))&1;
    if (c->debug) printf("  ordinary bit: %d\n",b);
    if (range_emitbit(c,b)) return -1;
  }
  //  printf(" (of %s)\n",asbits(mean));
  // range_status(c,0);
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

  assert(symbol>=0);
  assert(symbol<alphabet_size);

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
      if (!line) return -1;
      fprintf(stderr,"c->low >= c->high at line %d\n",line);
      exit(-1);
    }
  if (!c->decodingP) return 0;

  if (c->value>c->high||c->value<c->low) {
    if (!line) return -1;
    fprintf(stderr,"c->value out of bounds %d\n",line);
    fprintf(stderr,"  low=0x%08x, value=0x%08x, high=0x%08x\n",
	    c->low,c->value,c->high);
    range_status(c,1);
    exit(-1);
  }
  return 0;
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
  if (range_decode_common(c,p_low,p_high,s)) {
    fprintf(stderr,"range_decode_common() failed for some reason.\n");
    exit(-1);
  }
  return s;
}

int range_calc_new_range(range_coder *c,
			 unsigned int p_low, unsigned int p_high,
			 unsigned int *new_low,unsigned int *new_high)
{
  unsigned long long space=range_space(c);
  if (c->debug) fprintf(stderr,"calculating new range using space=0x%llx\n",space);

  if (space<MAXVALUEPLUS1) {
    c->errors++;
    if (c->debug) printf("%s : ERROR: space(0x%08llx)<0x%08x\n",c->debug,space,MAXVALUEPLUS1);
    return -1;
  }

  if (c->debug) {
    fprintf(stderr,"%s(): space=0x%llx, c->low=0x%x, c->high=0x%x, p_low=0x%x, p_high=0x%x\n",
	    __FUNCTION__,space,c->low,c->high,p_low,p_high);
  }
  if(c->debug) fprintf(stderr,"(0x%x * 0x%llx)>>24 = 0x%llx\n",p_low,space,(p_low*space)>>24LL);
  *new_low=c->low+((p_low*space)>>(32LL-SHIFTUPBITS));
  *new_high=c->low+(((p_high)*space)>>(32LL-SHIFTUPBITS))-1;
  if (p_high>=MAXVALUEPLUS1) *new_high=c->high;

  if (c->decodingP)
    if (*new_low>c->value||*new_high<c->value) {
      if (c->debug) {
	fprintf(stderr,"%s(): new range would be invalid: space=0x%llx, c->low=0x%x, c->high=0x%x, p_low=0x%x, p_high=0x%x\n",
		__FUNCTION__,space,c->low,c->high,p_low,p_high);
	fprintf(stderr,"  new_low=0x%x, new_high=0x%x, c->value=0x%x\n",
		*new_low,*new_high,c->value);
      }
      return -1;
    }

  return 0;
}

int range_decode_common(range_coder *c,unsigned int p_low,unsigned int p_high,int s)
{
  unsigned int new_low,new_high;

  // If there are no more bits, and low and high are the same, then we have no more
  // data from which to decode
  if ((c->bits_used>=c->bit_stream_length)
      &&(c->low>=c->high))
    {
      return -1;
    }
  
  if (range_check(c,0 /* don't abort if things go wrong */)) {
    if (c->debug) fprintf(stderr,"range check failed at %s:%d\n",__FILE__,__LINE__);
    return -1;
  }

  if (range_calc_new_range(c,p_low,p_high,&new_low,&new_high)) {
    if (c->debug) fprintf(stderr,"range calc new range failed at %s:%d\n",__FILE__,__LINE__);
    return -1;
  }

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
  if (range_check(c,0 /* don't abort if things go wrong */)) {
    if (c->debug) fprintf(stderr,"range check failed at %s:%d\n",__FILE__,__LINE__);
    return -1;
  }

  if (new_low>c->value||new_high<c->value) {
    if (c->debug) {
      fprintf(stderr,"c->value would be out of bounds at %s:%d\n",__FILE__,__LINE__);
      fprintf(stderr,"  new_low=0x%08x, c->value=0x%08x, new_high=0x%08x\n",new_low,c->value,new_high);
      fprintf(stderr,"  low=0x%08x, value=0x%08x, high=0x%08x\n",c->low,c->value,c->high);
      fprintf(stderr,"  p_low=0x%08x, p_high=0x%08x, space=%08llx, s=%d\n",
	      p_low,p_high,range_space(c),s);
      fprintf(stderr,"  c->low=0x%x, c->high=0x%x\n",c->low,c->high);
    
    }
    return -1;
  }

  /* work out how many bits are still significant */
  c->low=new_low;
  c->high=new_high;
  if (range_check(c,0 /* don't abort if things go wrong */)) {
    if (c->debug) fprintf(stderr,"range check failed at %s:%d\n",__FILE__,__LINE__);
    return -1;
  }
  
  if (c->debug) printf("%s: after decode: low=0x%08x, high=0x%08x\n",
		       c->debug,c->low,c->high);

  // printf("after decode before renormalise:\n");
  // range_status(c,1);

  range_emit_stable_bits(c);
  c->decodingP=0;
  range_check(c,__LINE__);

  if (c->debug) printf("%s: after rescale: low=0x%08x, high=0x%08x\n",
		       c->debug,c->low,c->high);

  return 0;
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

#ifdef TESTMODE
int test_foo1(range_coder *c)
{
  fprintf(stderr,"Testing a use case that failed some time.\n");
  range_coder_reset(c);

  unsigned int messagelengths[1024]={
0x22,
0x1ce4,0x60c4,0xce7b,0x15019,0x1f19a,0x28a40,0x34011,0x41cff,
0x4fa79,0x5e414,0x70b6d,0x85287,0x9a62b,0xb2e03,0xcd927,0xeb673,
0x109d6d,0x12a7d6,0x14fd35,0x1a4fba,0x1cbc44,0x1f4457,0x21f1e4,0x24b0c1,
0x2776d7,0x2a6796,0x2d7950,0x30a32a,0x338b53,0x3677fb,0x397889,0x3c6ebc,
0x3f539f,0x425a72,0x45337a,0x4812a4,0x4acea4,0x4d9bd0,0x5053dc,0x53336f,
0x55eee3,0x587ff4,0x5b280e,0x5db4a0,0x6035bf,0x62c4a2,0x6554bf,0x67d530,
0x6a4198,0x6c97a5,0x6f0752,0x7152e1,0x73bcd5,0x75fb71,0x78432f,0x7a8762,
0x7cb974,0x7ee72a,0x811f5f,0x833b3a,0x854375,0x875c50,0x895824,0x8b6bae,
0x8d5b84,0x8f4aab,0x914151,0x932a9b,0x94f4b0,0x96d2f0,0x98aa60,0x9a6da4,
0x9c1cbc,0x9dcf5f,0x9f893c,0xa13948,0xa2e033,0xa471da,0xa61146,0xa7ba5f,
0xa94ee3,0xaac7e0,0xac43ff,0xadd39b,0xaf49de,0xb0d840,0xb244d5,0xb3a8b1,
0xb5105f,0xb665c9,0xb7ca53,0xb92830,0xba7fa6,0xbbc358,0xbd0150,0xbe30d7,
0xbf6e66,0xc09fd5,0xc21b68,0xc34d40,0xc4d397,0xc6789a,0xc7bec0,0xc8fc0a,
0xca2bb3,0xcb37a7,0xcc5481,0xcd65e8,0xce71dc,0xcf73dc,0xd071e9,0xd17681,
0xd27701,0xd37602,0xd478b1,0xd57284,0xd66613,0xd76143,0xd86067,0xd95de8,
0xda66dc,0xdb52eb,0xdc53d4,0xdd4f4a,0xde583e,0xdf5aed,0xe06379,0xe172d5,
0xe2a6fd,0xe3c9d6,0xe50a43,0xe662ac,0xe7b468,0xe915ae,0xeaa732,0xee3816,
0xf01956,0xf1ff7e,0xf454dd,0xff10cd,0xff4a2e,0xff6593,0xff71d7,0xff79bf,
0xff7e61,0xff82be,0xff84a6,0xff8649,0xff86f7,0xff880e,0xff889a,0xff88bd,
0xff88e0,0xff8948,0xff896b,0xff89b1,0xff89d4,0xff89f7,0xff8a1a,0xff8a3d,
0xff8a60,0xff8a82,0xff8aa5,0xff8ac8,0xff8aeb,0xff8b0e,0xff8b31,0xff8b54,
0xff8b77,0xff8b9a,0xff8bbd,0xff8bdf,0xff8c02,0xff8c25,0xff8c48,0xff8c6b,
0xff8c8e,0xff8cb1,0xff8cd4,0xff8cf7,0xff8d19,0xff8d3c,0xff8d5f,0xff8d82,
0xff8da5,0xff8dc8,0xff8deb,0xff8e0e,0xff8e31,0xff8e53,0xff8e76,0xff8e99,
0xff8ebc,0xff8edf,0xff8f02,0xff8f25,0xff8f48,0xff8f6b,0xff8f8d,0xff8fb0,
0xff8fd3,0xff8ff6,0xff9019,0xff903c,0xff905f,0xff9082,0xff90a5,0xff90c7,
0xff90ea,0xff910d,0xff9130,0xff9153,0xff9176,0xff9199,0xff91bc,0xff91df,
0xff9201,0xff9224,0xff9247,0xff926a,0xff928d,0xff92b0,0xff92d3,0xff92f6,
0xff9319,0xff933c,0xff935e,0xff9381,0xff93a4,0xff93c7,0xff93ea,0xff940d,
0xff9430,0xff9453,0xff9476,0xff9498,0xff94bb,0xff94de,0xff9501,0xff9524,
0xff9547,0xff956a,0xff958d,0xff95b0,0xff95d2,0xff95f5,0xff9618,0xff963b,
0xff965e,0xff9681,0xff96a4,0xff96c7,0xff96ea,0xff970c,0xff972f,0xff9752,
0xff9775,0xff9798,0xff97bb,0xff97de,0xff9801,0xff9824,0xff9846,0xff9869,
0xff988c,0xff98af,0xff98d2,0xff98f5,0xff9918,0xff993b,0xff995e,0xff9980,
0xff99a3,0xff99c6,0xff99e9,0xff9a0c,0xff9a2f,0xff9a52,0xff9a75,0xff9a98,
0xff9abb,0xff9add,0xff9b00,0xff9b23,0xff9b46,0xff9b69,0xff9b8c,0xff9baf,
0xff9bd2,0xff9bf5,0xff9c17,0xff9c3a,0xff9c5d,0xff9c80,0xff9ca3,0xff9cc6,
0xff9ce9,0xff9d0c,0xff9d2f,0xff9d51,0xff9d74,0xff9d97,0xff9dba,0xff9ddd,
0xff9e00,0xff9e23,0xff9e46,0xff9e69,0xff9e8b,0xff9eae,0xff9ed1,0xff9ef4,
0xff9f17,0xff9f3a,0xff9f5d,0xff9f80,0xff9fa3,0xff9fc5,0xff9fe8,0xffa00b,
0xffa02e,0xffa051,0xffa074,0xffa097,0xffa0ba,0xffa0dd,0xffa0ff,0xffa122,
0xffa145,0xffa168,0xffa18b,0xffa1ae,0xffa1d1,0xffa1f4,0xffa217,0xffa23a,
0xffa25c,0xffa27f,0xffa2a2,0xffa2c5,0xffa2e8,0xffa30b,0xffa32e,0xffa351,
0xffa374,0xffa396,0xffa3b9,0xffa3dc,0xffa3ff,0xffa422,0xffa445,0xffa468,
0xffa48b,0xffa4ae,0xffa4d0,0xffa4f3,0xffa516,0xffa539,0xffa55c,0xffa57f,
0xffa5a2,0xffa5c5,0xffa5e8,0xffa60a,0xffa62d,0xffa650,0xffa673,0xffa696,
0xffa6b9,0xffa6dc,0xffa6ff,0xffa722,0xffa744,0xffa767,0xffa78a,0xffa7ad,
0xffa7d0,0xffa7f3,0xffa816,0xffa839,0xffa85c,0xffa87f,0xffa8a1,0xffa8c4,
0xffa8e7,0xffa90a,0xffa92d,0xffa950,0xffa973,0xffa996,0xffa9b9,0xffa9db,
0xffa9fe,0xffaa21,0xffaa44,0xffaa67,0xffaa8a,0xffaaad,0xffaad0,0xffaaf3,
0xffab15,0xffab38,0xffab5b,0xffab7e,0xffaba1,0xffabc4,0xffabe7,0xffac0a,
0xffac2d,0xffac4f,0xffac72,0xffac95,0xffacb8,0xffacdb,0xffacfe,0xffad21,
0xffad44,0xffad67,0xffad89,0xffadac,0xffadcf,0xffadf2,0xffae15,0xffae38,
0xffae5b,0xffae7e,0xffaea1,0xffaec3,0xffaee6,0xffaf09,0xffaf2c,0xffaf4f,
0xffaf72,0xffaf95,0xffafb8,0xffafdb,0xffaffe,0xffb020,0xffb043,0xffb066,
0xffb089,0xffb0ac,0xffb0cf,0xffb0f2,0xffb115,0xffb138,0xffb15a,0xffb17d,
0xffb1a0,0xffb1c3,0xffb1e6,0xffb209,0xffb22c,0xffb24f,0xffb272,0xffb294,
0xffb2b7,0xffb2da,0xffb2fd,0xffb320,0xffb343,0xffb366,0xffb389,0xffb3ac,
0xffb3ce,0xffb3f1,0xffb414,0xffb437,0xffb45a,0xffb47d,0xffb4a0,0xffb4c3,
0xffb4e6,0xffb508,0xffb52b,0xffb54e,0xffb571,0xffb594,0xffb5b7,0xffb5da,
0xffb5fd,0xffb620,0xffb642,0xffb665,0xffb688,0xffb6ab,0xffb6ce,0xffb6f1,
0xffb714,0xffb737,0xffb75a,0xffb77d,0xffb79f,0xffb7c2,0xffb7e5,0xffb808,
0xffb82b,0xffb84e,0xffb871,0xffb894,0xffb8b7,0xffb8d9,0xffb8fc,0xffb91f,
0xffb942,0xffb965,0xffb988,0xffb9ab,0xffb9ce,0xffb9f1,0xffba13,0xffba36,
0xffba59,0xffba7c,0xffba9f,0xffbac2,0xffbae5,0xffbb08,0xffbb2b,0xffbb4d,
0xffbb70,0xffbb93,0xffbbb6,0xffbbd9,0xffbbfc,0xffbc1f,0xffbc42,0xffbc65,
0xffbc87,0xffbcaa,0xffbccd,0xffbcf0,0xffbd13,0xffbd36,0xffbd59,0xffbd7c,
0xffbd9f,0xffbdc1,0xffbde4,0xffbe07,0xffbe2a,0xffbe4d,0xffbe70,0xffbe93,
0xffbeb6,0xffbed9,0xffbefc,0xffbf1e,0xffbf41,0xffbf64,0xffbf87,0xffbfaa,
0xffbfcd,0xffbff0,0xffc013,0xffc036,0xffc058,0xffc07b,0xffc09e,0xffc0c1,
0xffc0e4,0xffc107,0xffc12a,0xffc14d,0xffc170,0xffc192,0xffc1b5,0xffc1d8,
0xffc1fb,0xffc21e,0xffc241,0xffc264,0xffc287,0xffc2aa,0xffc2cc,0xffc2ef,
0xffc312,0xffc335,0xffc358,0xffc37b,0xffc39e,0xffc3c1,0xffc3e4,0xffc406,
0xffc429,0xffc44c,0xffc46f,0xffc492,0xffc4b5,0xffc4d8,0xffc4fb,0xffc51e,
0xffc540,0xffc563,0xffc586,0xffc5a9,0xffc5cc,0xffc5ef,0xffc612,0xffc635,
0xffc658,0xffc67b,0xffc69d,0xffc6c0,0xffc6e3,0xffc706,0xffc729,0xffc74c,
0xffc76f,0xffc792,0xffc7b5,0xffc7d7,0xffc7fa,0xffc81d,0xffc840,0xffc863,
0xffc886,0xffc8a9,0xffc8cc,0xffc8ef,0xffc911,0xffc934,0xffc957,0xffc97a,
0xffc99d,0xffc9c0,0xffc9e3,0xffca06,0xffca29,0xffca4b,0xffca6e,0xffca91,
0xffcab4,0xffcad7,0xffcafa,0xffcb1d,0xffcb40,0xffcb63,0xffcb85,0xffcba8,
0xffcbcb,0xffcbee,0xffcc11,0xffcc34,0xffcc57,0xffcc7a,0xffcc9d,0xffccbf,
0xffcce2,0xffcd05,0xffcd28,0xffcd4b,0xffcd6e,0xffcd91,0xffcdb4,0xffcdd7,
0xffcdfa,0xffce1c,0xffce3f,0xffce62,0xffce85,0xffcea8,0xffcecb,0xffceee,
0xffcf11,0xffcf34,0xffcf56,0xffcf79,0xffcf9c,0xffcfbf,0xffcfe2,0xffd005,
0xffd028,0xffd04b,0xffd06e,0xffd090,0xffd0b3,0xffd0d6,0xffd0f9,0xffd11c,
0xffd13f,0xffd162,0xffd185,0xffd1a8,0xffd1ca,0xffd1ed,0xffd210,0xffd233,
0xffd256,0xffd279,0xffd29c,0xffd2bf,0xffd2e2,0xffd304,0xffd327,0xffd34a,
0xffd36d,0xffd390,0xffd3b3,0xffd3d6,0xffd3f9,0xffd41c,0xffd43f,0xffd461,
0xffd484,0xffd4a7,0xffd4ca,0xffd4ed,0xffd510,0xffd533,0xffd556,0xffd579,
0xffd59b,0xffd5be,0xffd5e1,0xffd604,0xffd627,0xffd64a,0xffd66d,0xffd690,
0xffd6b3,0xffd6d5,0xffd6f8,0xffd71b,0xffd73e,0xffd761,0xffd784,0xffd7a7,
0xffd7ca,0xffd7ed,0xffd80f,0xffd832,0xffd855,0xffd878,0xffd89b,0xffd8be,
0xffd8e1,0xffd904,0xffd927,0xffd949,0xffd96c,0xffd98f,0xffd9b2,0xffd9d5,
0xffd9f8,0xffda1b,0xffda3e,0xffda61,0xffda83,0xffdaa6,0xffdac9,0xffdaec,
0xffdb0f,0xffdb32,0xffdb55,0xffdb78,0xffdb9b,0xffdbbe,0xffdbe0,0xffdc03,
0xffdc26,0xffdc49,0xffdc6c,0xffdc8f,0xffdcb2,0xffdcd5,0xffdcf8,0xffdd1a,
0xffdd3d,0xffdd60,0xffdd83,0xffdda6,0xffddc9,0xffddec,0xffde0f,0xffde32,
0xffde54,0xffde77,0xffde9a,0xffdebd,0xffdee0,0xffdf03,0xffdf26,0xffdf49,
0xffdf6c,0xffdf8e,0xffdfb1,0xffdfd4,0xffdff7,0xffe01a,0xffe03d,0xffe060,
0xffe083,0xffe0a6,0xffe0c8,0xffe0eb,0xffe10e,0xffe131,0xffe154,0xffe177,
0xffe19a,0xffe1bd,0xffe1e0,0xffe202,0xffe225,0xffe248,0xffe26b,0xffe28e,
0xffe2b1,0xffe2d4,0xffe2f7,0xffe31a,0xffe33d,0xffe35f,0xffe382,0xffe3a5,
0xffe3c8,0xffe3eb,0xffe40e,0xffe431,0xffe454,0xffe477,0xffe499,0xffe4bc,
0xffe4df,0xffe502,0xffe525,0xffe548,0xffe56b,0xffe58e,0xffe5b1,0xffe5d3,
0xffe5f6,0xffe619,0xffe63c,0xffe65f,0xffe682,0xffe6a5,0xffe6c8,0xffe6eb,
0xffe70d,0xffe730,0xffe753,0xffe776,0xffe799,0xffe7bc,0xffe7df,0xffe802,
0xffe825,0xffe847,0xffe86a,0xffe88d,0xffe8b0,0xffe8d3,0xffe8f6,0xffe919,
0xffe93c,0xffe95f,0xffe981,0xffe9a4,0xffe9c7,0xffe9ea,0xffea0d,0xffea30,
0xffea53,0xffea76,0xffea99,0xffeabc,0xffeade,0xffeb01,0xffeb24,0xffeb47,
0xffeb6a,0xffeb8d,0xffebb0,0xffebd3,0xffebf6,0xffec18,0xffec3b,0xffec5e,
0xffec81,0xffeca4,0xffecc7,0xffecea,0xffed0d,0xffed30,0xffed52,0xffed75,
0xffed98,0xffedbb,0xffedde,0xffee01,0xffee24,0xffee47,0xffee6a,0xffee8c,
0xffeeaf,0xffeed2,0xffeef5,0xffef18,0xffef3b,0xffef5e,0xffef81,0xffefa4,
0xffefc6,0xffefe9,0xfff00c,0xfff02f,0xfff052,0xfff075,0xfff098,0xfff0bb,
0xfff0de,0xfff100,0xfff123,0xfff146,0xfff169,0xfff18c,0xfff1af,0xfff1d2,
0xfff1f5,0xfff218,0xfff23b,0xfff25d,0xfff280,0xfff2a3,0xfff2c6,0xfff2e9,
0xfff30c,0xfff32f,0xfff352,0xfff375,0xfff397,0xfff3ba,0xfff3dd,0xfff400,
0xfff423,0xfff446,0xfff469,0xfff48c,0xfff4af,0xfff4d1,0xfff4f4,0xfff517,
0xfff53a,0xfff55d,0xfff580,0xfff5a3,0xfff5c6,0xfff5e9,0xfff60b,0xfff62e,
0xfff651,0xfff674,0xfff697,0xfff6ba,0xfff6dd,0xfff700,0xfff723,0xfff745,
0xfff768,0xfff78b,0xfff7ae,0xfff7d1,0xfff7f4,0xfff817,0xfff83a,0xfff85d,
0xfff87f,0xfff8a2,0xfff8c5,0xfff8e8,0xfff90b,0xfff92e,0xfff951,0xfff974,
0xfff997,0xfff9ba,0xfff9dc,0xfff9ff,0xfffa22,0xfffa45,0xfffa68,0xfffa8b,
0xfffaae,0xfffad1,0xfffaf4,0xfffb16,0xfffb39,0xfffb5c,0xfffb7f,0xfffba2,
0xfffbc5,0xfffbe8,0xfffc0b,0xfffc2e,0xfffc50,0xfffc73,0xfffc96,0xfffcb9,
0xfffcdc,0xfffcff,0xfffd22,0xfffd45,0xfffd68,0xfffd8a,0xfffdad,0xfffdd0,
0xfffdf3,0xfffe16,0xfffe39,0xfffe5c,0xfffe7f,0xfffea2,0xfffec4,0xfffee7,
0xffff0a,0xffff2d,0xffff50,0xffff73,0xffff96,0xffffb9,0xffffdc};

  unsigned int probPackedASCII=0.05*0xffffff;
  fprintf(stderr,"%s:%d  c->low=0x%x, c->value=0x%08x, c->high=0x%x\n",__FUNCTION__,__LINE__,c->low,c->value,c->high);
  range_encode_equiprobable(c,2,1);
  fprintf(stderr,"%s:%d  c->low=0x%x, c->value=0x%08x, c->high=0x%x\n",__FUNCTION__,__LINE__,c->low,c->value,c->high);
  range_encode_equiprobable(c,2,0);
  fprintf(stderr,"%s:%d  c->low=0x%x, c->value=0x%08x, c->high=0x%x\n",__FUNCTION__,__LINE__,c->low,c->value,c->high);
  range_encode_symbol(c,&probPackedASCII,2,1);
  fprintf(stderr,"%s:%d  c->low=0x%x, c->value=0x%08x, c->high=0x%x\n",__FUNCTION__,__LINE__,c->low,c->value,c->high);
  range_encode_symbol(c,messagelengths,1024,140);
  fprintf(stderr,"%s:%d  c->low=0x%x, c->value=0x%08x, c->high=0x%x\n",__FUNCTION__,__LINE__,c->low,c->value,c->high);

  range_conclude(c);

  {
    int i;
    for(i=0;i<=(c->bits_used>>3);i++) fprintf(stderr,"%02x ",c->bit_stream[i]);
    fprintf(stderr,"\n");
  }

  range_coder *vc=range_coder_dup(c);
  vc->bit_stream_length=vc->bits_used;
  vc->bits_used=0;
  range_decode_prefetch(vc);

  fprintf(stderr,"%s:%d  c->low=0x%x, c->value=0x%08x, c->high=0x%x\n",__FUNCTION__,__LINE__,vc->low,vc->value,vc->high);
  int b7=range_decode_equiprobable(vc,2);
  fprintf(stderr,"%s:%d  c->low=0x%x, c->value=0x%08x, c->high=0x%x\n",__FUNCTION__,__LINE__,vc->low,vc->value,vc->high);
  int b6=range_decode_equiprobable(vc,2);
  fprintf(stderr,"%s:%d  c->low=0x%x, c->value=0x%08x, c->high=0x%x\n",__FUNCTION__,__LINE__,vc->low,vc->value,vc->high);
  int notPackedASCII=range_decode_symbol(vc,&probPackedASCII,2);
  fprintf(stderr,"%s:%d  c->low=0x%x, c->value=0x%08x, c->high=0x%x\n",__FUNCTION__,__LINE__,vc->low,vc->value,vc->high);
  int length=range_decode_symbol(vc,messagelengths,1024);
  fprintf(stderr,"%s:%d  c->low=0x%x, c->value=0x%08x, c->high=0x%x\n",__FUNCTION__,__LINE__,vc->low,vc->value,vc->high);

  if (b7!=1) {
    fprintf(stderr,"Failed to decode b7=1 (got %d)\n",b7);
    exit(-1);
  }
  if (b6!=0) {
    fprintf(stderr,"Failed to decode b6=0 (got %d)\n",b6);
    exit(-1);
  }
  if (notPackedASCII!=1) {
    fprintf(stderr,"Failed to decode notPackedASCII=1 (got %d)\n",notPackedASCII);
    exit(-1);
  }
  if (length!=140) {
    fprintf(stderr,"Failed to decode length=140 (got %d)\n",length);
    exit(-1);
  }

  range_coder_free(vc);
  fprintf(stderr,"  -- passed.\n");
  return 0;
}

int test_fineslices(range_coder *c)
{
  int i;
  
  fprintf(stderr,"Testing performance with hair-width probabilities.\n");

  range_coder_reset(c);
  c->debug=NULL;
  
  unsigned int frequencies[2]={0x7fffff,0x800000};
  
  srandom(0);
  for(i=0;i<100;i++)
    {
      int symbol=random()%3;
      range_encode_symbol(c,frequencies,3,symbol);
    }
  range_conclude(c);

  range_coder *vc=range_coder_dup(c);
  vc->bit_stream_length=vc->bits_used;
  vc->bits_used=0;
  range_decode_prefetch(vc);

  srandom(0);
  for(i=0;i<100;i++)
    {
      int symbol=random()%3;
      int decodeSymbol=range_decode_symbol(vc,frequencies,3);
      if (symbol!=decodeSymbol) {
	fprintf(stderr,"Verify error after symbol #%d: wrote %d, read %d\n",
		i,symbol,decodeSymbol);
	exit(-1);
      }
    }
  range_coder_free(vc);
  fprintf(stderr,"  -- passed.\n");

  return 0;
}

int test_equiprobable(range_coder *c)
{
  srandom(0);
  int i;
  
  fprintf(stderr,"Running tests on equiprobable encoding.\n");

  c->debug=NULL;
  for(i=0;i<1000000;i++)
    {
      int alphabet_size=random();
      if (!alphabet_size) alphabet_size=1;
      int symbol=random()%alphabet_size;
      range_coder_reset(c);
      c->debug=NULL;
      range_encode_equiprobable(c,alphabet_size,symbol);
      range_encode_equiprobable(c,alphabet_size,symbol);
      // fprintf(stderr,"c->low=0x%x, c->high=0x%x\n",c->low,c->high);
      // range_status(c,0);
      range_conclude(c);
      
      range_coder *vc=range_coder_dup(c);
      vc->bit_stream_length=vc->bits_used;
      vc->bits_used=0;
      range_decode_prefetch(vc);
      int symbol1=range_decode_equiprobable(vc,alphabet_size);
      int symbol2=range_decode_equiprobable(vc,alphabet_size);
      if (symbol!=symbol1||symbol!=symbol2) {
	fprintf(stderr,"test #%d failed: range_encode_equiprobable(alphabet_size=%d, symbol=%d(0x%x)) x2 verified as symbol={%d(0x%x),%d(0x%x)}\n",
		i,alphabet_size,symbol,symbol,symbol1,symbol1,symbol2,symbol2);

	range_coder_reset(c);
	c->debug="encode";
	range_encode_equiprobable(c,alphabet_size,symbol);
	fprintf(stderr,"\n");
	range_encode_equiprobable(c,alphabet_size,symbol);
	fprintf(stderr,"\n");
	range_conclude(c);
	c->debug=NULL;
	vc->bits_used=0;
	vc->low=0; vc->high=0xffffff;
	range_decode_prefetch(vc);
	vc->debug="decode";
	range_decode_equiprobable(vc,alphabet_size);
	fprintf(stderr,"\n");
	range_decode_equiprobable(vc,alphabet_size);
	fprintf(stderr,"\n");

	exit(-1);
      }

      range_coder_free(vc);
    }
  fprintf(stderr,"  -- passed.\n");
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
      if (low_diff ||high_diff) {
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

int main() {
  struct range_coder *c=range_new_coder(8192);

  //  test_rescale(c);
  //  test_rescale2(c);
  test_foo1(c);
  test_fineslices(c);
  test_equiprobable(c);
  test_verify(c);

  return 0;
}
#endif

