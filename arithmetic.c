#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
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

  while(1) {

  /* look for actually stable bits */
  if (!((c->low^c->high)&0x80000000))
    {
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
#if 1
  /* Now see if we have underflow, and need to count the number of underflowed
     bits. */
  else if (((c->low&0xc0000000)==0x40000000)
	   &&((c->high&0xc0000000)==0x80000000))
    {
      c->underflow++;
      c->low=(c->low&0x80000000)|((c->low<<1)&0x7fffffff);
      c->high=(c->high&0x80000000)|((c->high<<1)&0x7fffffff);
      c->high|=1;
    }
#endif
  else 
    return;
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

  range_emit_stable_bits(c);
  return 0;
}

int range_status(range_coder *c)
{
  printf("range=[%s,",asbits(c->low));
  printf("%s)\n",asbits(c->high));

  return 0;
}

/* No more symbols, so just need to output enough bits to indicate a position
   in the current range */
int range_conclude(range_coder *c)
{
  int bits=0;
  unsigned int v;
  unsigned int mean=((c->high-c->low)/2)+c->low;

  /* wipe out hopefully irrelevant bits from low part of range */
  v=0;
  while((v<=c->low)||(v>=c->high))
    {
      bits++;
      v=(mean>>(32-bits))<<(32-bits);
    }

  int i,msb=(v>>31)&1;
  range_emitbit(c,msb);
  while(c->underflow-->0) range_emitbit(c,msb^1);
  for(i=1;i<bits;i++) {
    int b=(v>>(31-i))&1;
    range_emitbit(c,b);
  }

  return 0;
}

int range_coder_reset(struct range_coder *c)
{
  c->low=0;
  c->high=0xffffffff;
  c->entropy=0;
  c->bits_used=0;
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
  
  for(s=0;s<(alphabet_size-1);s++)
    if (v<frequencies[s]) break;
  
  double p_low=0;
  if (s>0) p_low=frequencies[s-1];
  double p_high=frequencies[s];

  double new_low=c->low+p_low*space;
  double new_high=c->low+p_high*space;

  /* work out how many bits are still significant */
  c->low=new_low;
  c->high=new_high;

  while(1) {
    if ((c->low&0x80000000)==(c->high&0x80000000))
      {
	/* MSBs match, so bit will get shifted out */
      }
#if 1
    else if (((c->low&0xc0000000)==0x40000000)
	     &&((c->high&0xc0000000)==0x80000000))
      {
	c->value^=0x40000000;
	c->low&=0x3fffffff;
	c->high|=0x40000000;
      }
#endif
    else {
      /* nothing can be done */
      return s;
    }

    c->low=c->low<<1;
    c->high=c->high<<1;
    c->high|=1;
    c->value=c->value<<1; 
    c->value|=range_decode_getnextbit(c);
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

int cmp_double(const void *a,const void *b)
{
  double *aa=(double *)a;
  double *bb=(double *)b;

  if (*aa<*bb) return -1;
  if (*aa>*bb) return 1;
  return 0;
}

int main() {
  struct range_coder *c=range_new_coder(8192);
  struct range_coder d;

  double frequencies[1024];
  int sequence[1024];
  int alphabet_size;
  int length;

  int test,i,j;

  srandom(0);

  for(test=0;test<1024;test++)
    {
      /* Pick a random alphabet size */
      alphabet_size=1+random()%1023;
 
      /* Generate incremental probabilities.
         Start out with randomly selected probabilities, then sort them.
	 It may make sense later on to use some non-uniform distributions
	 as well.
      */
      for(i=0;i<alphabet_size;i++)
	frequencies[i]=random()*1.0/(0x7fffffff);

      qsort(frequencies,alphabet_size,sizeof(double),cmp_double);

      /* now generate random string to compress */
      length=1+random()%1023;
      for(i=0;i<length;i++) sequence[i]=random()%alphabet_size;
      
      printf("Test #%d : %d symbols, with %d symbol alphabet\n",
	     test,length,alphabet_size);

      /* Encode the random symbols */
      range_coder_reset(c);
      for(i=0;i<length;i++)
	range_encode_symbol(c,frequencies,alphabet_size,sequence[i]);
      range_conclude(c);
      printf("  encoded %d symbols in %d bits (%f bits of entropy)\n",
	     length,c->bits_used,c->entropy);
      
      /* Copy encoder state to a new decoder.
         This also copies the pointer to the encoded data, which conveniently
         makes it available without us having to copy it. */
      bcopy(c,&d,sizeof(d));
      /* Now convert encoder state into a decoder state */
      d.bit_stream_length=d.bits_used;
      d.bits_used=0;
      range_decode_prefetch(&d);

      for(i=0;i<length;i++)
	{
	  int s=range_decode_symbol(&d,frequencies,alphabet_size);
	  if (s!=sequence[i]) {
	    fprintf(stderr,"Verify error decoding symbol #%d (expected %d, but got %d)\n",i,sequence[i],s);
	    range_status(&d);
	    double space=d.high-d.low;
	    double v=(d.value-d.low)/space;
	    
	    for(s=0;s<(alphabet_size-1);s++)
	      if (v<frequencies[s]) break;
	    
	    double p_low=0;
	    if (s>0) p_low=frequencies[s-1];
	    double p_high=frequencies[s];
	    fprintf(stderr,"  v=%f (0x%x)\n",v,d.value);

	    exit(-1);
	  }
	}
      printf("  successfully decoded and verified %d symbols.\n",length);
    }

  return 0;
}

