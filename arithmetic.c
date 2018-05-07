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
#include <string.h>
#include <strings.h>
#include <math.h>
#include <assert.h>
#include "arithmetic.h"
#include "log.h"

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
    LOGE("out of bits\n");
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
  range_check(c);
  /* look for actually stable bits, i.e.,msb of low and high match */
  while (!((c->low^c->high)&0x80000000))
    {
      int msb=c->low>>31;
      if (0)
	LOGI("emitting stable bit = %d @ bit %d\n",msb,c->bits_used);

      if (!c->decodingP) if (range_emitbit(c,msb)) return -1;
      if (c->underflow) {
	int u;
	if (msb) u=0; else u=1;
	while (c->underflow-->0) {	  
	  if (0)
	    LOGI("emitting underflow bit = %d @ bit %d\n",u,c->bits_used);

	  if (!c->decodingP) if (range_emitbit(c,u)) return -1;
	}
	c->underflow=0;
      }
      if (c->decodingP) {
	// LOGI("value was 0x%08x (low=0x%08x, high=0x%08x)\n",c->value,c->low,c->high);
      }
      c->low=c->low<<1;
      c->high=c->high<<1;      
      c->high|=1;
      if (c->decodingP) {
	c->value=c->value<<1;
	int nextbit=range_decode_getnextbit(c);
	c->value|=nextbit;
	// LOGI("value became 0x%08x (low=0x%08x, high=0x%08x), nextbit=%d\n",c->value,c->low,c->high,nextbit);
      }
      range_check(c);
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
	LOGI("underflow bit added @ bit %d\n",c->bits_used);

      unsigned int new_low=c->low<<1;
      new_low&=0x7fffffff;
      unsigned int new_high=c->high<<1;
      new_high|=1;
      new_high|=0x80000000;
      if (new_low>=new_high) { 
	LOGE("oops\n");
	return -1;
      }
      if (c->debug)
	LOGI("%s: rescaling: old=[0x%08x,0x%08x], new=[0x%08x,0x%08x]\n",
	       c->debug,c->low,c->high,new_low,new_high);

      if (c->decodingP) {
	unsigned int value_bits=((c->value<<1)&0x7ffffffe);
	if (c->debug)
	  LOGI("value was 0x%08x (low=0x%08x, high=0x%08x), keepbits=0x%08x\n",c->value,c->low,c->high,value_bits);
	c->value=(c->value&0x80000000)|value_bits;
	c->value|=range_decode_getnextbit(c);
      }
      c->low=new_low;
      c->high=new_high;
      if (c->decodingP&&c->debug)
	LOGI("value became 0x%08x (low=0x%08x, high=0x%08x)\n",c->value,c->low,c->high);
      range_check(c);
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
    LOGI("0x%08x+%d underflows flattens to 0x%08x\n",
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
    LOGE("range_encode() called with p_low>p_high: p_low=%u, p_high=%u\n",
	    p_low,p_high);
    return -1;
  }
  if (p_low>MAXVALUE||p_high>MAXVALUEPLUS1) {
    LOGE("range_encode() called with p_low or p_high >=0x%x: p_low=0x%x, p_high=0x%x\n",
	    MAXVALUE,p_low,p_high);
    return -1;
  }

  unsigned int new_low,new_high;

  if (c->debug) LOGE("Calculating new_low and new_high from p_low=0x%x, p_high=0x%x\n",
			p_low,p_high);
  if (range_calc_new_range(c,p_low,p_high,&new_low,&new_high))
    {
      LOGE("range_calc_new_range() failed.\n");
      return -1;
    }
  
  range_check(c);
  c->low=new_low;
  c->high=new_high;
  range_check(c);

  if (c->debug) {
    LOGI("%s: space=0x%08llx[%s], new_low=0x%08x, new_high=0x%08x\n",
	   c->debug,range_space(c),asbits(range_space(c)),new_low,new_high);
  }

  unsigned long long p_diff=p_high-p_low;
  unsigned long long p_range=(unsigned long long)p_diff<<(long long)SHIFTUPBITS;
  double p=((double)p_range)/(double)0x100000000LL;
  double this_entropy=-log(p)/log(2);
  if (0)
    LOGI("%s: entropy of range 0x%llx(p_low=0x%x, p_high=0x%x, p_diff=0x%llx) = %f, shiftupbits=%lld\n",
	   c->debug,p_range,p_low,p_high,p_diff,this_entropy,SHIFTUPBITS);
  if (this_entropy<0) {
    LOGE("entropy of symbol is negative! (p=%f, e=%f)\n",p,this_entropy);
    return -1;
  }
  c->entropy+=this_entropy;

  range_check(c);
  if (range_emit_stable_bits(c)) return -1;

  if (c->debug) {
    unsigned long long space=range_space(c);
    LOGI("%s: after rescale: space=0x%08llx[%s], low=0x%08x, high=0x%08x\n",
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
    LOGE("%s() passed alphabet_size>0x400000\n",__FUNCTION__);
    c->errors++;
    return -1;
  }
  if (alphabet_size<1) return 0;

  unsigned int p_low,p_high;
  range_equiprobable_range(c,alphabet_size,symbol,&p_low,&p_high);
  if (c->debug)
    LOGI("Encoding %d/%d: p_low=0x%x, p_high=0x%x\n",
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
    LOGI("decoding: alphabet size = %d, estimating s=%d (0x%x)\n",
	    alphabet_size,s,s);

  int symbol;

  for(symbol=s?s-1:0;symbol<s+2&&symbol<alphabet_size;symbol++)
    {
      unsigned int p_low,p_high;
      range_equiprobable_range(c,alphabet_size,symbol,&p_low,&p_high);
      if (!range_decode_common(c,p_low,p_high,symbol)) {
	if (c->debug) 
	  LOGI("Decoding %d/%d p_low=0x%x, p_high=0x%x\n",
		  symbol,alphabet_size,p_low,p_high);
	return symbol;     
      }
    }

  LOGE("Internal error in range_decode_equiprobable().\n");
  
  s=v*alphabet_size/space;
  LOGE("Estimated s=%d (0x%x)\n",s,s);
  for(symbol=s?s-1:0;symbol<s+2&&symbol<alphabet_size;symbol++)
    {
      unsigned int p_low,p_high;
      c->debug="here";
      range_equiprobable_range(c,alphabet_size,symbol,&p_low,&p_high);
      LOGE("tried symbol=%d (0x%x) : p_low=0x%x, p_high=0x%x\n",
	      symbol,symbol,p_low,p_high);
      if (range_decode_common(c,p_low,p_high,symbol)) {
	LOGE("  and range_decode_common() failed.\n");
      }
    } 

  return -1;
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

  LOGI("range  low: %s%s (offset=%d bits)\n",spaces,asbits(c->low),c->bits_used);
  LOGI("     value: %s%s (0x%08x/0x%08llx = 0x%08llx)\n",
	 range_coder_lastbits(c,90),decoderP?"":asbits(value),
	 (value-c->low),space,
	 (((unsigned long long)value-(unsigned long long)c->low)<<32LL)/
	 (space?space:1));
  LOGI("range high: %s%s\n",spaces,asbits(c->high));
  return 0;
}

/* No more symbols, so just need to output enough bits to indicate a position
   in the current range */
int range_conclude(range_coder *c)
{
  int bits;
  unsigned int v;
  unsigned int mean=((c->high-c->low)/2)+c->low;

  range_check(c);

  int i,msb=(mean>>31)&1;

  /* output msb and any deferred underflow bits. */
  if (c->debug) LOGI("conclude emit: %d\n",msb);
  if (range_emitbit(c,msb)) return -1;
  if (c->underflow>0) if (c->debug) LOGI("  plus %d underflow bits.\n",c->underflow);
  while(c->underflow-->0) if (range_emitbit(c,msb^1)) return -1;

  /* shift out msb */
  c->low=(c->low<<1)&0x7fffffff;
  c->high=(c->high<<1)|0x80000001;
  if (c->debug) {
    LOGI("after shifting out msb and underflow bits: low=0x%x, high=0x%x\n",
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
	LOGE("Could not conclude coder:\n");
	LOGE("  low=0x%08x, high=0x%08x\n",c->low,c->high);
	return -1;
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
    LOGI("%d bits to conclude 0x%08x (low=%08x, mean=%08x, high=%08x\n",
	   bits,v,c->low,mean,c->high);
    range_status(c,0);
    c->value=0;
  }

  /* now push bits until we know we have enough to unambiguously place the value
     within the final probability range. */
  for(i=0;i<bits;i++) {
    int b=(v>>(31-i))&1;
    if (c->debug) LOGI("  ordinary bit: %d\n",b);
    if (range_emitbit(c,b)) return -1;
  }
  //  LOGI(" (of %s)\n",asbits(mean));
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
  bzero(c, sizeof(struct range_coder));
  if (bytes)
    c->bit_stream=malloc(bytes);
  c->bit_stream_length=bytes*8;
  range_coder_reset(c);
  return c;
}



/* Assumes probabilities are cumulative */
int range_encode_symbol(range_coder *c,unsigned int frequencies[],int alphabet_size,int symbol)
{
  if (c->errors) return -1;
  range_check(c);

  assert(symbol>=0);
  assert(symbol<alphabet_size);

  unsigned int p_low=0;
  if (symbol>0) p_low=frequencies[symbol-1];
  unsigned int p_high=MAXVALUEPLUS1;
  if (symbol<(alphabet_size-1)) p_high=frequencies[symbol];
  // range_status(c,0);
  // LOGI("symbol=%d, p_low=%u, p_high=%u\n",symbol,p_low,p_high);
  return range_encode(c,p_low,p_high);
}

int _range_check(range_coder *c,int line)
{
  if (c->low>=c->high) 
    {
      if (!line) return -1;
      LOGE("c->low >= c->high at line %d\n",line);
      return -1;
    }
  if (!c->decodingP) return 0;

  if (c->value>c->high||c->value<c->low) {
    if (!line) return -1;
    LOGE("c->value out of bounds %d\n",line);
    LOGE("  low=0x%08x, value=0x%08x, high=0x%08x\n",
	    c->low,c->value,c->high);
    range_status(c,1);
    return -1;
  }
  return 0;
}

int range_decode_symbol(range_coder *c,unsigned int frequencies[],int alphabet_size)
{
  c->decodingP=1;
  range_check(c);
  c->decodingP=0;
  int s;
  unsigned long long space=range_space(c);
  //  unsigned long long v=(((unsigned long long)(c->value-c->low))<<24LL)/space;
  
  if (c->debug) LOGI(" decode: value=0x%08x; ",c->value);
  // range_status(c);
  
  for(s=0;s<(alphabet_size-1);s++) {
    unsigned int boundary=c->low+((frequencies[s]*space)>>(32LL-SHIFTUPBITS));
    if (c->value<boundary) {
      if (c->debug) {
	LOGI("value(0x%x) < frequencies[%d](boundary = 0x%x)\n",
	       c->value,s,boundary);
	if (s>0) {
	  boundary=c->low+((frequencies[s-1]*space)>>(32LL-SHIFTUPBITS));
	  LOGI("  previous boundary @ 0x%08x\n",boundary);
	}
      }
      break;
    } else {
      if (0&&c->debug)
	LOGI("value(0x%x) >= frequencies[%d](boundary = 0x%x)\n",
	       c->value,s,boundary);
    }
  }
  
  unsigned int p_low=0;
  if (s>0) p_low=frequencies[s-1];
  unsigned int p_high=MAXVALUEPLUS1;
  if (s<alphabet_size-1) p_high=frequencies[s];

  if (c->debug) LOGI("s=%d, value=0x%08x, p_low=0x%08x, p_high=0x%08x\n",
		       s,c->value,p_low,p_high);
  // range_status(c);

  // LOGI("in decode_symbol() about to call decode_common()\n");
  // range_status(c,1);
  if (range_decode_common(c,p_low,p_high,s)) {
    LOGE("range_decode_common() failed for some reason.\n");
    return -1;
  }
  return s;
}

int range_calc_new_range(range_coder *c,
			 unsigned int p_low, unsigned int p_high,
			 unsigned int *new_low,unsigned int *new_high)
{
  unsigned long long space=range_space(c);
  if (c->debug) LOGE("calculating new range using space=0x%llx\n",space);

  if (space<MAXVALUEPLUS1) {
    c->errors++;
    if (c->debug) LOGI("%s : ERROR: space(0x%08llx)<0x%08x\n",c->debug,space,MAXVALUEPLUS1);
    return -1;
  }

  if (c->debug) {
    LOGE("%s(): space=0x%llx, c->low=0x%x, c->high=0x%x, p_low=0x%x, p_high=0x%x\n",
	    __FUNCTION__,space,c->low,c->high,p_low,p_high);
  }
  if(c->debug) LOGE("(0x%x * 0x%llx)>>24 = 0x%llx\n",p_low,space,(p_low*space)>>24LL);
  *new_low=c->low+((p_low*space)>>(32LL-SHIFTUPBITS));
  *new_high=c->low+(((p_high)*space)>>(32LL-SHIFTUPBITS))-1;
  if (p_high>=MAXVALUEPLUS1) *new_high=c->high;

  if (c->decodingP)
    if (*new_low>c->value||*new_high<c->value) {
      if (c->debug) {
	LOGE("%s(): new range would be invalid: space=0x%llx, c->low=0x%x, c->high=0x%x, p_low=0x%x, p_high=0x%x\n",
		__FUNCTION__,space,c->low,c->high,p_low,p_high);
	LOGE("  new_low=0x%x, new_high=0x%x, c->value=0x%x\n",
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
  
  if (_range_check(c,0 /* don't abort if things go wrong */)) {
    if (c->debug) LOGE("range check failed");
    return -1;
  }

  if (range_calc_new_range(c,p_low,p_high,&new_low,&new_high)) {
    if (c->debug) LOGE("range calc new range failed");
    return -1;
  }

  if (new_high>0xffffffff) {
    LOGI("new_high=0x%08x\n",new_high);
    new_high=0xffffffff;
  }

  if (0) {
    LOGI("rdc: low=0x%08x, value=0x%08x, high=0x%08x\n",c->low,c->value,c->high);
    LOGI("rdc: p_low=0x%08x, p_high=0x%08x, space=%08llx, s=%d\n",
	   p_low,p_high,range_space(c),s);  
  }

  c->decodingP=1;
  if (_range_check(c,0 /* don't abort if things go wrong */)) {
    if (c->debug) LOGE("range check failed");
    return -1;
  }

  if (new_low>c->value||new_high<c->value) {
    if (c->debug) {
      LOGE("c->value would be out of bounds");
      LOGE("  new_low=0x%08x, c->value=0x%08x, new_high=0x%08x\n",new_low,c->value,new_high);
      LOGE("  low=0x%08x, value=0x%08x, high=0x%08x\n",c->low,c->value,c->high);
      LOGE("  p_low=0x%08x, p_high=0x%08x, space=%08llx, s=%d\n",
	      p_low,p_high,range_space(c),s);
      LOGE("  c->low=0x%x, c->high=0x%x\n",c->low,c->high);
    
    }
    return -1;
  }

  /* work out how many bits are still significant */
  c->low=new_low;
  c->high=new_high;
  if (_range_check(c,0 /* don't abort if things go wrong */)) {
    if (c->debug) LOGE("range check failed");
    return -1;
  }
  
  if (c->debug) LOGI("%s: after decode: low=0x%08x, high=0x%08x\n",
		       c->debug,c->low,c->high);

  // LOGI("after decode before renormalise:\n");
  // range_status(c,1);

  range_emit_stable_bits(c);
  c->decodingP=0;
  range_check(c);

  if (c->debug) LOGI("%s: after rescale: low=0x%08x, high=0x%08x\n",
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

range_coder *range_coder_dup(range_coder *in)
{
  range_coder *out=calloc(sizeof(range_coder),1);
  if (!out) {
    LOGE("allocation of range_coder in range_coder_dup() failed.\n");
    return NULL;
  }
  bcopy(in,out,sizeof(range_coder));
  out->bit_stream=malloc(bits2bytes(out->bit_stream_length));
  if (!out->bit_stream) {
    LOGE("range_coder_dup() failed\n");
    free(out);
    return NULL;
  }
  if (out->bits_used>out->bit_stream_length) {
    LOGE("bits_used>bit_stream_length in range_coder_dup()\n");
    LOGE("  bits_used=%d, bit_stream_length=%d\n",
	    out->bits_used,out->bit_stream_length);
    free(out);
    return NULL;
  }
  bcopy(in->bit_stream,out->bit_stream,bits2bytes(out->bits_used));
  return out;
}

int range_coder_free(range_coder *c)
{
  if (c->bit_stream)
    free(c->bit_stream);
  c->bit_stream=NULL;
  bzero(c,sizeof(range_coder));
  free(c);
  return 0;
}

