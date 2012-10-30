/*
  Compress short strings using english letter, bigraph, trigraph and quadgraph
  frequencies.  
  
  This part of the process only cares about lower-case letter sequences.  Encoding
  of word breaks, case changes and non-letter characters will be dealt with by
  separate layers.

  Interpolative coding is probably a good choice for a component in those higher
  layers, as it will allow efficient encoding of word break positions and other
  items that are possibly "clumpy"
*/

#include <stdio.h>
#include <strings.h>
#include <math.h>
#include <stdlib.h>

extern float tweet_freqs[72][72][72];

unsigned char chars[72]="abcdefghijklmnopqrstuvwxyz 0123456789!@#$%^&*()_+-=~`[{]}\\|;:'\"<,>.?/";
int charIdx(unsigned char c)
{
  int i;
  for(i=0;i<72;i++)
    if (c==chars[i]) return i;
       
  /* Not valid character -- must be encoded separately */
  return -1;
}

double encodeLCAlphaSpace(char *s)
{
  double bits=0;
  int c1=charIdx(' ');
  int c2=charIdx(' ');
  int o,i;
  for(o=0;o<strlen(s);o++) {
    int c3=charIdx(s[o]);
    float p=tweet_freqs[c1][c2][c3];
    float entropy=-log(p)/log(2);
    printf("%c : p=%f, entropy=%f\n",s[o],p,entropy);
    bits+=entropy;
    c1=c2; c2=c3;
  }
  printf("%d chars could be encoded in %f bits (%f per char)\n",
	 (int)strlen(s),bits,bits/strlen(s));
  return bits;
}

double encodeLength(char *m)
{
  int len=strlen(m);
  int bits=1;
  while((1<<bits)<len) bits++;
  float countBits=1+2*(bits-1);
  printf("Using %f bits to encode the length of the message.\n",countBits);

  return countBits;
}

float encodeNonAlpha(char *m)
{
  /* Get positions and values of non-alpha chars.
     Encode count, then write the chars, then use interpolative encoding to
     encode their positions. */

  char v[1024];
  int pos[1024];
  int count=0;
  
  int i;
  for(i=0;m[i];i++)
    if (charIdx(m[i])>=0) {
      /* alpha or space -- so ignore */
    } else {
      /* non-alpha, so remember it */
      v[count]=m[i];
      pos[count++]=i;
    }

  if (!count) {
    printf("Using 1 bit to indicate no non-alpha/space characters.\n");
    return 1;
  }

  printf("Using 8-bits to encode each of %d non-alpha chars.\n",count);
  float charBits=8*count;

  /* Encode number of non-alpha chars using:
     n 1's to specify how many bits required to encode the count.
     Then 0.
     Then n bits to encode the count.
     So 2*ceil(log2(count))+1 bits
  */
  int len=strlen(m);
  int bits=1;
  while((1<<bits)<len) bits++;
  float countBits=1+2*(bits-1);
  printf("Using %f bits to encode the number of non-alpha/space chars.\n",countBits);

  unsigned char out[1024];
  int posBits=0;

  ic_encode_heiriter(pos,count,NULL,NULL,len,len,out,&posBits);
  
  printf("Using interpolative coding for positions, total = %d bits.\n",posBits);

  return countBits+posBits+charBits;
}

double encodeCase(char *m)
{
  int pos[1024];
  int count=0;
  
  int i;
  int o=0;
  for(i=0;m[i];i++) {
    if (islower(m[i])||m[i]==' ') {
      /* lower case, so ignore */
    } else {
      /* upper case, so remember */
      pos[count++]=o;
    }
    /* Spaces do not have case, so don't bother remembering their
       positions, since they just add entropy. */
    if (m[i]!=' ') o++;
  }

  if (!count) {
    printf("Using 1 bit to indicate no upper-case characters.\n");
    return 1;
  }

  /* Encode number of upper case chars using:
     n 1's to specify how many bits required to encode the count.
     Then 0.
     Then n bits to encode the count.
     So 2*ceil(log2(count))+1 bits
  */
  int len=strlen(m);
  int bits=1;
  while((1<<bits)<len) bits++;
  float countBits=1+2*(bits-1);
  printf("Using %f bits to encode the number of upper-case chars.\n",countBits);

  unsigned char out[1024];
  int posBits=0;
  ic_encode_heiriter(pos,count,NULL,NULL,len,len,out,&posBits);
  
  printf("Using interpolative coding for upper-case positions, total = %d bits.\n",posBits);

  return countBits+posBits;
}

int stripNonAlpha(char *in,char *out)
{
  int l=0;
  int i;
  for(i=0;in[i];i++)
    if (isalpha(in[i])||in[i]==' ') out[l++]=in[i];
  out[l]=0;
  return 0;
}

int foldCase(char *in,char *out)
{
  int l=0;
  int i;
  for(i=0;in[i];i++) out[l++]=tolower(in[i]);
  out[l]=0;
  return 0;
}

int main(int argc,char *argv[])
{
  if (!argv[1]) {
    fprintf(stderr,"Must provide message to compress.\n");
    exit(-1);
  }

  char m[1024]; // raw message, no pre-processing
  char alpha[1024]; // message with all non alpha/spaces removed
  char lcalpha[1024]; // message with all alpha chars folded to lower-case
  
  double length=0;

  strcpy(m,argv[1]);

  length+=encodeLength(m);
  length+=encodeNonAlpha(m);
  stripNonAlpha(m,alpha);
  length+=encodeCase(alpha);
  foldCase(alpha,lcalpha);
  printf("XXX - do to: tokenisation of common words\n");
  length+=encodeLCAlphaSpace(lcalpha);
  
  printf("Total encoded length = %fbits (%f bits/char)\n",
	 length,length/strlen(m));
  
  return 0;
}
