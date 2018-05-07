/*
Copyright (C) 2005,2012 Paul Gardner-Stephen
 
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
  Interpolative Coder/Decoder.
  Paul Gardner-Stephen, sometime around 2005.

  Implements the traditional recursive algorithm, and the recursive turned stack base version.
  In addition, it implements my new fast interpolative coder which all but does away with recursion.

  In all cases, the assignment of binary codes follows the same rules as indicated in the 
  2000 paper on interpolative codes.  
  Similarly, the divisions are placed on powers of two where possible to avoid unbalanced
  traversals.

  The ic_encode_*() routines encode a simple list which has list_length entries,
  and a maximum possible value of max_value.  Word frequencies and positions 
  within d document are handled by passing an optional pointer to an array of
  cumulative frequencies and the position of each hit within its host document.
*/

#ifndef COMMON_DEFS
#define COMMON_DEFS

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include "arithmetic.h"

inline void encode_bits(int code_bits,unsigned int value,range_coder *c);
inline void decode_bits(int code_bits,unsigned int *value,range_coder *c);
inline void decode_few_bits(int code_bits,unsigned int *value,range_coder *c);

int biggest_power_of_2(int v)
{
  int b;
  for(b=30;b>=0;b--)
    if ((1<<b)<v) return 1<<b;
  return 0;
}

int ic_encode_recursive(int *list,
			int list_length,
			int max_value,
			range_coder *c);

void binary_encode(int low,int *pp,int high,range_coder *c)
{
  /* Work out the range of values we can encode/decode */
  int p=*pp;
  int range_minus_1=high-low;
  int range=range_minus_1+1;
  int value=p-low;

  range_encode_equiprobable(c,range,value);
  
  return;
}

void binary_decode(int low,int *pp,int high,range_coder *c)
{

  /* Work out the range of values we can encode/decode */
  int range_minus_1=high-low;
  int range=range_minus_1+1;
  int value;

  value=range_decode_equiprobable(c,range);
  *pp=value+low;

  
  return;
}

#endif


/* Parse the file twice, once for encoding and common functions, and once more
	for decoding functions.  This allows us to keep the code base very small, and less
	prone to cut-and-paste errors */
#ifndef UNDER_CONTROL
#define UNDER_CONTROL
#undef ENCODE
#undef ENCODING
#define ENCODE(X,Y) X ## decode ## Y
#include "gsinterpolative.c"
#undef ENCODE
#define ENCODING
#define ENCODE(X,Y) X ## encode ## Y
#endif


/*      -------------------------------------------------
	Recursive Implementation
	------------------------------------------------- */
 
int ENCODE(ic_,_recursive_r)(int *list,
			     int list_length,
			     int max_value,
			     range_coder *c,
			     int lo,
			     int p,
			     int hi,
			     int step)
{
  int doc_number;
  int doc_low;
  int doc_high;

  if (lo>=list_length) return 0;

  /* Skip coding of entries outside the valid range, since the pseudo entries do not require
     coding as their values are known */
  if (p<list_length)
    {
      doc_number=list[p];
      
      /* Work out initial constraint on this value */
      if (lo>-1) doc_low=list[lo]+1; else doc_low=0;
      if (hi<list_length) doc_high=list[hi]-1; 
      else doc_high=max_value+(hi-list_length);
      
      /* Now narrow range to take into account the number of postings which occur between us and lo and hi */
      doc_low+=(p-lo-1); 
      doc_high-=(hi-p-1);
      
      /* Encode the document number */
      if (p<list_length)
	{
	  ENCODE(binary_,)(doc_low,&list[p],doc_high,c);
	  //	  printf("%d [%d,%d,%d] step=%d\n",p,doc_low,list[p],doc_high,step);
	}
    }
  
  if (step>1)
    {
      /* Now recurse left and right children */
      ENCODE(ic_,_recursive_r)(list,list_length,			       
			       max_value,
			       c,
			       lo,p-(step>>1),p,
			       step>>1);  
      
      ENCODE(ic_,_recursive_r)(list,list_length,
			       max_value,
			       c,
			       p,p+(step>>1),hi,
			       step>>1);  
    }
  
  return 0;
}


int ENCODE(ic_,_recursive)(int *list,
			   int list_length,
			   int max_value,
			   range_coder *c)
{
  /* Start at largest power of two, and round list size up to next power of two
     to keep recursion simple, and keep bit stream compatible with fast
     interpolative coder. */
  int powerof2=biggest_power_of_2(list_length+1);
  
  ENCODE(ic_,_recursive_r)(list,list_length,
			   max_value,
			   c,
			   -1,powerof2-1,list_length+1,powerof2); 
  
  return 0;
}

