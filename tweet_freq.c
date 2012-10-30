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
    //    printf("%c : p=%f, entropy=%f\n",s[o],p,entropy);
    bits+=entropy;
    c1=c2; c2=c3;
  }
  if (0) printf("%d chars could be encoded in %f bits (%f per char)\n",
		(int)strlen(s),bits,bits/strlen(s));
  return bits;
}

double encodeLength(char *m)
{
  int len=strlen(m);
  int bits=1;
  while((1<<bits)<len) bits++;
  float countBits=1+2*(bits-1);
  // printf("Using %f bits to encode the length of the message.\n",countBits);

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
    if (charIdx(tolower(m[i]))>=0) {
      /* alpha or space -- so ignore */
    } else {
      /* non-alpha, so remember it */
      v[count]=m[i];
      printf("non-alpha char: 0x%02x '%c'\n",m[i],m[i]);
      pos[count++]=i;
    }

  if (!count) {
    // printf("Using 1 bit to indicate no non-alpha/space characters.\n");
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
  // printf("Using %f bits to encode the number of non-alpha/space chars.\n",countBits);

  unsigned char out[1024];
  int posBits=0;

  ic_encode_heiriter(pos,count,NULL,NULL,len,len,out,&posBits);
  
  // printf("Using interpolative coding for positions, total = %d bits.\n",posBits);

  return countBits+posBits+charBits;
}

double encodeCase(char *m)
{
  int pos[1024];
  int count=0;
  
  int i;
  int o=0;
  //  printf("caps eligble chars: ");
  for(i=0;m[i];i++) {
    if (m[i]>='A'&&m[i]<='Z') {
      /* upper case, so remember */
      pos[count++]=o;
    }
    /* Spaces do not have case, so don't bother remembering their
       positions, since they just add entropy. */
    if (isalpha(m[i])) { 
      //      printf("%c",m[i]);
      o++; 
    }
  }
  //  printf("\n");

  if (!count) {
    // printf("Using 1 bit to indicate no upper-case characters.\n");
    return 1;
  }

  /* Encode number of upper case chars using:
     n 1's to specify how many bits required to encode the count.
     Then 0.
     Then n bits to encode the count.
     So 2*ceil(log2(count))+1 bits
  */
  int bits=1;
  while((1<<bits)<o) bits++;
  int countBits=1+2*(bits-1);
  if (0) printf("Using %d bits to encode that there are %d upper-case chars.\n",
	 countBits,count);

  unsigned char out[1024];
  int posBits=0;
  ic_encode_heiriter(pos,count,NULL,NULL,o,o,out,&posBits);
  
  //  printf("upper case cost = %d bits.\n",1+countBits+posBits);

  if ((countBits+posBits)>o) {
    /* more efficient to just use a bitmap */
    return 1+o;
  }

  /* 1 bit = "there are upper case chars"
     1 bit = 0 for not bitmap
     then bits to encode count and positions.
  */
  return 1+1+countBits+posBits;
}

int stripNonAlpha(char *in,char *out)
{
  int l=0;
  int i;
  for(i=0;in[i];i++)
    if (charIdx(tolower(in[i]))>=0) out[l++]=in[i];
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

int mungeCase(char *m)
{
  int i;

  /* flip case of first letter of message */
  for(i=1;m[i];i++) if (isalpha(m[i])) break;
    
  if (isalpha(m[0])&&((m[0]^m[i])&0x20)) m[0]^=0x20;

  /* Change isolated I's to i, provided preceeding char is lower-case
     (so that we don't mess up all-caps).
  */
  for(i=1;m[i+1];i++)
    if (m[i]=='I'&&(!isalpha(m[i-1]))&&(!isalpha(m[i+1])))
      {
	int j;
	int foo=0;
	for(j=i-2;j>=0;j--) 
	  if (isalpha(m[j])) 
	    {
	      foo=1;
	      if ((m[j]^m[i])&0x20) {
		/* case differs to previous character, so flip */
		m[i]^=0x20;
		printf("flipping I@%d in: %s\n",i,m);
	      }
	      break;
	    }
	if (!foo) m[i]^=0x20;
      }
     
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
  
  m[0]=0; fgets(m,1024,stdin);
  
  int lines=0;
  double runningPercent=0;
  double worstPercent=0,bestPercent=100;

  while(m[0]) {
    /* chop newline */
    m[strlen(m)-1]=0;
    if (1) printf(">>> %s\n",m);

    double lengthLength=encodeLength(m);
    double binaryLength=encodeNonAlpha(m);
    stripNonAlpha(m,alpha);
    mungeCase(alpha);
    double caseLength=encodeCase(alpha);
    foldCase(alpha,lcalpha);
    double alphaLength=encodeLCAlphaSpace(lcalpha);
    
    double length=lengthLength+binaryLength+caseLength+alphaLength;

    if ((length>=(8*strlen(m)))
	&&(binaryLength<=1)) {
      /* we can't encode it more efficiently than 7-bit ASCII */
      lengthLength=0;
      binaryLength=0;
      caseLength=0;
      alphaLength=8*strlen(m);
      length=alphaLength;
    }

    double percent=length*12.5/strlen(m);
    if (percent<bestPercent) bestPercent=percent;
    if (percent>worstPercent) worstPercent=percent;
    runningPercent+=percent;
    lines++;

    printf("Total encoded length = L%.1f+B%.1f+C%.1f+A%.1f = %f bits = %.2f%% (best:avg:worst %.2f%%:%.2f%%:%.2f%%)\n",
	   lengthLength,binaryLength,caseLength,alphaLength,
	   length,percent,bestPercent,runningPercent/lines,worstPercent);
    m[0]=0; fgets(m,1024,stdin);
  }
  return 0;
}
