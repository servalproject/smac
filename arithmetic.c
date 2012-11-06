#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <math.h>
#include "arithmetic.h"

#undef UNDERFLOWFIXENCODE
#undef UNDERFLOWFIXDECODE

int bits2bytes(int b)
{
  int extra=0;
  if (b&7) extra=1;
  return (b>>3)+extra;
}

int range_decode_getnextbit(range_coder *c)
{
  /* return 0s once we have used all bits */
  if (c->bit_stream_length<=c->bits_used) return 0;

  int bit=c->bit_stream[c->bits_used>>3]&(1<<(7-(c->bits_used&7)));
  c->bits_used++;
  if (bit) return 1;
  return 0;
}

int range_emitbit(range_coder *c,int b)
{
  if (c->bits_used>=(c->bit_stream_length)) {
    printf("out of bits\n");
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

  while(1) {

  /* look for actually stable bits */
  if (!((c->low^c->high)&0x80000000))
    {
      int msb=c->low>>31;
      if (range_emitbit(c,msb)) return -1;
      if (c->underflow) {
	int u;
	if (msb) u=0; else u=1;
	while (c->underflow-->0) if (range_emitbit(c,u)) return -1;
	c->underflow=0;
      }
      c->low=c->low<<1;
      c->high=c->high<<1;
      c->high|=1;
    }
#ifdef UNDERFLOWFIXENCODE
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
    return 0;
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

int range_encode(range_coder *c,unsigned int p_low,unsigned int p_high)
{
  if (p_low>p_high) {
    fprintf(stderr,"range_encode() called with p_low>p_high: p_low=%u, p_high=%u\n",
	    p_low,p_high);
    exit(-1);
  }

  unsigned long long space=(unsigned long long)c->high-(unsigned long long)c->low+1;
  if (space<0x100000) {
    fprintf(stderr,"Ran out of room in coder space (convergence around 0.5?)\n");
    exit(-1);
  }
  unsigned int new_low=c->low+((p_low*space)>>32LL);
  unsigned int new_high=c->low+((p_high*space)>>32LL);

  c->low=new_low;
  c->high=new_high;

  c->entropy+=-log(p_high-p_low+1)/log(2);

  if (range_emit_stable_bits(c)) return -1;
  return 0;
}

int range_encode_equiprobable(range_coder *c,int alphabet_size,int symbol)
{
  unsigned int p_low=((unsigned long long)symbol<<32LL)/alphabet_size;
  unsigned int p_high=((unsigned long long)(symbol+1)<<32LL)/alphabet_size-1;
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
  unsigned long long space=(unsigned long long)c->high-(unsigned long long)c->low+1;
  if (!c) return -1;
  char *prefix=range_coder_lastbits(c,90);
  char spaces[8193];
  int i;
  for(i=0;prefix[i];i++) spaces[i]=' '; 
  if (decoderP&&(i>=32)) i-=32;
  spaces[i]=0; prefix[i]=0;

  printf("range  low: %s%s (offset=%d bits)\n",spaces,asbits(c->low),c->bits_used);
  printf("     value: %s%s (%u/%llu = %llu)\n",
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
  int bits=0;
  unsigned int v;
  unsigned int mean=((c->high-c->low)/2)+c->low;

  /* wipe out hopefully irrelevant bits from low part of range */
  v=0;
  int mask=0xffffffff;
  while((v<=c->low)||((v+mask)>=c->high))
    {
      bits++;
      v=(mean>>(32-bits))<<(32-bits);
      mask=0xffffffff>>bits;
    }
  /* Actually, apparently 2 bits is always the correct answer, because normalisation
     means that we always have 2 uncommitted bits in play, excepting for underflow
     bits, which we handle separately. */
  if (bits<2) bits=2;

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
  //  printf("conclude emit: %d",msb);
  if (range_emitbit(c,msb)) return -1;
  //  if (c->underflow>0) printf("  plus %d underflow bits.\n",c->underflow);
  while(c->underflow-->0) if (range_emitbit(c,msb^1)) return -1;

  /* now push bits until we know we have enough to unambiguously place the value
     within the final probability range. */
  for(i=1;i<bits;i++) {
    int b=(v>>(31-i))&1;
    //    printf("%d",b);
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
  unsigned int p_low=0;
  if (symbol>0) p_low=frequencies[symbol-1];
  unsigned int p_high=0xffffffff;
  if (symbol<(alphabet_size-1)) p_high=frequencies[symbol]-1;
  //  range_status(c,0);
  //  printf("symbol=%d, p_low=%u, p_high=%u\n",symbol,p_low,p_high);
  return range_encode(c,p_low,p_high);
}

int range_check(range_coder *c,int line)
{
  if (c->value>c->high||c->value<c->low) {
    fprintf(stderr,"c->value out of bounds %d\n",line);
    range_status(c,1);
    exit(-1);
  }
  return 0;
}

int range_decode_equiprobable(range_coder *c,int alphabet_size)
{
  unsigned long long s;
  unsigned long long space=(unsigned long long)c->high-(unsigned long long)c->low+1;
  unsigned int v=(c->value-c->low)/space;
  s=v/alphabet_size;
  unsigned int p_low=(s<<32LL)/alphabet_size;
  unsigned int p_high=((s+1)<<32LL)/alphabet_size-1;
  if (s==alphabet_size) p_high=0xffffffff;
  
  return range_decode_common(c,p_low,p_high,s);
}

int range_decode_symbol(range_coder *c,unsigned int frequencies[],int alphabet_size)
{
  int s;
  unsigned long long space=(unsigned long long)c->high-(unsigned long long)c->low+1;
  unsigned int v=(((unsigned long long)(c->value-c->low))<<32LL)/space;
  
  //  printf(" decode: v=%f; ",v);
  // range_status(c);

  for(s=0;s<(alphabet_size-1);s++)
    if (v<=frequencies[s]) break;
  
  unsigned int p_low=0;
  if (s>0) p_low=frequencies[s-1];
  unsigned int p_high=0xffffffff;
  if (s<alphabet_size-1) p_high=frequencies[s]-1;

  // printf("s=%d, v=%f, p_low=%f, p_high=%f\n",s,v,p_low,p_high);
  // range_status(c);

  // printf("in decode_symbol() about to call decode_common()\n");
  // range_status(c,1);
    return range_decode_common(c,p_low,p_high,s);
}

int range_decode_common(range_coder *c,unsigned int p_low,unsigned int p_high,int s)
{
  unsigned long long space=(unsigned long long)c->high-(unsigned long long)c->low+1;
  long long new_low=c->low+(((unsigned long long)p_low*space)>>32LL);
  long long new_high=c->low+(((unsigned long long)p_high*space)>>32LL);
  if (p_high>=0xffffffff) new_high=c->high;
  if (new_high>0xffffffff) {
    printf("new_high=0x%llx\n",new_high);
    new_high=0xffffffff;
  }

  // printf("p_low=0x%08x, p_high=0x%08x, space=%llu\n",p_low,p_high,space);  

  /* work out how many bits are still significant */
  c->low=new_low;
  c->high=new_high;

  // printf("after decode before renormalise:\n");
  // range_status(c,1);

  range_check(c,__LINE__);
  while(1) {
    if ((c->low&0x80000000)==(c->high&0x80000000))
      {
	/* MSBs match, so bit will get shifted out */
      }
#ifdef UNDERFLOWFIXDECODE
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
      range_check(c,__LINE__);
      return s;
    }

    c->low=c->low<<1;
    c->high=c->high<<1;
    c->high|=1;
    c->value=c->value<<1; 
    c->value|=range_decode_getnextbit(c);
    if (c->value>c->high||c->value<c->low) {
      fprintf(stderr,"c->value out of bounds @ line %d\n",__LINE__);
      fprintf(stderr,"c->low=0x%08x, c->value=0x%08x, c->high=0x%08x\n",c->low,c->value,c->high);
      range_status(c,1);
      exit(-1);
    }
  }
  range_check(c,__LINE__);
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

  unsigned int frequencies[1024];
  int sequence[1024];
  int alphabet_size;
  int length;

  int test,i,j;

  srandom(0);

  for(test=0;test<1024;test++)
    {
      /* Pick a random alphabet size */
      // alphabet_size=1+random()%1023;
      alphabet_size=1+test%1023;
 
      /* Generate incremental probabilities.
         Start out with randomly selected probabilities, then sort them.
	 It may make sense later on to use some non-uniform distributions
	 as well.

	 We only need n-1 probabilities for alphabet of size n, because the
	 probabilities are fences between the symbols, and p=0 and p=1 are implied
	 at each end.
      */
      for(i=0;i<alphabet_size-1;i++)
	frequencies[i]=random()<<1;
      frequencies[alphabet_size-1]=0;

      qsort(frequencies,alphabet_size-1,sizeof(unsigned int),cmp_uint);

      /* now generate random string to compress */
      length=1+random()%1023;
      for(i=0;i<length;i++) sequence[i]=random()%alphabet_size;
      
      printf("Test #%d : %d symbols, with %d symbol alphabet\n",
	     test,length,alphabet_size);\

      /* Quick test. If it works, no need to go into more thorough examination. */
      range_coder_reset(c);
      for(i=0;i<length;i++) {
	if (range_encode_symbol(c,frequencies,alphabet_size,sequence[i]))
	  {
	    fprintf(stderr,"Error encoding symbol #%d of %d (out of space?)\n",
		    i,length);
	    exit(-1);
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
      range_coder_free(vc);
      /* go to next test if this one passes. */
      if (!error) {
	fprintf(stderr,"Test #%d passed: encoded %d symbols in %d bits (%f bits of entropy)\n",
	       test,length,c->bits_used,c->entropy);
	continue;
      }
      {
	int k;
	printf("symbol probability steps: ");
	for(k=0;k<alphabet_size-1;k++) printf(" %f[0x%08x]",frequencies[k]*1.0/0x100000000,frequencies[k]);
	printf("\n");
	printf("symbol list: ");
	for(k=0;k<length;k++) printf(" %d#%d",sequence[k],k);
	printf("\n");
      }
      fprintf(stderr,"Test #%d failed: verify error of symbol %d\n",test,i);


      /* Encode the random symbols */
      range_coder_reset(c);
      for(i=0;i<length;i++) {
	range_coder *dup=range_coder_dup(c);

	if (range_encode_symbol(c,frequencies,alphabet_size,sequence[i]))
	  {
	    fprintf(stderr,"Error encoding symbol #%d of %d (out of space?)\n",
		    i,length);
	    exit(-1);
	  }

	/* verify as we go, so that we can report any divergence that we
	   notice. */
	range_coder *vc=range_coder_dup(c);
	range_conclude(vc);
	// printf("bit sequence: %s\n",range_coder_lastbits(vc,8192));
	/* Now convert encoder state into a decoder state */
	vc->bit_stream_length=vc->bits_used;
	vc->bits_used=0;
	range_decode_prefetch(vc);

	for(j=0;j<=i;j++) {
	  range_coder *vc2=range_coder_dup(vc);
	  if (!vc2) {
	    fprintf(stderr,"vc2=range_coder_dup(vc) failed\n");
	    exit(-1);
	  }	  

	  if (0&&j==i) {
	    printf("coder status before emitting symbol #%d:\n",i); 
	    range_status(dup,0);
	    printf("coder status after emitting symbol #%d:\n",i); 
	    range_status(c,0);
	  }
	  if (0) {
	    printf("decoder status before extracting symbol #%d:\n",j);
	    range_status(vc2,1);

	    int s;
	    unsigned long long space=(unsigned long long)vc2->high-(unsigned long long)vc2->low+1;
	    unsigned int v=((unsigned long long)(vc2->value-vc2->low)<<32LL)/space;
	    
	    printf(" decode: v=%u\n",v);
	    range_status(c,1);
	    
	    for(s=0;s<(alphabet_size-1);s++)
	      if (v<frequencies[s]) break;
	    
	    double p_low=0;
	    if (s>0) p_low=frequencies[s-1]*1.0/0x100000000;
	    double p_high=1;
	    if (s<alphabet_size-1) p_high=frequencies[s]*1.0/0x100000000;
	    
	    //	    printf("  s=%d, p_v=%f, p_low=%f, p_high=%f\n",s,v*1.0/0x100000000,p_low,p_high);
	    //      printf("  low=0x%08x, v=0x%08x, high=0x%08x\n",vc2->low,vc2->value,vc2->high);
	  }

	  /* Show encoder status at that point in time */
	  range_coder *vc3=range_new_coder(8192);
	  int k;
	  for(k=0;k<j;k++) {	   
	    if (range_encode_symbol(vc3,frequencies,alphabet_size,sequence[k]))
	      {
		fprintf(stderr,"Error encoding symbol #%d of %d (out of space?)\n",
			k,length);
		exit(-1);
	      }		
	  }
	  /* only one side is expected to match at any time */
	  if (vc3->low!=vc2->low||vc3->high!=vc2->high) {
	    printf("Encoder/decoder status diverged after symbol #%d\n",j);
	    printf("decode:  low=0x%08x, v=0x%08x, high=0x%08x\n",vc2->low,vc2->value,vc2->high);
	    printf("encode:  low=0x%08x, v=0x%08x, high=0x%08x\n",vc3->low,vc3->value,vc3->high);
	    exit(-1);
	  }
	  
	  int s=range_decode_symbol(vc,frequencies,alphabet_size);
	  if (s!=sequence[j]) {
	    fflush(stdout);
	    fprintf(stderr,"Verify error decoding symbol %d of [0,%d] (expected %d, but got %d)\n",j,i,sequence[j],s);
	    if (j==i-1) {
	      fflush(stdout);
	      fprintf(stderr,"Verify is on final character.\n"
		      "Encoder state before encoding final symbol was as follows:\n");
	      fflush(stderr);
	    } else {
	      double p_v=(vc2->value-vc2->low)*1.0/(vc2->high-vc2->low+1);
	      
	      unsigned long long space=(unsigned long long)vc2->high-(unsigned long long)vc2->low+1;
	      unsigned int v=(((unsigned long long)(vc2->value-vc2->low))<<32LL)/space;

	      printf("Encoder/decoder status at symbol #%d\n",j);
	      printf("decode:  low=0x%08x, v=0x%08x, high=0x%08x\n",vc2->low,vc2->value,vc2->high);
	      printf("         p_v=%f, v=0x%08x\n",p_v,v);
		     
	      printf("encode:  low=0x%08x, v=0x%08x, high=0x%08x\n",vc3->low,vc3->value,vc3->high);	      
	    }
	    exit(-1);
	  }

	  range_coder_free(vc2);
	  range_coder_free(vc3);
	}

	range_coder_free(vc);
	range_coder_free(dup);
      }
      range_conclude(c);
    }

  return 0;
}

#endif
