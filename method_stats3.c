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
#include "charset.h"
#include "packed_stats.h"
#include "method_stats3.h"
#include "unicode.h"

int encodeLCAlphaSpace(range_coder *c,unsigned short *s,int len,stats_handle *h);
int encodeNonAlpha(range_coder *c,unsigned short *s,int len);
int stripNonAlpha(unsigned short *in,int in_len,
		  unsigned short *out,int *out_len);
int stripCase(unsigned short *in,int len,unsigned short *out);
int mungeCase(unsigned short *m,int len);
int encodeCaseModel1(range_coder *c,unsigned short *line,int len,stats_handle *h);

int decodeNonAlpha(range_coder *c,int nonAlphaPositions[],
		   unsigned char nonAlphaValues[],int *nonAlphaCount,
		   int messageLength);
int decodeCaseModel1(range_coder *c,unsigned short *line,int len,stats_handle *h);
int decodeLCAlphaSpace(range_coder *c,unsigned short *s,int length,stats_handle *h);
int decodePackedASCII(range_coder *c, unsigned char *m,int encodedLength);
int encodePackedASCII(range_coder *c,unsigned char *m);

unsigned int probPackedASCII=0.05*0xffffff;

int stats3_decompress_bits(range_coder *c,unsigned char m[1025],int *len_out,
			   stats_handle *h)
{
  int i;
  *len_out=0;

  /* Check if message is encoded naturally */
  int b7=range_decode_equiprobable(c,2);
  int b6=range_decode_equiprobable(c,2);
  int notRawASCII=0;
  if (b7&&(!b6)) notRawASCII=1;
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

  int encodedLength=range_decode_symbol(c,h->messagelengths,1024);
  for(i=0;i<encodedLength;i++) m[i]='?'; m[i]=0;
  *len_out=encodedLength;

  if (notPackedASCII==0) {
    /* packed ASCII -- copy from input to output */
    // printf("decoding packed ASCII\n");
    decodePackedASCII(c,m,encodedLength);
    return 0;
  }

  unsigned char nonAlphaValues[1024];
  int nonAlphaPositions[1024];
  int nonAlphaCount=0;

  decodeNonAlpha(c,nonAlphaPositions,nonAlphaValues,&nonAlphaCount,encodedLength);

  int alphaCount=(*len_out)-nonAlphaCount;

  // printf("message contains %d non-alpha characters, %d alpha chars.\n",nonAlphaCount,alphaCount);

  unsigned short lowerCaseAlphaChars[1025];

  decodeLCAlphaSpace(c,lowerCaseAlphaChars,alphaCount,h);

  decodeCaseModel1(c,lowerCaseAlphaChars,alphaCount,h);
  mungeCase(lowerCaseAlphaChars,alphaCount);
  
  /* reintegrate alpha and non-alpha characters */
  int nonAlphaPointer=0;
  int alphaPointer=0;
  unsigned short m16[1025];
  for(i=0;i<(*len_out);i++)
    {
      if (nonAlphaPointer<nonAlphaCount
	  &&nonAlphaPositions[nonAlphaPointer]==i) {
	m16[i]=nonAlphaValues[nonAlphaPointer++];
      } else {
	m16[i]=lowerCaseAlphaChars[alphaPointer++];
      }
    }
  m16[i]=0;
  utf16toutf8(m16,i,m,len_out);

  return 0;
}

int stats3_decompress(unsigned char *in,int inlen,unsigned char *out, int *outlen,
		      stats_handle *h)
{
  range_coder *c=range_new_coder(inlen);
  bcopy(in,c->bit_stream,inlen);

  c->bit_stream_length=inlen*8;
  c->bits_used=0;
  c->low=0;
  c->high=0xffffffff;
  range_decode_prefetch(c);

  if (stats3_decompress_bits(c,out,outlen,h)) {
    range_coder_free(c);
    return -1;
  }

  range_coder_free(c);
  return 0;
}

int stats3_compress_bits(range_coder *c,unsigned char *m_in,int m_in_len,
			 stats_handle *h)
{
  int len;
  unsigned short utf16[1024];

  unsigned short alpha[1024]; // message with all non alpha/spaces removed
  unsigned short lcalpha[1024]; // message with all alpha chars folded to lower-case

  // Convert UTF8 input string to UTF16 for handling
  if (utf8toutf16(m_in,m_in_len,utf16,&len)) return -1;

  /* Use model instead of just packed ASCII.
     We use 10x to indicate compressed message. This corresponds to a UTF8
     continuation byte, which is never allowed at the start of a string, and so
     we can use that disallowed state to indicate whether a message is compressed
     or not.
  */
  range_encode_equiprobable(c,2,1); 
  range_encode_equiprobable(c,2,0);
  range_encode_symbol(c,&probPackedASCII,2,1); // not packed ASCII

  // printf("%f bits to encode model\n",c->entropy);
  total_model_bits+=c->entropy;
  double lastEntropy=c->entropy;
  
  /* Encode length of message */
  range_encode_symbol(c,h->messagelengths,1024,len);
  
  // printf("%f bits to encode length\n",c->entropy-lastEntropy);
  total_length_bits+=c->entropy-lastEntropy;
  lastEntropy=c->entropy;

  /* encode any non-ASCII characters */
  encodeNonAlpha(c,utf16,len);

  int alpha_len=0;
  stripNonAlpha(utf16,len,alpha,&alpha_len);
  int nonAlphaChars=len-alpha_len;

  //  printf("%f bits (%d emitted) to encode non-alpha\n",c->entropy-lastEntropy,c->bits_used);
  total_nonalpha_bits+=c->entropy-lastEntropy;

  lastEntropy=c->entropy;

  /* compress lower-caseified version of message */
  stripCase(alpha,alpha_len,lcalpha);
  encodeLCAlphaSpace(c,lcalpha,alpha_len,h);

  // printf("%f bits (%d emitted) to encode chars\n",c->entropy-lastEntropy,c->bits_used);
  total_alpha_bits+=c->entropy-lastEntropy;

  lastEntropy=c->entropy;
  
  /* case must be encoded after symbols, so we know how many
     letters and where word breaks are.
 */
  mungeCase(alpha,alpha_len);
  encodeCaseModel1(c,alpha,alpha_len,h);

  //  printf("%f bits (%d emitted) to encode case\n",c->entropy-lastEntropy,c->bits_used);
  total_case_bits+=c->entropy-lastEntropy;

  range_conclude(c);
  // printf("%d bits actually used after concluding.\n",c->bits_used);
  total_finalisation_bits+=c->bits_used-c->entropy;

  if ((!nonAlphaChars)&&c->bits_used>=7*m_in_len)
    {
      /* Can we code it more efficiently without statistical modelling? */
      range_coder *c2=range_new_coder(1024);
      range_encode_equiprobable(c2,2,1); // not raw ASCII
      range_encode_equiprobable(c2,2,0); 
      range_encode_symbol(c2,&probPackedASCII,2,0); // is packed ASCII
      range_encode_symbol(c2,h->messagelengths,1024,m_in_len);
      int bad=encodePackedASCII(c2,m_in);
      range_conclude(c2);
      if ((!bad)&&c2->bits_used<c->bits_used) {
	range_coder_reset(c);
	range_encode_equiprobable(c,2,1); // not raw ASCII
	range_encode_equiprobable(c,2,0); 
	range_encode_symbol(c,&probPackedASCII,2,0); // is packed ASCII
	range_encode_symbol(c,h->messagelengths,1024,m_in_len);
	encodePackedASCII(c,m_in);
	range_conclude(c);
	// printf("Reverting to raw non-statistical encoding: %d chars in %d bits\n",
	//        (int)strlen((char *)m),c->bits_used);
      }
      range_coder_free(c2);
    }
  
  if (c->bits_used>=8*m_in_len)
    {
      /* we can't encode it more efficiently than 8-bit raw.
         We can only do this is MSB of first char of message is 0, as we use
	 the first bit of the message to indicate if it is compressed or not. */
      int i;
      range_coder_reset(c);
      for(i=0;i<m_in_len;i++) c->bit_stream[i]=m_in[i];
      c->bits_used=8*i;
      c->entropy=8*i;

      // printf("Reverting to raw 8-bit encoding: used %d bits\n",c->bits_used);
    }

  return 0;
}

int stats3_compress(unsigned char *in,int inlen,unsigned char *out, int *outlen,stats_handle *h)
{
  range_coder *c=range_new_coder(inlen*2);
  if (stats3_compress_bits(c,in,inlen,h)) {
    range_coder_free(c);
    return -1;
  }
  range_conclude(c);
  *outlen=c->bits_used>>3;
  if (c->bits_used&7) (*outlen)++;
  bcopy(c->bit_stream,out,*outlen);
  range_coder_free(c);
  return 0;
}
