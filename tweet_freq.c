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

extern unsigned int tweet_freqs3[69][69][69];
extern unsigned int tweet_freqs2[69][69];
extern unsigned int tweet_freqs1[69];
extern unsigned int caseend1[1][1];
extern unsigned int caseposn2[2][80][1];
extern unsigned int caseposn1[80][1];
unsigned int casestartofmessage[1][1];

unsigned char chars[69]="abcdefghijklmnopqrstuvwxyz 0123456789!@#$%^&*()_+-=~`[{]}\\|;:'\"<,>.?/";
int charIdx(unsigned char c)
{
  int i;
  for(i=0;i<69;i++)
    if (c==chars[i]) return i;
       
  /* Not valid character -- must be encoded separately */
  return -1;
}

int encodeLCAlphaSpace(range_coder *c,unsigned char *s)
{
  int b=c->bits_used;
  int c1=charIdx(' ');
  int c2=charIdx(' ');
  int o;
  for(o=0;s[o];o++) {
    int c3=charIdx(s[o]);
    range_encode_symbol(c,tweet_freqs3[c1][c2],69,c3);
    c1=c2; c2=c3;
  }
  if (1) printf("%d chars could be encoded in %d bits (%f per char)\n",
		(int)strlen((char *)s),c->bits_used-b,(c->bits_used-b)*1.0/strlen((char *)s));
  return 0;
}

int encodeLength(range_coder *c,unsigned char *m)
{
  int len=strlen((char *)m);
  range_encode_length(c,len);

  return 0;
}

int encodeNonAlpha(range_coder *c,unsigned char *m)
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
    range_encode_equiprobable(c,2,1);
    return 0;
  } else {
    // There are special characters present 
    range_encode_equiprobable(c,2,0);
  }

  printf("Using 8-bits to encode each of %d non-alpha chars.\n",count);

  /* Encode number of non-alpha chars using:
     n 1's to specify how many bits required to encode the count.
     Then 0.
     Then n bits to encode the count.
     So 2*ceil(log2(count))+1 bits
  */

  int len=strlen((char *)m);
  range_encode_length(c,len);  

  // printf("Using %f bits to encode the number of non-alpha/space chars.\n",countBits);

  /* Encode the positions of special characters */
  ic_encode_heiriter(pos,count,NULL,NULL,len,len,c);
  
  /* Encode the characters */
  for(i=0;i<count;i++) {
    range_encode_equiprobable(c,256,v[1]); 
  }

  // printf("Using interpolative coding for positions, total = %d bits.\n",posBits);

  return 0;
}

unsigned char wordChars[36]="abcdefghijklmnopqrstuvwxyz0123456789";
int charInWord(unsigned c)
{
  int i;
  int cc=tolower(c);
  for(i=0;wordChars[i];i++) if (cc==wordChars[i]) return 1;
  return 0;
}

double encodeCaseModel1(range_coder *c,unsigned char *line)
{
  int wordPosn=-1;
  int lastCase=0;

  int i;
  //  printf("caps eligble chars: ");
  for(i=0;line[i];i++) {
    int wordChar=charInWord(line[i]);
    if (!wordChar) {	  
      wordPosn=-1; lastCase=0;
    } else {
      if (isalpha(line[i])) {
	wordPosn++;
	int upper=0;
	int caseEnd=0;
	if (isupper(line[i])) upper=1; 
	/* note if end of word (which includes end of message,
	   implicitly detected here by finding null at end of string */
	if (!charInWord(line[i+1])) caseEnd=1;
	if (wordPosn==0) {
	  /* first letter of word, so can only use 1st-order model */
	  unsigned int frequencies[1]={caseposn1[0][0]};
	  if (i==0) frequencies[0]=casestartofmessage[0][0];
	  if (0) printf("case of first letter of word/message @ %d: p=%f\n",
			i,(frequencies[0]*1.0)/0x100000000);
	  range_encode_symbol(c,frequencies,2,upper);
	} else {
	  /* subsequent letter, so can use case of previous letter in model */
	  if (wordPosn>79) wordPosn=79;
	  if (0) printf("case of first letter of word/message @ %d.%d: p=%f\n",
			i,wordPosn,
			(caseposn2[lastCase][wordPosn][0]*1.0)/0x100000000);
	  range_encode_symbol(c,caseposn2[lastCase][wordPosn],2,upper);
	}
	if (isupper(line[i])) lastCase=1; else lastCase=0;
      }
    }
    
    /* fold all letters to lower case */
    if (line[i]>='A'&&line[i]<='Z') line[i]|=0x20;
  }
  //  printf("\n");

  return 0;
}

int stripNonAlpha(unsigned char *in,unsigned char *out)
{
  int l=0;
  int i;
  for(i=0;in[i];i++)
    if (charIdx(tolower(in[i]))>=0) out[l++]=in[i];
  out[l]=0;
  return 0;
}

int stripCase(unsigned char *in,unsigned char *out)
{
  int l=0;
  int i;
  for(i=0;in[i];i++) out[l++]=tolower(in[i]);
  out[l]=0;
  return 0;
}

int freq_compress(range_coder *c,unsigned char *m)
{
  unsigned char alpha[1024]; // message with all non alpha/spaces removed
  unsigned char lcalpha[1024]; // message with all alpha chars folded to lower-case

  /* Use model instead of just packed ASCII */
  range_encode_equiprobable(c,2,1); // not raw ASCII
  range_encode_equiprobable(c,2,1); // not packed ASCII

  printf("%f bits to encode model\n",c->entropy);
  double lastEntropy=c->entropy;
  
  /* Encode length of message */
  encodeLength(c,m);
  
  printf("%f bits to encode length\n",c->entropy-lastEntropy);
  lastEntropy=c->entropy;

  /* encode any non-ASCII characters */
  encodeNonAlpha(c,m);
  stripNonAlpha(m,alpha);

  printf("%f bits to encode non-alpha\n",c->entropy-lastEntropy);
  lastEntropy=c->entropy;

  /* compress lower-caseified version of message */
  stripCase(alpha,lcalpha);
  encodeLCAlphaSpace(c,lcalpha);

  printf("%f bits to encode chars\n",c->entropy-lastEntropy);
  lastEntropy=c->entropy;
  
  /* case must be encoded after symbols, so we know how many
     letters and where word breaks are */
  encodeCaseModel1(c,alpha);
  
  printf("%f bits to encode case\n",c->entropy-lastEntropy);

  range_conclude(c);
  printf("%d bits actually used after concluding.\n",c->bits_used);

  if (c->bits_used>=7*strlen((char *)m))
    {
      /* we can't encode it more efficiently than 7-bit ASCII */
      range_coder_reset(c);
      range_encode_equiprobable(c,2,1); // not raw ASCII
      range_encode_equiprobable(c,2,0); // is packed ASCII
      int i;
      for(i=0;m[i];i++) {
	int v=m[i];
	int upper=0;
	if (isalpha(v)&&isupper(v)) upper=1;
	v=tolower(v);
	v=charIdx(v);
	range_encode_equiprobable(c,69,v);
	if (isalpha(v))
	  range_encode_equiprobable(c,2,upper);
      }
      range_conclude(c);
      printf("Reverting to raw non-statistical encoding: %d chars in 2+%f bits\n",
	     (int)strlen((char *)m),c->entropy-2.0);
    }
  
  if ((c->bits_used>=8*strlen((char*)m))
      &&(!(m[0]&0x80)))
    {
      /* we can't encode it more efficiently than 8-bit raw.
         We can only do this is MSB of first char of message is 0, as we use
	 the first bit of the message to indicate if it is compressed or not. */
      int i;
      range_coder_reset(c);
      for(i=0;m[i];i++) c->bit_stream[i]=m[i];
      c->bits_used=8*i;
      c->entropy=8*i;
      printf("Reverting to raw 8-bit encoding: used %d bits\n",c->bits_used);
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
  
  FILE *f=fopen(argv[1],"r");
  if (!f) {
    fprintf(stderr,"Failed to open `%s' for input.\n",argv[1]);
    exit(-1);
  }

  m[0]=0; fgets(m,1024,f);
  
  int lines=0;
  double runningPercent=0;
  double worstPercent=0,bestPercent=100;

  while(m[0]) {    
    /* chop newline */
    m[strlen(m)-1]=0;
    if (1) printf(">>> %s\n",m);

    range_coder *c=range_new_coder(1024);
    freq_compress(c,(unsigned char *)m);

    double percent=c->bits_used*100.0/(strlen(m)*8);
    if (percent<bestPercent) bestPercent=percent;
    if (percent>worstPercent) worstPercent=percent;
    runningPercent+=percent;

    lines++;

    printf("Total encoded length = %d bits = %.2f%% (best:avg:worst %.2f%%:%.2f%%:%.2f%%)\n",
	   c->bits_used,percent,bestPercent,runningPercent/lines,worstPercent);
    m[0]=0; fgets(m,1024,f);
  }
  fclose(f);
  return 0;
}
