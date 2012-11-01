#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "arithmetic.h"

int range_decode_getnextbit(range_coder *c)
{
  /* return 0s once we have used all bits */
  if (c->bit_stream_length<(c->bits_used)) return 0;

  int bit=c->bit_stream[c->bits_used>>3]&(1<<(7-(c->bits_used&7)));
  c->bits_used++;
  if (bit) return 1;
  return 0;
}

void range_emitbit(range_coder *c,int b)
{
  printf(">>> %d\n",b);
  if (c->bits_used>=(c->bit_stream_length)) {
    fprintf(stderr,"output overflow.\n");
    exit(-1);
  }
  int bit=(c->bits_used&7)^7;
  c->bit_stream[c->bits_used>>3]|=(b<<bit);
  c->bits_used++;
  return;
}

void range_emit_stable_bits(range_coder *c)
{
  /* look for actually stable bits */
  while(!((c->low^c->high)&0x80000000))
    {
      printf("emit stable bit: ");
      range_status(c);
      int msb=c->low>>31;
      range_emitbit(c,msb);
      if (c->underflow) {
	int u;
	if (msb) u=0; else u=1;
	while (c->underflow-->0) range_emitbit(c,u);
	c->underflow=0;
      }
      c->low=c->low<<1;
      c->high=c->high<<1;
      c->high|=1;
    }

  /* Now see if we have underflow, and need to count the number of underflowed
     bits. */
  while (((c->low&0xc0000000)==0x40000000)
	 &&((c->high&0xc0000000)==0x80000000))
    {
      c->underflow++;
      c->low=(c->low&0x80000000)|((c->low<<1)&0x7fffffff);
      c->high=(c->high&0x80000000)|((c->high<<1)&0x7fffffff);
      c->high|=1;
    }

  return;
}

void range_emitbits(range_coder *c,int n)
{
  int i;
  for(i=0;i<n;i++)
    {
      range_emitbit(c,(c->low>>31));
      c->low=c->low<<1;
      c->high=c->high<<1;
      c->high|=1;
    }
  return;
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

int range_encode(range_coder *c,double p_low,double p_high)
{
  unsigned int space=c->high-c->low;
  if (space<0x10000) {
    fprintf(stderr,"Ran out of room in coder space (convergence around 0.5?)\n");
    exit(-1);
  }
  double new_low=c->low+p_low*space;
  double new_high=c->low+p_high*space;

  c->low=new_low;
  c->high=new_high;

  c->entropy+=-log(p_high-p_low)/log(2);

  range_status(c);

  range_emit_stable_bits(c);
  return 0;
}

int range_status(range_coder *c)
{
  printf("range=[%s,\n",asbits(c->low));
  printf("       %s)\n",asbits(c->high));
  return 0;
}

/* No more symbols, so just need to output enough bits to indicate a position
   in the current range */
int range_conclude(range_coder *c)
{
  int bits=0;
  unsigned int v;
  unsigned int mean=((c->high-c->low)/2)+c->low;

  printf("concluding:\n");
  range_status(c);

  /* wipe out hopefully irrelevant bits from low part of range */
  v=0;
  printf("v[0] = %s\n",asbits(v));
  while((v<=c->low)||(v>=c->high))
    {
      bits++;
      v=(mean>>(32-bits))<<(32-bits);
      printf("v[%d] = %s\n",bits,asbits(v));
    }
  printf("Need %d bits to conclude: %s\n",bits,asbits(v));

  int i,msb=(v>>31)&1;
  printf("msb=%d\n",msb);
  range_emitbit(c,msb);
  while(c->underflow-->0) range_emitbit(c,msb^1);
  for(i=1;i<bits;i++) {
    int b=(v>>(31-i))&1;
    range_emitbit(c,b);
  }

  return 0;
}

struct range_coder *range_new_coder(int bytes)
{
  struct range_coder *c=calloc(sizeof(struct range_coder),1);
  c->bit_stream=malloc(bytes);
  c->bit_stream_length=bytes*8;
  c->high=0xffffffff;
  return c;
}

/* Assumes probabilities are cumulative */
int range_encode_symbol(range_coder *c,double frequencies[],int alphabet_size,int symbol)
{
  double p_low=0;
  if (symbol>0) p_low=frequencies[symbol-1];
  double p_high=1;
  if (symbol<(alphabet_size-1)) p_high=frequencies[symbol];
  return range_encode(c,p_low,p_high);
}

int range_decode_symbol(range_coder *c,double frequencies[],int alphabet_size)
{
  int s;
  double space=c->high-c->low;
  double v=(c->value-c->low)/space;
  
  for(s=0;s<alphabet_size;s++)
    if (v<frequencies[s]) break;
  
  double p_low=0;
  if (s>0) p_low=frequencies[s-1];
  double p_high=frequencies[s];

  double new_low=c->low+p_low*space;
  double new_high=c->low+p_high*space;

  /* work out how many bits are still significant */
  c->low=new_low;
  c->high=new_high;

  printf("after narrowing: ");
  range_status(c);

  /* discard stable bits */
  while (!((c->low^c->high)&0x80000000))
    {
      c->low=c->low<<1;
      c->high=c->high<<1;
      c->high|=1;
      c->value=c->value<<1;
      c->value|=range_decode_getnextbit(c);
      printf("shifting out stable bit: ");
      range_status(c);
    }

  /* discard underflow bits, in effect doubling the range, while keeping it
     centred. */
  while (((c->low&0xc0000000)==0x40000000)
	 &&((c->high&0xc0000000)==0x80000000))
    {
      c->underflow++;
      c->low=(c->low&0x80000000)|((c->low<<1)&0x7fffffff);
      c->high=(c->high&0x80000000)|((c->high<<1)&0x7fffffff);
      c->high|=1;
      c->value=c->value<<1;
      c->value|=range_decode_getnextbit(c);
      printf("shifting out underflow bit: ");
      range_status(c);
    }
  
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

int main() {
  struct range_coder *c=range_new_coder(1024);
  
  double frequencies[4]={0.2,0.3,0.7,0.9};

  range_status(c);
  range_encode_symbol(c,frequencies,5,2);
  range_status(c);
  range_encode_symbol(c,frequencies,5,2);
  range_status(c);
  range_encode_symbol(c,frequencies,5,4);
  range_status(c);
  range_conclude(c);
  printf("Wrote %d bits to encode %.3f bits of entropy.\n",
	 c->bits_used,c->entropy);
  printf("wrote: %s\n",asbits(*(unsigned int *)&c->bit_stream[0]));

  c->bit_stream_length=c->bits_used;
  c->bits_used=0;
  range_decode_prefetch(c);
  printf("decode state: %s\n",asbits(c->value));
  range_status(c);
  int s;
  s=range_decode_symbol(c,frequencies,5);
  printf("Decoded symbol #%d\n",s);
  printf("decode state: %s\n",asbits(c->value));
  range_status(c);
  s=range_decode_symbol(c,frequencies,5);
  printf("Decoded symbol #%d\n",s);
  printf("decode state: %s\n",asbits(c->value));
  range_status(c);
  s=range_decode_symbol(c,frequencies,5);
  printf("Decoded symbol #%d\n",s);
  printf("decode state: %s\n",asbits(c->value));
  range_status(c);

  return 0;
}

