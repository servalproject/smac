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
#include "message_stats.h"
#include "charset.h"

int encodeLCAlphaSpace(range_coder *c,unsigned char *s);
int encodeLength(range_coder *c,int length);
int decodeLength(range_coder *c);

int freq_compress(range_coder *c,unsigned char *m)
{
  unsigned char alpha[1024]; // message with all non alpha/spaces removed
  unsigned char lcalpha[1024]; // message with all alpha chars folded to lower-case

  unsigned int probPackedASCII=0.95*0xffffffff;

  /* Use model instead of just packed ASCII */
  range_encode_equiprobable(c,2,1); // not raw ASCII
  range_encode_symbol(c,&probPackedASCII,2,0); // not packed ASCII

  printf("%f bits to encode model\n",c->entropy);
  double lastEntropy=c->entropy;
  
  /* Encode length of message */
  encodeLength(c,strlen((char *)m));
  
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
     letters and where word breaks are.
 */
  mungeCase((char *)alpha);
  encodeCaseModel1(c,alpha);
  
  printf("%f bits to encode case\n",c->entropy-lastEntropy);

  range_conclude(c);
  printf("%d bits actually used after concluding.\n",c->bits_used);

  if (c->bits_used>=7*strlen((char *)m))
    {
      /* we can't encode it more efficiently than 7-bit ASCII */
      range_coder_reset(c);
      range_encode_equiprobable(c,2,1); // not raw ASCII
      range_encode_symbol(c,&probPackedASCII,2,1); // is packed ASCII
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

  int i;
  for(i=1;i<wordCount-1;i++)
    if (wordFrequencies[i]<=wordFrequencies[i-1]) {
      fprintf(stderr,"poot: frequency of %s (#%d) = 0x%x, but %s (#%d) = 0x%x\n",
	      wordList[i-1],i-1,wordFrequencies[i-1],
	      wordList[i],i,wordFrequencies[i]);
      exit(-1);
    }

  char m[1024]; // raw message, no pre-processing
  
  FILE *f;

  if (strcmp(argv[1],"-")) f=fopen(argv[1],"r"); else f=stdin;
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
