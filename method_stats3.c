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
#include "method_stats3.h"

int encodeLCAlphaSpace(range_coder *c,unsigned char *s);
int encodeNonAlpha(range_coder *c,unsigned char *s);
int encodeLength(range_coder *c,int length);
int decodeLength(range_coder *c);
int stripNonAlpha(unsigned char *in,unsigned char *out);
int stripCase(unsigned char *in,unsigned char *out);
int mungeCase(char *m);
int encodeCaseModel1(range_coder *c,unsigned char *line);

int decodeNonAlpha(range_coder *c,int nonAlphaPositions[],
		   unsigned char nonAlphaValues[],int *nonAlphaCount,int messageLength);
int decodeCaseModel1(range_coder *c,unsigned char *line);
int decodeLCAlphaSpace(range_coder *c,unsigned char *s,int length);
int decodePackedASCII(range_coder *c, char *m,int encodedLength);
int encodePackedASCII(range_coder *c,char *m);

unsigned int probPackedASCII=0.95*0xffffff;

int stats3_decompress(range_coder *c,unsigned char m[1025],int *len_out)
{
  int i;
  *len_out=0;

  /* Check if message is encoded naturally */
  int notRawASCII=range_decode_equiprobable(c,2);
  if (notRawASCII==0) {
    /* raw bytes -- copy from input to output */
    // printf("decoding raw bytes: bits_used=%d\n",c->bits_used);
    for(i=0;c->bit_stream[i]&&i<1024&&i<(c->bit_stream_length>>3);i++) {
      m[i]=c->bit_stream[i];
      // printf("%d 0x%02x\n",i,c->bit_stream[i]);
    }
    // printf("%d 0x%02x\n",i,c->bit_stream[i]);
    m[i]=0;
    *len_out=i;
    return 0;
  }
  
  int notPackedASCII=range_decode_symbol(c,&probPackedASCII,2);

  int encodedLength=decodeLength(c);
  for(i=0;i<encodedLength;i++) m[i]='?'; m[i]=0;
  *len_out=encodedLength;

  if (notPackedASCII==0) {
    /* packed ASCII -- copy from input to output */
    // printf("decoding packed ASCII\n");
    decodePackedASCII(c,(char *)m,encodedLength);
    return 0;
  }

  unsigned char nonAlphaValues[1024];
  int nonAlphaPositions[1024];
  int nonAlphaCount=0;

  decodeNonAlpha(c,nonAlphaPositions,nonAlphaValues,&nonAlphaCount,encodedLength);

  int alphaCount=(*len_out)-nonAlphaCount;

  // printf("message contains %d non-alpha characters, %d alpha chars.\n",nonAlphaCount,alphaCount);

  unsigned char lowerCaseAlphaChars[1025];

  decodeLCAlphaSpace(c,lowerCaseAlphaChars,alphaCount);
  lowerCaseAlphaChars[alphaCount]=0;

  decodeCaseModel1(c,lowerCaseAlphaChars);
  mungeCase((char *)lowerCaseAlphaChars);
  
  /* reintegrate alpha and non-alpha characters */
  int nonAlphaPointer=0;
  int alphaPointer=0;
  for(i=0;i<(*len_out);i++)
    {
      if (nonAlphaPointer<nonAlphaCount
	  &&nonAlphaPositions[nonAlphaPointer]==i) {
	m[i]=nonAlphaValues[nonAlphaPointer++];
      } else {
	m[i]=lowerCaseAlphaChars[alphaPointer++];
      }
    }
  m[i]=0;

  return 0;
}

int stats3_compress(range_coder *c,unsigned char *m)
{
  unsigned char alpha[1024]; // message with all non alpha/spaces removed
  unsigned char lcalpha[1024]; // message with all alpha chars folded to lower-case

  /* Use model instead of just packed ASCII */
  range_encode_equiprobable(c,2,1); // not raw ASCII
  range_encode_symbol(c,&probPackedASCII,2,1); // not packed ASCII

  // printf("%f bits to encode model\n",c->entropy);
  total_model_bits+=c->entropy;
  double lastEntropy=c->entropy;
  
  /* Encode length of message */
  encodeLength(c,strlen((char *)m));
  
  // printf("%f bits to encode length\n",c->entropy-lastEntropy);
  total_length_bits+=c->entropy-lastEntropy;
  lastEntropy=c->entropy;

  /* encode any non-ASCII characters */
  encodeNonAlpha(c,m);
  stripNonAlpha(m,alpha);
  int nonAlphaChars=strlen(m)-strlen(alpha);

  //  printf("%f bits (%d emitted) to encode non-alpha\n",c->entropy-lastEntropy,c->bits_used);
  total_nonalpha_bits+=c->entropy-lastEntropy;

  lastEntropy=c->entropy;

  /* compress lower-caseified version of message */
  stripCase(alpha,lcalpha);
  encodeLCAlphaSpace(c,lcalpha);

  // printf("%f bits (%d emitted) to encode chars\n",c->entropy-lastEntropy,c->bits_used);
  total_alpha_bits+=c->entropy-lastEntropy;

  lastEntropy=c->entropy;
  
  /* case must be encoded after symbols, so we know how many
     letters and where word breaks are.
 */
  mungeCase((char *)alpha);
  encodeCaseModel1(c,alpha);
  
  //  printf("%f bits (%d emitted) to encode case\n",c->entropy-lastEntropy,c->bits_used);
  total_case_bits+=c->entropy-lastEntropy;

  range_conclude(c);
  // printf("%d bits actually used after concluding.\n",c->bits_used);
  total_finalisation_bits+=c->bits_used-c->entropy;

  if ((!nonAlphaChars)&&c->bits_used>=7*strlen((char *)m))
    {
      /* Can we code it more efficiently without statistical modelling? */
      range_coder *c2=range_new_coder(1024);
      range_encode_equiprobable(c2,2,1); // not raw ASCII
      range_encode_symbol(c2,&probPackedASCII,2,0); // is packed ASCII
      encodeLength(c2,strlen((char *)m));
      encodePackedASCII(c2,(char *)m);
      range_conclude(c2);
      if (c2->bits_used<c->bits_used) {
	range_coder_reset(c);
	range_encode_equiprobable(c,2,1); // not raw ASCII
	range_encode_symbol(c,&probPackedASCII,2,0); // is packed ASCII
	encodeLength(c,strlen((char *)m));
	encodePackedASCII(c,(char *)m);
	range_conclude(c);
	// printf("Reverting to raw non-statistical encoding: %d chars in %d bits\n",
	//        (int)strlen((char *)m),c->bits_used);
      }
      range_coder_free(c2);
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

      // printf("Reverting to raw 8-bit encoding: used %d bits\n",c->bits_used);
    }

  return 0;
}


