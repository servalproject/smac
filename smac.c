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
#include "smac.h"
#include "unicode.h"

int encodeLCAlphaSpace(range_coder *c,unsigned short *s,int len,stats_handle *h,
		       double *entropyLog);
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
int decodeLCAlphaSpace(range_coder *c,unsigned short *s,int length,stats_handle *h,
		       double *entropyLog);
int decodePackedASCII(range_coder *c, unsigned char *m,int encodedLength);
int encodePackedASCII(range_coder *c,unsigned char *m);

unsigned int probPackedASCII=0.05*0xffffff;

int stats3_decompress_bits(range_coder *c,unsigned char m[1025],int *len_out,
			   stats_handle *h,double *entropyLog)
{
  int i;
  *len_out=0;

  /* Check if message is encoded naturally */
  int b7=range_decode_equiprobable(c,2);
  int b6=range_decode_equiprobable(c,2);
  int notRawASCII=0;
  if (b7&&(!b6)) notRawASCII=1;
  if (notRawASCII==0) {
    /* raw bytes -- copy from input to output.
       But use range_decode_equiprobable() so that we can decode from non-byte
       boundaries.  We now include a null byte to terminate the string and make
       the decoding definitive.
    */
    
    // Reconstitute first byte
    unsigned char b5to0 = range_decode_equiprobable(c,64);
    b5to0|=(b6<<6);
    b5to0|=(b7<<7);
    m[0]=b5to0;

    // Also stop decoding if there are too many bytes (2K should be a reasonable
    // limit).
    for(i=1;m[i-1]&&(i<2048);i++) {
      int r=range_decode_equiprobable(c,256);
      // ... or if we hit the end of the compressed bit stream.
      if (r==-1) break;
      else m[i]=r;
      printf("Read byte 0x%02x\n",m[i]);
    }
    *len_out=i-1;
    return 0;
  }
  
  int notPackedASCII=range_decode_symbol(c,&probPackedASCII,2);

  int encodedLength=range_decode_symbol(c,(unsigned int *)h->messagelengths,1024);
  for(i=0;i<encodedLength;i++) m[i]='?'; m[i]=0;

  if (notPackedASCII==0) {
    /* packed ASCII -- copy from input to output */
    // printf("decoding packed ASCII\n");
    decodePackedASCII(c,m,encodedLength);
    *len_out=encodedLength;
    return 0;
  }

  unsigned char nonAlphaValues[1024];
  int nonAlphaPositions[1024];
  int nonAlphaCount=0;

  decodeNonAlpha(c,nonAlphaPositions,nonAlphaValues,&nonAlphaCount,encodedLength);

  int alphaCount=encodedLength-nonAlphaCount;

  // printf("message contains %d non-alpha characters, %d alpha chars.\n",nonAlphaCount,alphaCount);

  unsigned short lowerCaseAlphaChars[1025];

  decodeLCAlphaSpace(c,lowerCaseAlphaChars,alphaCount,h,entropyLog);

  decodeCaseModel1(c,lowerCaseAlphaChars,alphaCount,h);
  mungeCase(lowerCaseAlphaChars,alphaCount);
  
  /* reintegrate alpha and non-alpha characters */
  int nonAlphaPointer=0;
  int alphaPointer=0;
  unsigned short m16[1025];
  for(i=0;i<(alphaCount+nonAlphaCount);i++)
    {
      if (nonAlphaPointer<nonAlphaCount
	  &&nonAlphaPositions[nonAlphaPointer]==i) {
	m16[i]=nonAlphaValues[nonAlphaPointer++];
      } else {
	m16[i]=lowerCaseAlphaChars[alphaPointer++];
      }
    }
  utf16toutf8(m16,i,m,len_out);
  m[*len_out]=0;
  //  fprintf(stderr,"m='%s', len=%d\n",m,*len_out);

  return 0;
}

int stats3_decompress(unsigned char *in,int inlen,unsigned char *out, int *outlen,
		      stats_handle *h)
{
  range_coder *c=range_new_coder(inlen*2);
  bcopy(in,c->bit_stream,inlen);

  c->bit_stream_length=inlen*8;
  c->bits_used=0;
  c->low=0;
  c->high=0xffffffff;
  range_decode_prefetch(c);

  if (stats3_decompress_bits(c,out,outlen,h,NULL)) {
    range_coder_free(c);
    return -1;
  }

  range_coder_free(c);
  return 0;
}

int stats3_compress_radix_append(range_coder *c,unsigned char *m_in,int m_in_len,
				 stats_handle *h,double *entropyLog)
{
  range_encode_equiprobable(c,2,1); // not raw ASCII
  range_encode_equiprobable(c,2,0); 
  range_encode_symbol(c,&probPackedASCII,2,0); // is packed ASCII
  range_encode_symbol(c,(unsigned int *)h->messagelengths,1024,m_in_len);
  return encodePackedASCII(c,m_in);       
}

int stats3_compress_model1_append(range_coder *c,unsigned char *m_in,int m_in_len,
				  stats_handle *h,double *entropyLog)
{
  int len;
  unsigned short utf16[1024];

  unsigned short alpha[1024]; // message with all non alpha/spaces removed
  unsigned short lcalpha[1024]; // message with all alpha chars folded to lower-case

  // Convert UTF8 input string to UTF16 for handling
  if (utf8toutf16(m_in,m_in_len,utf16,&len)) return -1;

  /* Use model instead of just packed ASCII.
     We use %10x as the first three bits to indicate compressed message. 
     This corresponds to a UTF8 continuation byte, which is never allowed at 
     the start of a string, and so we can use that disallowed state to
     indicate whether a message is compressed or not.
  */
  range_encode_equiprobable(c,2,1); 
  range_encode_equiprobable(c,2,0);
  range_encode_symbol(c,&probPackedASCII,2,1); // not packed ASCII

  // printf("%f bits to encode model\n",c->entropy);
  total_model_bits+=c->entropy;
  double lastEntropy=c->entropy;
  
  /* Encode length of message */
  range_encode_symbol(c,(unsigned int *)h->messagelengths,1024,len);
  
  // printf("%f bits to encode length\n",c->entropy-lastEntropy);
  total_length_bits+=c->entropy-lastEntropy;
  lastEntropy=c->entropy;

  /* encode any non-ASCII characters */
  encodeNonAlpha(c,utf16,len);

  int alpha_len=0;
  stripNonAlpha(utf16,len,alpha,&alpha_len);

  //  printf("%f bits (%d emitted) to encode non-alpha\n",c->entropy-lastEntropy,c->bits_used);
  total_nonalpha_bits+=c->entropy-lastEntropy;

  lastEntropy=c->entropy;

  /* compress lower-caseified version of message */
  stripCase(alpha,alpha_len,lcalpha);
  encodeLCAlphaSpace(c,lcalpha,alpha_len,h,entropyLog);

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

  return 0;
}

int stats3_compress_uncompressed_append(range_coder *c,unsigned char *m_in,int m_in_len,
					stats_handle *h,double *entropyLog)
{
  // Encode bit by bit in case range coder is not on a byte boundary.
  int i;
  for(i=0;i<m_in_len;i++)
    range_encode_equiprobable(c,256,m_in[i]);
    
  // Add $FF byte to terminate
  range_encode_equiprobable(c,256,255);

  return 0;
}

int stats3_compress_append(range_coder *c,unsigned char *m_in,int m_in_len,
			   stats_handle *h,double *entropyLog)
{
  int b1,b2,b3;

  /* Try the three sub-models to see which performs best. */

  // Variable depth model
  range_coder *t1=range_new_coder(1024);
  stats3_compress_model1_append(t1,m_in,m_in_len,h,entropyLog);
  range_conclude(t1); b1=t1->bits_used; range_coder_free(t1);

  // Packed ascii (only if there are no non-ascii chars)
  range_coder *t2=range_new_coder(1024);
  if (stats3_compress_radix_append(t2,m_in,m_in_len,h,entropyLog)) 
    b2=999999;
  else { range_conclude(t2); b2=t2->bits_used; }
  range_coder_free(t2);

  // Unpacked (only if the first character <= 127)
  b3=(m_in_len+1)*8; // one extra character for null termination
  b3=999999;

  // Compare the results and encode accordingly
  if (b1<b2&&b1<b3)
    return stats3_compress_model1_append(c,m_in,m_in_len,h,entropyLog);
  else if (b2<b3||(m_in[0]&0x80))
    return stats3_compress_radix_append(c,m_in,m_in_len,h,entropyLog);
  else
    return stats3_compress_uncompressed_append(c,m_in,m_in_len,h,entropyLog);
}


int stats3_compress_bits(range_coder *c,unsigned char *m_in,int m_in_len,
			 stats_handle *h,double *entropyLog)
{
  if (stats3_compress_append(c,m_in,m_in_len,h,entropyLog)) return -1;
  range_conclude(c);
  // printf("%d bits actually used after concluding.\n",c->bits_used);
  total_finalisation_bits+=c->bits_used-c->entropy;

  return 0;
}

int stats3_compress(unsigned char *in,int inlen,unsigned char *out, int *outlen,stats_handle *h)
{
  range_coder *c=range_new_coder(inlen*2);
  if (stats3_compress_bits(c,in,inlen,h,NULL)) {
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
