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
#include <unistd.h>
#include <ctype.h>

#include "arithmetic.h"

extern float tweet_freqs3[69][69][69];
extern float tweet_freqs2[69][69];
extern float tweet_freqs1[69];

unsigned char chars[69]="abcdefghijklmnopqrstuvwxyz 0123456789!@#$%^&*()_+-=~`[{]}\\|;:'\"<,>.?/";
int charIdx(unsigned char c)
{
  int i;
  for(i=0;i<69;i++)
    if (c==chars[i]) return i;
       
  /* Not valid character -- must be encoded separately */
  return -1;
}

int encodeLCAlphaSpace(range_coder *c,char *s)
{
  int b=c->bits_used;
  int c1=charIdx(' ');
  int c2=charIdx(' ');
  int o;
  for(o=0;o<strlen(s);o++) {
    int c3=charIdx(s[o]);
    range_encode_symbol(c,tweet_freqs3[c1][c2],69,c3);
    c1=c2; c2=c3;
  }
  if (1) printf("%d chars could be encoded in %f bits (%f per char)\n",
		(int)strlen(s),c->bits_used-b,(c->bits_used-b)/strlen(s));
  return 0;
}

int encodeLength(range_coder *c,char *m)
{
  int b=c->bits_used;
  int len=strlen(m);
  while((1<<bits)<len) {
    range_emitbit(c,1);
  }
  for(i=bits-1;i>=0;i++) range_emitbit(c,(len>>i)&1);
    
  printf("Using %f bits to encode the length of the message.\n",c->bits_used-b);

  return 0;
}

int encodeNonAlpha(range_coder *c,char *m)
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

  // XXX - The following assumes that 50% of messages have special characters.
  // This is a patently silly assumption.
  if (!count) {
    // printf("Using 1 bit to indicate no non-alpha/space characters.\n");
    range_emitbit(c,1);
    return 0;
  } else {
    // There are special characters present 
    range_emitbit(c,0);
  }

  printf("Using 8-bits to encode each of %d non-alpha chars.\n",count);

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

  /* Encode the positions of special characters */
  // ic_encode_heiriter(pos,count,NULL,NULL,len,len,out,&posBits);
  fprintf(stderr,"Encoding positions of non-alpha characters not implemented.\n");
  exit(-1);
  
  /* Encode the characters */
  for(i=0;i<count;i++) {
    double p_low=v[i]*1.0/256;
    double p_high=v[i+1]*1.0/256-EPSILON;
    range_encode(c,p_low,p_high);
  }

  // printf("Using interpolative coding for positions, total = %d bits.\n",posBits);

  return countBits+posBits+charBits;
}

int foldCaseInitialCaps(char *m)
{
  int pos[1024];
  int inWord=0;

  int i;
  int o=0;
  for(i=0;m[i];i++) {
    int thisInWord=0;
    int skip=0;
    if (isalnum(m[i])) thisInWord=1;
    if (thisInWord&&(!inWord)) {
      /* start of word */
      if (m[i]>='A'&&m[i]<='Z') skip=1;
    }
    inWord=thisInWord;
    if (!skip) m[o++]=m[i];
  }
  m[o]=0;
  return 0;
}

int encodeCaseInitialCaps(range_coder *c,char *m)
{
  int pos[1024];
  int inWord=0;
  int wordCount=0;

  int i;
  int count=0;
  for(i=0;m[i];i++) {
    int thisInWord=0;
    if (isalnum(m[i])) thisInWord=1;
    if (thisInWord&&(!inWord)) {
      /* start of word */
      if (m[i]>='A'&&m[i]<='Z') pos[count++]=wordCount;
      wordCount++;      
    }
    inWord=thisInWord;
  }
  //  printf("\n");

  if (!count) {
    // printf("Using 1 bit to indicate no upper-case characters.\n");
    range_emitbit(c,0);
    return 1;
  } else range_emitbit(c,1);

  /* Encode number of words starting with upper case using:
     n 1's to specify how many bits required to encode the count.
     Then 0.
     Then n bits to encode the count.
     So 2*ceil(log2(count))+1 bits
  */
  int bits=1;
  while((1<<bits)<count) bits++;
  int countBits=1+2*(bits-1);
  if (1) printf("Using %d bits to encode that there are %d words with initial caps.\n",
	 countBits,wordCount);

  unsigned char out[1024];
  int posBits=0;
  ic_encode_heiriter(pos,count,NULL,NULL,wordCount,wordCount,out,&posBits);  

  //  printf("upper case cost = %d bits.\n",1+countBits+posBits);

  if ((countBits+posBits)>wordCount) {
    /* more efficient to just use a bitmap */
    return 1+wordCount;
  }

  /* 1 bit = "there are upper case chars"
     1 bit = 0 for not bitmap
     then bits to encode count and positions.
  */
  return 1+1+countBits+posBits;
}


double encodeCaseEdgeTriggered(char *m)
{
  int pos[1024];
  int count=0;
  int ucase=0;
  
  int i;
  int o=0;
  //  printf("caps eligble chars: ");
  for(i=0;m[i];i++) {
    int thisUpper=0;
    if (isalpha(m[i])) {
      if (m[i]>='A'&&m[i]<='Z') thisUpper=1;
      if (thisUpper!=ucase) {
	/* case changed, so remember */
	pos[count++]=o;
      }
      ucase=thisUpper;
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


double encodeCaseLevelTriggered(char *m)
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
  int i,j;

  /* flip case of first letter of message.
     Ideally we would detect if the message looks like all-caps first,
     but this is hard to do in practice.
  */

  if (isalpha(m[0])) {
    m[0]^=0x20;
  }

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

int freq_compress(range_coder *c,unsigned char *m)
{
  char alpha[1024]; // message with all non alpha/spaces removed
  char lcalpha[1024]; // message with all alpha chars folded to lower-case

  double lengthLength=encodeLength(c,m);
  double binaryLength=encodeNonAlpha(c,m);
  stripNonAlpha(m,alpha);
  mungeCase(alpha);
  double caseLengthI=encodeCaseInitialCaps(c,alpha);
  foldCaseInitialCaps(alpha);
  double caseLengthL=encodeCaseLevelTriggered(c,alpha);
  stripNonAlpha(m,alpha);
  mungeCase(alpha);
  foldCase(alpha,lcalpha);
  double alphaLength=encodeLCAlphaSpace(c,lcalpha);
  
  if (c->bits_used>=8*strlen(m))
    {
      /* we can't encode it more efficiently than 7-bit ASCII */
      range_reset(c);
      for(i=0;m[i];i++) {
	double p_low=m[i]*1.0/256;
	double p_high=m[i+1]*1.0/256-EPSILON;
	range_encode(c,p_low,p_high);
      }
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
  
  m[0]=0; fgets(m,1024,stdin);
  
  int lines=0;
  double runningPercent=0;
  double worstPercent=0,bestPercent=100;

  while(m[0]) {    
    /* chop newline */
    m[strlen(m)-1]=0;
    if (1) printf(">>> %s\n",m);

    range_coder *c=range_new_coder(strlen(m)*2);
    freq_compress(c,m);
    range_conclude(c);

    double percent=c->bits_used*1.0/(strlen(m)*8);
    if (percent<bestPercent) bestPercent=percent;
    if (percent>worstPercent) worstPercent=percent;
    runningPercent+=percent;

    lines++;

    printf("Total encoded length = L%.1f+B%.1f+C%.1f+A%.1f = %f bits = %.2f%% (best:avg:worst %.2f%%:%.2f%%:%.2f%%)\n",
	   c->bits_used,percent,bestPercent,runningPercent/lines,worstPercent);
    m[0]=0; fgets(m,1024,stdin);
  }
  return 0;
}
