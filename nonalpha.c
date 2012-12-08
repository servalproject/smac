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

#include <stdio.h>
#include <strings.h>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>

#include "arithmetic.h"
#include "charset.h"

int stripNonAlpha(unsigned short *in,int in_len,
		  unsigned short *out,int *out_len)
{
  int l=0;
  int i;
  for(i=0;i<in_len;i++)
    if (in[i]>0x80||charIdx(tolower(in[i]))>=0) out[l++]=in[i];
  *out_len=l;
  return 0;
}

unsigned int probNoNonAlpha=0.95*0xffffff;

int decodeNonAlpha(range_coder *c,int nonAlphaPositions[],
		   unsigned char nonAlphaValues[],int *nonAlphaCount,int messageLength)
{
  int i;
  int containsNonAlpha=range_decode_symbol(c,&probNoNonAlpha,2);
  if (containsNonAlpha) {    
    int count=range_decode_equiprobable(c,messageLength+1);
    
    // printf("Decoding %d non-alpha characters.\n",count);

    /* Decode the positions of special characters */
    ic_decode_recursive(nonAlphaPositions,count,messageLength,c);
    
    /* Decode the characters */
    for(i=0;i<count;i++) {
      nonAlphaValues[i]=range_decode_equiprobable(c,256); 
    }    

    // for(i=0;i<count;i++)
    //   printf("  nonalpha char #%d @ %d = 0x%02x\n",i,nonAlphaPositions[i],nonAlphaValues[i]);
    *nonAlphaCount=count;

    return 0;
  } else {
    *nonAlphaCount=0;
    return 0;
  }
}


int encodeNonAlpha(range_coder *c,unsigned short *m,int messageLength)
{
  /* Get positions and values of non-alpha chars.
     Encode count, then write the chars, then use interpolative encoding to
     encode their positions. */

  unsigned char v[1024];
  int pos[1024];
  int count=0;
  
  int i;
  for(i=0;i<messageLength;i++)
    /* (non-alpha characters can only be <0x80,
       since higher codepoints are encoded using unicode processing mechanisms) */
    if (m[i]<0x80) {
      if (charIdx(tolower(m[i]))>=0) {
	/* alpha or space -- so ignore */
      } else {
	/* non-alpha, so remember it */
	v[count]=m[i];
	// printf("non-alpha char: 0x%02x '%c' @ %d\n",m[i],m[i],i);
	pos[count++]=i;
      }  
    }

  // XXX - The following assumes that 50% of messages have special characters.
  // This is a patently silly assumption.
  if (!count) {
    // printf("Using 1 bit to indicate no non-alpha/space characters.\n");
    range_encode_symbol(c,&probNoNonAlpha,2,0);
    return 0;
  } else {
    // There are special characters present 
    range_encode_symbol(c,&probNoNonAlpha,2,1);
  }

  // printf("Using 8-bits to encode each of %d non-alpha chars.\n",count);

  /* Encode number of non-alpha chars */
  range_encode_equiprobable(c,messageLength+1,count);

  // printf("Using %f bits to encode the number of non-alpha/space chars.\n",countBits);

  /* Encode the positions of special characters */
  ic_encode_recursive(pos,count,messageLength,c);
  
  /* Encode the characters */
  for(i=0;i<count;i++) {
    range_encode_equiprobable(c,256,v[i]); 
  }

  // printf("Using interpolative coding for positions, total = %d bits.\n",posBits);

  return 0;
}
