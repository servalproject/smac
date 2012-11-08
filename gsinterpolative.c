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

/* Parse the file twice, once for encoding and common functions, and once more
	for decoding functions.  This allows us to keep the code base very small, and less
	prone to cut-and-paste errors */
#ifndef UNDER_CONTROL
#define UNDER_CONTROL
#define COMMON
#undef ENCODE
#undef ENCODING
#define ENCODE(X,Y) X ## decode ## Y
#include "gsinterpolative.c"
#undef COMMON
#undef ENCODE
#define ENCODING
#define ENCODE(X,Y) X ## encode ## Y
#endif

#ifdef COMMON

#undef DEBUG

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include "arithmetic.h"

inline void encode_bits(int code_bits,unsigned int value,range_coder *c);
inline void decode_bits(int code_bits,unsigned int *value,range_coder *c);
inline void decode_few_bits(int code_bits,unsigned int *value,range_coder *c);

int ic_encode_recursive(int *list,
			int list_length,
			int *frequencies,
			int *word_positions,
			int corpus_document_count,
			int max_document_words,
			range_coder *c);
int ic_encode_heiriter(int *list,
		       int list_length,
		       int *frequencies,
		       int *word_positions,
		       int corpus_document_count,
		       int max_document_words,
		       range_coder *c);


int mask[33]={0,1,3,7,15,31,63,127,255,
				  0x1ff,0x3ff,0x7ff,0xfff,
				  0x1fff,0x3fff,0x7fff,0xffff,
				  0x1ffff,0x3ffff,0x7ffff,0xfffff,
				  0x1fffff,0x3fffff,0x7fffff,0xffffff,
				  0x1ffffff,0x3ffffff,0x7ffffff,0xfffffff,
				  0x1fffffff,0x3fffffff,0x7fffffff,0xffffffff};
int msb[256]={0,1,2,2,3,3,3,3,4,4,4,4,4,4,4,4,
				  5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
				  6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
				  6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
				  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
				  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
				  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
				  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
				  8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
				  8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
				  8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
				  8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
				  8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
				  8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
				  8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
				  8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8};

#define GD_BITS 8
#define GD_COUNT 256
int gap_dist_i[GD_COUNT]={0};
int gap_dist_l[GD_COUNT]={0};

/* XXX - This should use a table look up, and become a macro too, for that matter  */
unsigned int biggest_power_of_2(unsigned int v)
{
  unsigned int b;
  unsigned int powof2;

  if (!v) return 0;
  
  b=msb[v&0xff];
  if (v&0xff00) b=msb[(v>>8)&0xff]+8;
  if (v&0xff0000) b=msb[(v>>16)&0xff]+16;
  if (v&0xff000000) b=msb[v>>24]+24;
  powof2=1<<b;
  if (v>powof2) powof2=powof2<<1;
  while(powof2>v) powof2=powof2>>1;
  
  return powof2;
}

unsigned int log2_ceil(unsigned int v)
{
  unsigned int b;
  unsigned int powof2;

  if (!v) return 0;
  
  b=msb[v&0xff];
  if (v&0xff00) b=msb[(v>>8)&0xff]+8;
  if (v&0xff0000) b=msb[(v>>16)&0xff]+16;
  if (v&0xff000000) b=msb[v>>24]+24;

  powof2=1<<b;

  if (v>powof2) { b++; powof2=powof2<<1; }
  
  return b;
}

#endif /* COMMON */

inline void ENCODE(binary_,)(int low,int *pp,int high,int step,range_coder *c)
{
#ifdef ENCODING
  int p=*pp;
#else
  int p;
#endif

  /* Work out the range of values we can encode/decode */
  int range_minus_1=high-low;
  int range=range_minus_1+1;
  int code_bits;
  int code_range;
  int short_codes;
#ifdef ENCODING
  int value=p-low;
#else
  int value;
#endif
  int range_count;
  int code=-1;
  int i;

  struct range {
	 int low,high,first_code,short_code;
  };

  struct range ranges[3];

#ifdef DEBUG
#ifdef ENCODING
  if (low>high||(p>high)||(p<low))
	 {
		printf("Illegal triple encountered: [%d,%d,%d]\n",
				 low,p,high);
		sleep(60);
	 }
#endif
#endif

  /* Encode zero bits if the range is zero  */
  if (high==low) 
	 {
#ifndef ENCODING
		p=low; *pp=low;
#endif
#ifdef DEBUG
		if ((p>high||p<low)||(DEBUG&4))
		  {
			 printf("[%d,%d,%d]   -no code required-\n",low,p,high);
			 if (DEBUG&8) sleep(1);
		  }
#endif
		return;
	 }

#ifdef ENCODING
  /* Gather gap statistics for investigation */
  if (range) {
    if (step>1) gap_dist_l[(value<<GD_BITS)/range]++;
    else gap_dist_i[(value<<GD_BITS)/range]++;
  }
#endif
  
  /* Otherwise, figure out how many bits we need */
  code_bits=msb[range_minus_1&0xff];
  if (range_minus_1>>8) code_bits=msb[(range_minus_1>>8)&0xff]+8;
  if (range_minus_1>>16) code_bits=msb[(range_minus_1>>16)&0xff]+16;
  if (range_minus_1>>24) code_bits=msb[range_minus_1>>24]+24;
  code_range=1<<code_bits;
  if (range>code_range) code_range=code_range<<1;

  /* Work out the number of codes which can be one bit shorter */
  short_codes=(code_range-range);
  
  /* Work out the code itself.
	Note that the code numbers must be allocated in strictly ascending
	order so that the decoder can stop as soon as it finds a match.
         The values to be encoded/decoded on the other hand are allowed to
         be out of order so that this can be achieved.  This way the extra
         search time is spend during encoding rather than decoding. */
  if (step>1)
    {
      if (short_codes)
	{
	  int drs=(range-short_codes)>>1;
	  
	  /* Place short codes in middle of range */
	  ranges[1].low=0; 
	  ranges[1].high=drs-1;
	  ranges[1].first_code=short_codes<<1;
	  ranges[1].short_code=0;
	  
	  ranges[0].low=ranges[1].high+1;
	  ranges[0].high=ranges[0].low+short_codes-1;
	  ranges[0].first_code=0;
	  ranges[0].short_code=1;
	  
	  ranges[2].low=ranges[0].high+1;
	  ranges[2].high=range_minus_1;
	  ranges[2].first_code=code_range-drs;
	  ranges[2].short_code=0;
	  
	  range_count=3;
	}
      else
	{ 
	  /* No short codes, so use flat space */
	  ranges[0].low=0;
	  ranges[0].high=range_minus_1;
	  ranges[0].first_code=0;
	  ranges[0].short_code=0;
	  
	  range_count=1;
	}
    }
  else
    {
      /* Place short codes at outside of range */
      if (short_codes&1) short_codes--;
      
      if (short_codes)
	{
	  /* Place short codes at outside of range */
	  ranges[0].low=0;
	  ranges[0].high=(short_codes>>1)-1;
	  ranges[0].first_code=0;
	  ranges[0].short_code=1;
	  
	  ranges[2].low=(short_codes>>1);
	  ranges[2].high=range-(short_codes>>1)-1;
	  ranges[2].first_code=short_codes<<1;
	  ranges[2].short_code=0;
	  
	  ranges[1].low=range-(short_codes>>1);
	  ranges[1].high=range_minus_1;
	  ranges[1].first_code=short_codes;
	  ranges[1].short_code=1;
	  
	  range_count=3;
	}
      else
	{ 
	  /* No short codes, so use flat space */
	  ranges[0].low=0;
	  ranges[0].high=range_minus_1;
	  ranges[0].first_code=0;
	  ranges[0].short_code=0;
	  
	  range_count=1;
	}
      
    }
  
#ifdef DEBUG
  if (DEBUG&2)
    for(i=0;i<range_count;i++)
      printf("Range %d: values=[%d,%d], first code=0x%x, short code? %s\n",
	     i,ranges[i].low,ranges[i].high,ranges[i].first_code,
	     ranges[i].short_code ? "Yes" : "No");
#endif
  
#ifdef ENCODING
  /* Pick the code for the value */
  for(i=0;i<range_count;i++)
    {
      if ((value>=ranges[i].low)&&(value<=ranges[i].high))
	{
	  code=ranges[i].first_code
	    +((value-ranges[i].low)<<ranges[i].short_code);
	  if (ranges[i].short_code)
	    {
	      code=code>>1;
	      code_bits-=1;
	    }
	  break;
	}
    }
#else
  code=0;
#endif

  /* Read  code */
#ifndef ENCODING
  for(i=code_bits-1;i>=0;i++)
    code|=range_decode_getnextbit(c) << i;
#endif		  

#ifndef ENCODING
  /* Work out which range the code came from.
     If there is only one range, then this for loop short circuits and 
     gives the correct answer.  Otherwise one or more iterations are
     required.  The final range never needs to be checked, because if
     the code is not in one of the previous ranges, it must be in the
     last one.  This saves a little bit of decoding time */
  if (range_count==1) i=0; else
    for(i=0;i<(range_count-1);i++)
      if (code<ranges[i+1].first_code)
	break;
  value=code-ranges[i].first_code;
  if (ranges[i].short_code) 
    {
      /* The code is a minimum length code, so we must trim one bit. */
      value=value>>1; code_bits--;
#ifdef DEBUG
      code=code>>1; code_bits--;
#endif
    }
  /* The final value can now be calculated */
  *pp=low+ranges[i].low+value;
#endif

#ifdef ENCODING
  for(i=code_bits-1;i>=0;i++)
    range_emitbit(c,(code >> i ) & 1 );
#endif
  
#ifdef DEBUG
#ifndef ENCODING
  p=*pp;
#endif
  if ((p>high||p<low)||(DEBUG&4))
    {
      int b;
      printf("[%d,%d,%d] [",low,p,high);
      for(b=code_bits-1;b>=0;b--)
	printf("%d",(code>>b)&1);
      printf("]  %d bits processed, step=%d\n",*out_bits,step);
      if (DEBUG&8) sleep(1);
    }
#endif
  
  return;
}

#ifdef COMMON

inline void binary_many_encode(int low,int p,int high,int leafP,range_coder *c)
{
  /* Work out the range of values we can encode/decode */
  int range_minus_1=high-low;
  int range=range_minus_1+1;
  int code_bits;
  int code_range;
  int short_codes,long_codes;
  int value=p-low;
  unsigned int code;

#ifdef DEBUG
  if (low>high||(p>high)||(p<low))
    {
      printf("menc Illegal triple encountered: [%d,%d,%d]\n",
	     low,p,high);
      sleep(60);
    }
#endif
  
  /* Encode zero bits if the range is zero  */
  if (high==low) 
    {
#ifdef DEBUG
      if ((p>high||p<low)||(DEBUG&4))
	{
	  printf("menc [%d,%d,%d]   -no code required-\n",low,p,high);
	  if (DEBUG&8) sleep(1);
	}
#endif
      return;
    }
  
  /* Figure out how many bits we need */
  if (range_minus_1>>24) code_bits=msb[range_minus_1>>24]+24;
  else if (range_minus_1>>16) code_bits=msb[(range_minus_1>>16)&0xff]+16;
  else if (range_minus_1>>8) code_bits=msb[(range_minus_1>>8)&0xff]+8;
  else code_bits=msb[range_minus_1&0xff];

  /* Compute size of range */
  code_range=1<<code_bits;
  if (range>code_range) code_range=code_range<<1;

  /* Work out the number of codes which can be one bit shorter */
  short_codes=code_range-range;
  long_codes=range-short_codes;

  if (!short_codes)
    {
      /* Write simple fixed length code and return */
      encode_bits(code_bits,value,c);
#ifdef DEBUG
      code=value;
#endif
    }
  else
    {
      int sc_on_2=short_codes>>1;
      int shift;
      int add; 
      
      /* Transform 3 segment space into two, with short codes at the bottom  */
      if (!leafP) shift=-(long_codes>>1); /* move short codes from centre */
      else shift=sc_on_2;
#ifdef DEBUG
      printf("menc value=0x%x, add=%d, shift=%d, sc/2=%d, sc=%d, range=%d, code_range=%d\n",
	     value,add,shift,sc_on_2,short_codes,range,code_range);
#endif
      value+=shift; 
      if (value<0) value+=range;
      if (value>=range) value-=range;
#ifdef DEBUG
      printf("menc after reassigning value=0x%x\n",value);
#endif
      add=value;
      if (add>short_codes) add=short_codes; if (add<0) add=0;
      
      code=value+add; /* expand value to fit short codes */
#ifdef DEBUG
      printf("menc after inserting short codes, code=0x%x\n",code);
#endif
      /* Work out final number of code bits */
      if (code<(short_codes<<1))
	{
#ifdef DEBUG
	  printf("menc trimming short code\n");
#endif
	  code_bits--;
	}
      
      /* Rotate LSB around to become MSB */
      code=(code>>1)+((code&1)<<(code_bits-1));
#ifdef DEBUG
      printf("menc after rotation code=0x%x\n",code);
#endif
      /* Encode bits */
      encode_bits(code_bits,code,c);
    }
  
#ifdef DEBUG
  if ((p>high||p<low)||(DEBUG&4))
    {
      int b;
      printf("menc [%d,%d,%d] [",low,p,high);
      for(b=code_bits-1;b>=0;b--)
	printf("%d",(code>>b)&1);
      printf("]  %d bits processed\n",*out_bits);
      if (DEBUG&8) sleep(1);
    }
#endif
  
  return;
}

inline void binary_few_decode(int low,int *pp,int high,int leafP,range_coder *c)
{
#ifdef ENCODING
  int p=*pp;
#else
  int p;
#endif

  /* Work out the range of values we can encode/decode */
  int range_minus_1=high-low;
  int range=range_minus_1+1;
  int code_bits;
  int code_range;
  int short_codes,long_codes;
  int value;
  unsigned int code;

  /* Encode zero bits if the range is zero  */
  if (high==low) 
    {
      p=low; *pp=low;
#ifdef DEBUG
      if ((p>high||p<low)||(DEBUG&4))
	{
	  printf("fdec [%d,%d,%d]   -no code required-\n",low,p,high);
	  if (DEBUG&8) sleep(1);
	}
#endif
      return;
    }
  
  /* Otherwise, figure out how many bits we need */
  code_bits=msb[range_minus_1&0xff];
  if (range_minus_1&0xffffff00) code_bits=msb[(range_minus_1>>8)&0xff]+8;
  if (range_minus_1&0xffff0000) code_bits=msb[(range_minus_1>>16)&0xff]+16;
  if (range_minus_1&0xff000000) code_bits=msb[range_minus_1>>24]+24;
  code_range=1<<code_bits;


  /* Work out the number of codes which can be one bit shorter */
  short_codes=code_range-range;
  long_codes=range-short_codes;

  if (!short_codes)
    {
      /* Read simple fixed length code and return */
      decode_few_bits(code_bits,(unsigned int*)pp,c);
      (*pp)+=low;
    }
  else
    {
      int sc_on_2=short_codes>>1;
      int shift;
      int add;
      
      /* Decode bits */
      decode_few_bits(code_bits-1,&code,c);
      code=code<<1;

      /* Rotate LSB around to become MSB */
      code=(code<<1)+((code>>(code_bits-1))&1);
      code&=mask[code_bits];
      
      /* Shorten the code if we can */
      if (code>=(short_codes<<1))
	{ 
	  code|=range_decode_equiprobable(c,2);
	}
      
      add=code>>1;
      if (add>short_codes) add=short_codes; 
      
      value=code-add;
      
      if (!leafP) shift=-(long_codes>>1); /* move short codes from centre */
      else shift=sc_on_2;		
      value-=shift;
      
      if (value<0) value+=range;
      if (value>=range) value-=range;
      
      (*pp)=value+low;
    }
  
  
#ifdef DEBUG
  {
    int p=*pp;
    if ((p>high||p<low)||(DEBUG&4))
      {
	int b;
	printf("fdec [%d,%d,%d] [",low,p,high);
	for(b=code_bits-1;b>=0;b--)
	  printf("%d",(p>>b)&1);
	printf("]  %d bits processed\n",*out_bits);
	if (DEBUG&8) sleep(1);
      }
  }
#endif
  
  
  return;
}

inline void binary_many_decode(int low,int *pp,int high,int leafP,range_coder *c)
{
#ifdef ENCODING
  int p=*pp;
#else
  int p;
#endif

  /* Work out the range of values we can encode/decode */
  int range_minus_1=high-low;
  int range=range_minus_1+1;
  int code_bits;
  int code_range;
  int short_codes,long_codes;
  int value;
  unsigned int code;

#ifdef DEBUG
#ifdef ENCODING
  if (low>high||(p>high)||(p<low))
    {
      printf("Illegal triple encountered: [%d,%d,%d]\n",
	     low,p,high);
      sleep(60);
    }
#endif
#endif
  
  /* Encode zero bits if the range is zero  */
  if (high==low) 
    {
      p=low; *pp=low;
#ifdef DEBUG
      if ((p>high||p<low)||(DEBUG&4))
	{
	  printf("mdec [%d,%d,%d]   -no code required-\n",low,p,high);
	  if (DEBUG&8) sleep(1);
	}
#endif
      return;
    }
  
  /* Otherwise, figure out how many bits we need */
  code_bits=msb[range_minus_1&0xff];
  if (range_minus_1>>8) code_bits=msb[(range_minus_1>>8)&0xff]+8;
  if (range_minus_1>>16) code_bits=msb[(range_minus_1>>16)&0xff]+16;
  if (range_minus_1>>24) code_bits=msb[range_minus_1>>24]+24;
  code_range=1<<code_bits;
  if (range>code_range) code_range=code_range<<1;

  /* Work out the number of codes which can be one bit shorter */
  short_codes=code_range-range;
  long_codes=range-short_codes;

  if (!short_codes)
    {
      /* Read simple fixed length code and return */
      decode_bits(code_bits,(unsigned int *)pp,c);
      (*pp)+=low;
    }
  else
    {
      int sc_on_2=short_codes>>1;
      int shift;
      int add;
      
      /* Decode bits */
      decode_few_bits(code_bits-1,&code,c);
      code=code<<1;
#ifdef DEBUG
      printf("mdec Raw code is 0x%x\n",code);
#endif
      /* Rotate LSB around to become MSB */
      code=(code<<1)+((code>>(code_bits-1))&1);
      code&=mask[code_bits];
#ifdef DEBUG
      printf("mdec after rotation code=0x%x\n",code);
#endif
      /* Work out final number of code bits */
      if (code>=(short_codes<<1))
	code|=range_decode_equiprobable(c,2);
      
      add=code>>1;
      if (add>short_codes) add=short_codes; if (add<0) add=0;		
#ifdef DEBUG
      printf("mdec add=%d, sc/2=%d\n",add,sc_on_2);
#endif
      value=code-add;
#ifdef DEBUG
      printf("mdec after removing short codes, value=%d\n",value);
#endif
      if (!leafP) shift=-(long_codes>>1); /* move short codes from centre */
      else shift=sc_on_2;		
      value-=shift;
      
      if (value<0) value+=range;
      if (value>=range) value-=range;
#ifdef DEBUG		
      printf("mdec after reassigning, value=0x%x\n",value);
#endif
      (*pp)=value+low;
    }
  
  
#ifdef DEBUG
  {
    int p=*pp;
    if ((p>high||p<low)||(DEBUG&4))
      {
	int b;
	printf("mdec [%d,%d,%d] [",low,p,high);
	for(b=code_bits-1;b>=0;b--)
	  printf("%d",(p>>b)&1);
	printf("]  %d bits processed\n",*out_bits);
	if (DEBUG&8) sleep(1);
      }
  }
#endif
  
  
  return;
}


#endif

/*      -------------------------------------------------
	Recursive Implementation
	------------------------------------------------- */
 
int ENCODE(ic_,_recursive_r)(int *list,
			     int list_length,
			     int *frequencies,
			     int *word_positions,
			     int corpus_document_count,
			     int max_document_words,
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
      else doc_high=corpus_document_count+(hi-list_length);
      
      /* Now narrow range to take into account the number of postings which occur between us and lo and hi */
      doc_low+=(p-lo-1); 
      doc_high-=(hi-p-1);
      
      /* Encode the document number */
      if (p<list_length)
	{
	  ENCODE(binary_,)(doc_low,&list[p],doc_high,step,c);
	}
      
      /* Encode the frequencies and postings if required */
      if (frequencies)
	{
	  int cf_low;
	  int cf_high;
	  
	  /* Work out initial constraint on this value */
	  if (lo>-1) cf_low=frequencies[lo]; else cf_low=0;
	  if (hi<list_length) cf_high=frequencies[hi]; 
	  else cf_high=max_document_words;
	  
	  /* Now narrow range to take into account the number of postings which occur between us and lo and hi */
	  cf_low+=(p-lo); 
	  cf_high-=(hi-p);
	  
	  /* Encode the document number */
	  ENCODE(binary_,)(cf_low,&frequencies[p],cf_high,step,c);
	  
	}
    }
  
  if (step>1)
    {
      /* Now recurse left and right children */
      ENCODE(ic_,_recursive_r)(list,list_length,
			       frequencies,
			       word_positions,
			       corpus_document_count,
			       max_document_words,
			       c,
			       lo,p-(step>>1),p,
			       step>>1);  
      
      ENCODE(ic_,_recursive_r)(list,list_length,
			       frequencies,
			       word_positions,
			       corpus_document_count,
			       max_document_words,
			       c,
			       p,p+(step>>1),hi,
			       step>>1);  
    }
  
  return 0;
}


int ENCODE(ic_,_recursive)(int *list,
			   int list_length,
			   int *frequencies,
			   int *word_positions,
			   int corpus_document_count,
			   int max_document_words,
			   range_coder *c)
{
  /* Start at largest power of two, and round list size up to next power of two to keep recursion simple, and
     keep bit stream compatible with fast interpolative coder */
  int powerof2=biggest_power_of_2(list_length<<1);
  
  ENCODE(ic_,_recursive_r)(list,list_length,
			   frequencies,
			   word_positions,
			   corpus_document_count,
			   max_document_words,
			   c,
			   -1,(powerof2>>1)-1,powerof2,powerof2>>1); 
  
  if (frequencies&&word_positions)
    { 
      unsigned int p;
      
      for(p=0;p<list_length;p++)
	{
	  unsigned f;
	  unsigned int bpow2;
	  
	  if (p) f=frequencies[p]-frequencies[p-1];
	  else f=frequencies[p];
	  bpow2=biggest_power_of_2(f);
	  if (bpow2<(f-1)) bpow2=bpow2<<1;
	  
	  ENCODE(ic_,_recursive_r)(word_positions,f,
				   NULL,NULL,
				   f,
				   max_document_words,
				   c,
				   -1,bpow2>>1,bpow2,bpow2>>1);
	  
	}
    }
  
  return 0;
}

/*      -------------------------------------------------
		  Heirarchial-Iterative Implementation
	------------------------------------------------- */

#ifdef COMMON

int tab_initialised=0;
unsigned int tab_p1[256];
unsigned char tab_lo1[256];
unsigned int tab_hi1[256];
unsigned char tab_leaf[256];


#ifdef USE_TAB_ALL

unsigned int tab_all[256];

#define TAB_P1(X) (tab_all[X]&0xff)
#define TAB_LO1(X) ((tab_all[X]>>8)&0xff)
#define TAB_HI1(X) (tab_all[X]>>16)

#define TAB_P0(X) ((tab_all[X]&0xff)-1)
#define TAB_LO0(X) (((tab_all[X]>>8)&0xff)-1)
#define TAB_HI0(X) ((tab_all[X]>>16)-1)
#define TAB_HI016(X) ((tab_all[X]&0xffff0000)-0x10000)

#define TAB_LEAF(X) (tab_all[X]&0x10000000)

#else

#define TAB_LEAF(X) (tab_leaf[X])

#ifdef USE_TAB_0

unsigned char tab_p0[256];
unsigned char tab_lo0[256];
unsigned char tab_hi0[256]; 

#define TAB_P0(X) tab_p0[X]
#define TAB_LO0(X) tab_lo0[X]
#define TAB_HI0(X) tab_hi0[X]

#else

#define TAB_P0(X) (tab_p1[X]-1)
#define TAB_LO0(X) (tab_lo1[X]-1)
#define TAB_HI0(X) (tab_hi1[X]-1)

#undef USE_TAB_0HI16
#ifdef USE_TAB_0HI16
unsigned int tab_hi016[256];
#define TAB_HI016(X) tab_hi016[X]
#else
#define TAB_HI016(X) ((tab_hi1[X]-1)<<16)
#endif

#endif

#define TAB_P1(X) tab_p1[X]
#define TAB_LO1(X) tab_lo1[X]
#define TAB_HI1(X) tab_hi1[X]

#endif

int initialise_hi_tables(int lo,int p,int hi,int step)
{
#ifdef USE_TAB_0
  tab_p0[tab_initialised]=p;
  tab_lo0[tab_initialised]=lo;
  tab_hi0[tab_initialised]=hi;
#endif

#ifdef USE_TAB_0HI16
  tab_hi016[tab_initialised]=hi<<16;
#endif

  tab_p1[tab_initialised]=p+1;
  tab_lo1[tab_initialised]=lo+1;
  tab_hi1[tab_initialised]=hi+1;

  tab_leaf[tab_initialised]=((step<1) ? 1 : 0);

#ifdef USE_TAB_ALL  
  tab_all[tab_initialised]
	 =((p+1)&0xff)+(((lo+1)<<8)&0xff00)+((hi+1)<<16)
	 +((step<1) ? 0x1000000 : 0);
#endif

  tab_initialised++;

  if (!step) return 0;

  initialise_hi_tables(lo,(lo+p)>>1,p,step>>1);
  initialise_hi_tables(p,((hi+p)>>1),hi,step>>1);

  return 0;
}
#endif

int ENCODE(ic_,_heiriter)(int *list,
			  int list_length,
			  int *frequencies,
			  int *word_positions,
			  int corpus_document_count,
			  int max_document_words,
			  range_coder *c)
{
  int j,k,n;
  int lower_bound,upper_bound;
  int lo,p,hi;
  int lo_j,p_j,hi_j;
  int lo_k,p_k,hi_k;
		  
  /* Make sure our unique order lookup tables are initialised */
  if (!tab_initialised) 
    {
      initialise_hi_tables(0,127,255,64);
    }
  
  /* Commence the three layers of traversal, which allows for 2^24 entries.
     Adding an extra layer is trivial, but requires 64 bit arithmatic. */
  
  /* Fill in the top level (0x00{00-ff}0000) */
  for(j=0;j<256;j++) 
    {
      /* Work out bounds of the list slice. */
      if (j<255) 
	{ 
	  lo_j=TAB_LO0(j)<<16; 
	  p_j=TAB_P0(j)<<16; 
	  hi_j=TAB_HI0(j)<<16; 
	}
      else 
	{ 
	  p_j=255<<16; lo_j=254<<16; 
	  hi_j=corpus_document_count-(list_length-p_j); 
	}
      
      /* Save time if this slice is entirely irrelevant */
      if (list_length<=p_j) continue;
      
      if (TAB_LO1(j)>1) lower_bound=list[lo_j]+(p_j-lo_j);
      else lower_bound=p_j;
      
      /* if (p_j||lo_j) lower_bound=list[lo_j]+(p_j-lo_j);
	 else lower_bound=p_j; */
      
      if (hi_j<list_length) upper_bound=list[hi_j]-(hi_j-p_j);
      else upper_bound=corpus_document_count-(list_length-p_j-1);
      
      /* Encode or decode value */
#ifdef ENCODING
      binary_many_encode(lower_bound,list[p_j],upper_bound,
			 hi_j-p_j,c);
#else
      binary_many_decode(lower_bound,&list[p_j],upper_bound,
			 hi_j-p_j,c);
#endif
    }
  
  /* Fill in the middle level (0x00{00-ff}{01-ff}00), making use of the top level values */
  for(j=0;j<256;j++) 
    {
      /* Work out bounds of the list slice. */
      if (j<255) p_j=(TAB_P0(j))<<16; else p_j=255<<16; 
      
      /* Save time if this slice is entirely irrelevant */
      if (list_length<=p_j) continue;
      
      for(k=0;k<255;k++)
	{
	  p_k=p_j+(TAB_P1(k)<<8); 
	  
	  /* Save time if this slice is entirely irrelevant */
	  if (list_length<=p_k) continue;
	  
	  lo_k=p_j+(TAB_LO1(k)<<8); 
	  hi_k=p_j+(TAB_HI1(k)<<8);
	  
	  if (TAB_LO1(k)>1) lower_bound=list[lo_k]+(p_k-lo_k);
	  else lower_bound=list[p_j]+(p_k-p_j);
	  
	  if (hi_k<list_length) upper_bound=list[hi_k]-(hi_k-p_k);
	  else upper_bound=corpus_document_count-(list_length-p_k-1);
	  
	  /* Encode or decode value */
#ifdef ENCODING
	  binary_many_encode(lower_bound,list[p_k],upper_bound,
			     hi_k-p_k,c);
#else
	  binary_many_decode(lower_bound,&list[p_k],upper_bound,
			     hi_k-p_k,c);
#endif
	}
    }
  
  /* Fill out the bottom layer (0x00{00-ff}{00-ff}{01-ff}), making use of the higher levels  */
  for(k=0;k<65536;k++)
    {
      int slice_range,slice_upper,slice_lower;
      
      p_k=(k<<8);
      
      /* Save time if this slice is entirely irrelevant */
      if (list_length<=p_k) break;
      
      slice_lower=list[p_k];
      
      if (list_length<=(p_k+256)) 
	slice_upper=corpus_document_count+1;
      else slice_upper=list[p_k+256]-1;
      slice_range=slice_upper-slice_lower;
      
      if (slice_range<0x1000000)
	{
	  /* Fields are narrow enough to always fit into a word, regardless of the fraction of
	     the bottom byte that is already occupied */
	  for(n=0;n<255;n++)
	    {
	      p=p_k+TAB_P1(n);
	      
	      if (list_length<=p) continue;
	      
	      lo=p_k+TAB_LO1(n);
	      hi=p_k+TAB_HI1(n);
	      
	      /* Work out bounds of interval */
	      
	      if (TAB_LO1(n)>1) lower_bound=list[lo]+(p-lo);
	      else lower_bound=slice_lower+TAB_P1(n);
	      
	      if (hi<list_length)
		upper_bound=list[hi]-(hi-p);
	      else 
		upper_bound=slice_upper-(hi-p);
	      
	      /* Encode or decode value */
#ifdef ENCODING
	      binary_many_encode(lower_bound,list[p],upper_bound,
				 TAB_LEAF(n),c);
#else
	      binary_few_decode(lower_bound,&list[p],upper_bound,
				TAB_LEAF(n),c);
#endif
	    }
	}
      else
	{
	  /* Field width is too wide to guarantee the correctness of the fast coder used above */
	  for(n=0;n<255;n++)
	    {
	      p=p_k+TAB_P1(n);
	      
	      if (list_length<=p) continue;
	      
	      lo=p_k+TAB_LO1(n);
	      hi=p_k+TAB_HI1(n);
	      
	      /* Work out bounds of interval */
	      
	      if (TAB_LO1(n)>1) lower_bound=list[lo]+(p-lo);
	      else lower_bound=slice_lower+TAB_P1(n);
	      
	      if (hi<list_length)
		upper_bound=list[hi]-(hi-p);
	      else 
		upper_bound=slice_upper-(hi-p);
	      
	      /* Encode or decode value */
#ifdef ENCODING
	      binary_many_encode(lower_bound,list[p],upper_bound,
				 TAB_LEAF(n),c);
#else
	      binary_many_decode(lower_bound,&list[p],upper_bound,
				 TAB_LEAF(n),c);
#endif
	    }
	}
      
    }
  
  return 0;
}

/*      -------------------------------------------------
	Fixed Binary Code (Selector) Coder (according to Anh&Moffat 2004)
	------------------------------------------------- */

#ifdef COMMON

inline void decode_few_bits(int code_bits,unsigned int *value,range_coder *c)
{
  /* Read / Write code */  
  int v=0;
  int i;
  for(i=0;i<code_bits;i++) 
    v=(v<<1)+range_decode_equiprobable(c,2);
  *value=v;
  
  return;
}  

inline void decode_bits(int code_bits,unsigned int *value,range_coder *c)
{  
  decode_few_bits(code_bits,value,c);
}

inline void encode_bits(int code_bits,unsigned int value,range_coder *c)
{  
  range_encode_equiprobable(c,(1<<code_bits),value);
}



struct selector_table {
  int relative_base;
  int s[7][3];
  int max;
  
  int field_width[17];
  int s_num[17];
};

struct selector_table selector_tables[7]={
  { /* Table at minimum offset */
	 0,{{1,2,3},
		 {4,5,6},
		 {7,8,9},
		 {10,11,-1},
		 {12,13,-1},
		 {14,-1,-1},
		 {15,-1,-1}}, 16,
	 {0,0,0,0,1,1,1,2,2,2,3,3,4,4,5,6,99},
	 {0,1,2,3,1,2,3,1,2,3,1,2,1,2,1,1,1}
  },
  { /* Table at 1 */
	 -1,{{1,2,3},
		  {4,5,6},
		  {7,8,9},
		  {10,11,-1},
		  {12,13,-1},
		  {14,-1,-1},
		  {15,-1,-1}}, 16,
	 {0,-1,-1,-1,0,0,0,1,1,1,2,2,3,3,4,5,99},
	 {0,1,2,3,1,2,3,1,2,3,1,2,1,2,1,1,1}
  },
  { /* Table at 2 */
	 -2,{{1,2,-1},
		  {3,4,5},
		  {6,7,8},
		  {9,10,11},
		  {12,13,-1},
		  {14,-1,-1},
		  {15,-1,-1}}, 16,
	 {0,-2,-2,-1,-1,-1,0,0,0,1,1,1,2,2,3,4,99},
	 {0,1,2,1,2,3,1,2,3,1,2,3,1,2,1,1,1}
  },  
  { /* Centred table, with no distortion */
	 -3,{{1,-1,-1},
		  {2,3,-1},
		  {4,5,6},
		  {7,8,9},
		  {10,11,12},
		  {13,14,-1},
		  {15,-1,-1}}, 16,
	 {0,-3,-2,-2,-1,-1,-1,0,0,0,1,1,1,2,2,3,99},
	 {0,1,1,2,1,2,3,1,2,3,1,2,3,1,2,1,1}
  },
  { /* Only two values larger */
	 -4,{{1,-1,-1},
		  {2,3,-1},
		  {4,5,-1},
		  {6,7,-1},
		  {8,9,10},
		  {11,12,13},
		  {-1,14,15}}, 16,
	 {0,-4,-3,-3,-2,-2,-1,-1,0,0,0,1,1,1,2,2,99},
	 {0,1,1,2,1,2,1,2,1,2,3,1,2,3,2,3,1}
	 },
  { /* Only one value larger */
	 -5,{{1,-1,-1},
		  {2,-1,-1},
		  {3,4,-1},
		  {5,6,7},
		  {8,9,10},
		  {11,12,13,},
		  {-1,14,15}}, 16,
	 {0,-5,-4,-3,-3,-2,-2,-2,-1,-1,-1,0,0,0,1,1,99},
	 {0,1,1,1,2,1,2,3,1,2,3,1,2,3,2,3,1}
  },
  { /* At maximum value */
	 -6,{{1,-1,-1},
		  {2,-1,-1},
		  {3,4,-1},
		  {5,6,7},
		  {8,9,10},
		  {11,12,13},
		  {-1,14,15}}, 16,
	 {0,-6,-5,-4,-4,-3,-3,-3,-2,-2,-2,-1,-1,-1,0,0,99},
	 {0,1,1,1,2,1,2,3,1,2,3,1,2,3,2,3,1}
  }
};


/* Infinity may be added to the number of bits output.
	Therefore infinity should be less than half of the maximum value representable in an integer */
#define INFINITY 0x3fffffff
#define get_cost_to_end(FIELD_WIDTH,S_NUM,LIST_INDEX) cost_to_end[((LIST_INDEX)<<(6+2))+((FIELD_WIDTH)<<2)+((S_NUM)&3)]
#define DEBUG_COST_TO_END
#ifdef DEBUG_COST_TO_END
inline void set_cost_to_end(int f,int s,int l, int v,int list_length,int *cost_to_end)
{
  if (f>63) { fprintf(stderr,"field_width too large\n"); exit(3); }
  if (f<0) { fprintf(stderr,"field_width too small\n"); exit(3); }
  if (s>2) { fprintf(stderr,"s_num too large\n"); exit(3); }
  if (s<0) { fprintf(stderr,"s_num too small\n"); exit(3); }
  if (l>=list_length) { fprintf(stderr,"list index too large\n"); exit(3); }
  if (l<0) { fprintf(stderr,"list index too small\n"); exit(3); }

  if ((((l<<(6+2))+(f<<2)+(s&3))<0)
		||(((l<<(6+2))+(f<<2)+(s&3))>=(list_length<<(6+2))))
	 {
		fprintf(stderr,"Computed array index out of bounds\n");
		exit(3);
	 }
  get_cost_to_end(f,s,l)=v;
  return;
}
#else
#define set_cost_to_end(F,S,L,V,LL,CTE) get_cost_to_end(F,S,L)=V
#endif
int ic_encode_selector_r(int s1,int s2,int s3,int m,int escape,
								 int *list,
								 int list_length,
								 int *frequencies,
								 int *word_positions,
								 int corpus_document_count,
								 int max_document_words,
								 range_coder *c)
{
  int max_bits=log2_ceil(corpus_document_count);
  int *cost_to_end;
  int *s3_extension[64];
  int current_width=max_bits;
  int current_table=6;
  int i,j,k,l;
  int *dgaps;
  char *dgapbits;

  int s1_m=s1*m;
  int s2_m=s2*m;

  int possible_lengths[18]
	 ={s1_m,s2_m,
		s3*m,(s3+1)*m,(s3+2)*m,(s3+3)*m,
		(s3+4)*m,(s3+5)*m,(s3+6)*m,(s3+7)*m,
		(s3+8)*m,(s3+9)*m,(s3+10)*m,(s3+11)*m,
		(s3+12)*m,(s3+13)*m,(s3+14)*m,(s3+15)*m};

  int max_extent,selector_table=3,table_offset;

  /* Allocate and populate array for dgaps, and get size in bits of each dgap */
  dgaps=malloc(sizeof(int)*list_length);
  dgapbits=malloc(sizeof(char)*list_length);
  if ((!dgaps)||(!dgapbits))
	 {
		fprintf(stderr,"Could not allocate d-gap array for "
				  "list of length %d\n",list_length);
		exit(3);
	 }

  /* the number of bits required per d-gap is based on one less than the interval, since we can assume that there is an increase
	  of at least one each time */
  dgaps[0]=list[0]-1;
  for(i=1;i<list_length;i++) dgaps[i]=list[i]-list[i-1]-1;
  for(i=0;i<list_length;i++) dgapbits[i]=log2_ceil(dgaps[i]);

  /* Allocate cost vectors for every possible action */
  cost_to_end=malloc(sizeof(int)*list_length*64*4);
  if (!cost_to_end)
	 {
		fprintf(stderr,"Could not allocate optimisation tables for "
				  "list of length %d\n",list_length);
		exit(3);
	 }
  for(i=0;i<=max_bits;i++) 
	 {
		s3_extension[i]=malloc(sizeof(int)*list_length);
		if (!s3_extension[i])
		  {
			 fprintf(stderr,"Could not allocate optimisation tables for "
						"list of length %d\n",list_length);
			 exit(3);
		  }
	 }

  /* Calculate the maximum number of fields that we can encode */
  max_extent=s3*m+m*15;

  for(i=list_length-1;i>-1;i--)
	 {
		int bits;
		int max_length=0;

		/* Work out best step for each of the 16 possible selectors,
			taking into account whether the code at the destination 
			point is actually valid, i.e., is not outside of the relative 
			addressing range */

		/* printf("\nList offset #%d : s{1,2,3}={%d,%d,%d}, m=%d, esc=%s\n",
			i,s1,s2,s3,m,escape ? "Y" : "N"); */

		for(bits=0;bits<=max_bits;bits++)
		  {
			 int s_num;
			 int best_cost_to_end[3]={INFINITY,INFINITY,INFINITY};
			 int best_cost_to_end_s_num[3]={-1,-1,-1};
			 int best_cost_to_end_s_3_extension=-1;
			 int best_cost_to_end_code[3]={-1,-1,-1};
			 int best_cost_to_end_tail_s_num[3]={-1,-1,-1};

			 int this_cost_to_end=INFINITY;

			 int max_l;

			 /* Work out which selector table applies here */
			 if (bits<3) selector_table=bits; 
			 else if (bits>(max_bits-3)) 
				selector_table=6-(max_bits-bits); 
			 else selector_table=3;

			 table_offset=selector_tables[selector_table].relative_base;

			 /* Work out the maximum distance we can encode using this 
				 field length.  Note that this length accumulates with each 
				 increasing value of y, rather than getting recalced from the start,
			      since the maximum length can only increase as the bit field widens */
			 while((dgapbits[i+max_length]<=bits)
					 &&max_length<max_extent
					 &&max_length<=(list_length-i))
				max_length++;

			 if (max_length>=(list_length-i))
				{
				  /* This width gets us to the end, so indicate that it is long enough for anything */
				  max_length=list_length-i+possible_lengths[17];
				}

			 /* Don't waste time if this field width is too narrow - just mark this path as impossible */
			 if (!max_length) 
				{
				  for(s_num=0;s_num<=2;s_num++)
					 set_cost_to_end(bits,s_num,i,INFINITY,
										  list_length,cost_to_end);
				  continue;
				}

			 /* printf("Field width = %d, table #%d : ",bits,selector_table); */
				
			 /* Try all possible lengths to reach the next code */
			 max_l=3+escape*15;
			 for(l=0;(l<max_l) && (max_length>=possible_lengths[l])
					 ;l++)
				{
				  int this_length_cost;
				  int this_length=possible_lengths[l];
				  int tail_index=i+possible_lengths[l];

				  /* Trim codes to fit if necessary */
				  if (!l)
					 if ((i+this_length)>list_length)
						this_length=list_length-i;

				  this_length_cost=4+this_length*bits;

				  s_num=3;
				  if (l==0) s_num=1;
				  if (l==1) s_num=2;

				  /* Now look for the optimal path from this next code to the end of the list.
					  Unfortunately, we cannot choose the simgle best code in all cases, because only
					  16 transitions out are valid. */
				  for(j=0;j<7;j++)
					 {
						int tail_s_num=INFINITY;
					 
						/* Lookup if this transition is valid in the current selector table */
						int field_size=dgapbits[i]+j+table_offset;

						int code_number;

						/* Work out code for this combination.  -1 means not valid */
						code_number=selector_tables[selector_table].s[j][s_num-1];
						if (bits==max_bits&&l==0) 
						  code_number=selector_tables[selector_table].max;

						if ((field_size>0)&&(field_size<=max_bits)
							 &&(code_number>-1))
						  {
							 /* Work out the minimum cost to the end */
							 int tail_cost=INFINITY;

							 if (tail_index<list_length) {
								for(k=0;k<3;k++)
								  if (get_cost_to_end(field_size,k,tail_index)
										<tail_cost)
									 { 
										tail_cost
										  =get_cost_to_end(field_size,k,tail_index);
										tail_s_num=k+1;
									 }
							 } else {
								/* No tail - so add zero cost to the end */
								tail_cost=0; tail_s_num=-1; 
							 }
							 
							 /* Work out the cost of this code */
							 this_cost_to_end=this_length_cost;
							 if (escape&&s_num==3) this_cost_to_end+=4;
							 
							 /* Add to it the cost to the end */
							 if (tail_cost<INFINITY)
								this_cost_to_end+=tail_cost;
							 else 
								this_cost_to_end=INFINITY;
						  }

						/* See if this is the best cost to end seen so far */	
						if (this_cost_to_end<best_cost_to_end[s_num-1])
						  {
							 best_cost_to_end[s_num-1]=this_cost_to_end;
							 best_cost_to_end_s_num[s_num-1]=s_num;
							 best_cost_to_end_code[s_num-1]
								=selector_tables[selector_table].s[j][s_num-1];
							 best_cost_to_end_tail_s_num[s_num-1]=tail_s_num;
							 if (s_num==3&&escape)
								best_cost_to_end_s_3_extension=l-2;
							 else
								best_cost_to_end_s_3_extension=0;
						  }
					 }

				  if (!l)
					 {
						/* Try using maximum width field and s1, since it is always valid */
						this_cost_to_end=4+this_length*max_bits;
						if (tail_index<list_length)
						  this_cost_to_end+=
							 get_cost_to_end(max_bits,0,tail_index);

						if (this_cost_to_end<best_cost_to_end[0])
						  {
							 best_cost_to_end[0]=this_cost_to_end;
							 best_cost_to_end_s_num[0]=s_num;
							 best_cost_to_end_code[0]=16;
							 best_cost_to_end_tail_s_num[0]=0;
							 best_cost_to_end_s_3_extension=0;
						  }
					 }
				}

			 /* We now have the best cost to the end, so record it */
			 /* printf("Cost of :"); */
			 for(s_num=1;s_num<=3;s_num++)
				{
				  if ((s_num==1)&&(bits==max_bits)
						&&(best_cost_to_end[s_num-1]==INFINITY))
					 printf("WARNING:"
							  " max width, s1 combo has inifinite cost at #%d!\n",i);
				  set_cost_to_end(bits,s_num-1,i,
										best_cost_to_end[s_num-1],
										list_length,cost_to_end);
				  if (best_cost_to_end[s_num-1]>=INFINITY)
					 {
						int next_index=i+possible_lengths[s_num-1]
						  + (escape ? m*best_cost_to_end_s_3_extension : 0 );

						if (0)
						  if ((next_index<=list_length)&&(bits>=dgapbits[i]))
							 {
								int z;
								printf("WARNING: Infinite cost to end from "
										 "index #%d, dgap=%d, width=%d,"
										 " s_num=%d, dest index #%d\n",
										 i,dgaps[i],bits,s_num,next_index);
								printf("Possible moves from index #%d (dgap=%d):\n",
										 next_index,
										 (next_index<list_length) 
										 ? dgapbits[next_index] : -1);
								for(z=0;z<=max_bits;z++)
								  {
									 int s;
									 printf("  %2d bits : cost =",z);
									 for(s=0;s<3;s++)
										printf(" %d,",
												 get_cost_to_end(z,s,next_index));
									 printf("\n");
								  }
							 }
					 }
				  /* printf(" s%d=%d,",s_num,best_cost_to_end[s_num-1]); */
				}
			 s3_extension[bits][i]=best_cost_to_end_s_3_extension;
			 /* if (cost_to_end[bits][3-1][i]<INFINITY)
				 printf(" s3_extension=%d",s3_extension[bits][i]);
				 printf("\n"); */
		  }
	 }

  /* Follow back-track to select optimal codes to describe this list */
  for(i=0;i<list_length;)
	 {
		int best_cost_to_end=INFINITY;
		int best_width=-1;
		int best_s_num=-1;
		int best_code=-1;
		int table_offset=selector_tables[current_table].relative_base;

		int this_code;
		int this_cost_to_end;

		int row,s_num;

		/* Try possible destinations */
		for(row=0;row<7;row++)
		  {
			 for(s_num=1;s_num<=3;s_num++)
				{
				  this_code=selector_tables[current_table].s[row][s_num-1];
				  if (this_code>-1)
					 {
						int this_width=current_width+table_offset+row;
						this_cost_to_end=get_cost_to_end(this_width,s_num-1,i);
						if (this_cost_to_end<best_cost_to_end)
						  {
							 best_width=this_width;
							 best_s_num=s_num;
							 best_code=this_code;
							 best_cost_to_end=this_cost_to_end;
						  }
					 }
				}
		  }

		/* Try s1 jump to max_bits, since it is always valid */
		this_code=selector_tables[current_table].max;
		if (this_code>-1)
		  {
			 this_cost_to_end
				=get_cost_to_end(max_bits-1,0,i);
			 if (this_cost_to_end<best_cost_to_end)
				{
				  best_width=max_bits;
				  best_s_num=1;
				  best_code=this_code;
				  best_cost_to_end=this_cost_to_end;
				}
		  }

		/* Output code */
		best_code--;
		encode_bits(4,best_code,c);
		if (best_s_num==3)
		  {
			 if (escape)
				encode_bits(4,s3_extension[best_width][i],c);
			 j=possible_lengths[2+s3_extension[best_width][i]];
			 /* printf("s3 + %d extension gives length of %d\n",
				 s3_extension[best_width][i],
				 j); */
		  }
		else
		  j=possible_lengths[best_s_num-1];
		j+=i;
		if (i>list_length) i=list_length;

		/* printf("Writing %d entries of %d bits : i=%d, j=%d\n",
			possible_lengths[best_s_num-1],best_width,i,j); */

		if (best_width<0)
		  {
			 /* There was no possible valid field width here that would work.
				 There is always at least one, so it must be a bug to reach here. */

			 printf("ERROR: No valid encoding from index %d, dgap=%d\n",
					  i,i ? list[i]-list[i-1] : list[i]);
			 printf("The following represents each possible coding using\n");
			 printf("  the current table (#%d):\n\n",current_table);

			 /* Try possible destinations */
			 for(row=0;row<7;row++)
				{
				  for(s_num=1;s_num<=3;s_num++)
					 {
						this_code=selector_tables[current_table].s[row][s_num-1];
						if (this_code>-1)
						  {
							 int this_width=current_width+table_offset+row;
							 this_cost_to_end=get_cost_to_end(this_width,s_num-1,i);
							 printf("  code=%-2d, row=%d (width = %d), s%d"
									  " : cost to end = %d\n",
									  this_code,
									  row,this_width,s_num,this_cost_to_end);
						  }
					 }
				}
			 this_code=selector_tables[current_table].max;
			 if (this_code>-1)
				{
				  this_cost_to_end
					 =get_cost_to_end(max_bits-1,0,i);
				  printf("  code=%-2d, width = %d, s1"
							" : cost to end = %d\n",
							this_code,
							max_bits,this_cost_to_end);
				}
			 exit(-3);
			 
		  }


		/* Encode each d-gap using the specified field width */
		if (j>list_length) j=list_length;
		for(;i<j;i++)
		  {
			 /* printf("Encoding entry #%d\n",i); */
			 if (dgaps[i]>=(1<<best_width))
				{
				  printf("Encoding entry #%d = %d in %d bits\n",
							i,dgaps[i],best_width);
				  printf("   VALUE TOO BIG FOR FIELD\n");
				  printf("m=%d, s3=%d, escape=%d, best_code=%d\n",
							m,s3,escape,best_code);
				  exit(-3);
				}
			 if (i) 
			   encode_bits(best_width,list[i]-list[i-1]-1,c);
			 else encode_bits(best_width,list[i]-1,c);
		  }

		/* Select field width and table context for next code */
		current_width=best_width;
		if (current_width<3) current_table=current_width;
		else if ( current_width>=(max_bits-3))
		  current_table=6-(max_bits-current_width);
		else
		  current_table=3;
	 }

  /* Clean up allocated memory */
  free(dgaps); free(dgapbits); free(cost_to_end);
  for(i=0;i<=max_bits;i++) 
	 free(s3_extension[i]);
  
  return 0;
}
	  

int ic_encode_selector(int *list,
		       int list_length,
		       int *frequencies,
		       int *word_positions,
		       int corpus_document_count,
		       int max_document_words,
		       range_coder *c)
{
  int m,s1=1,s2=2,s3=3,escape;
  
  int shortest_length=-1;
  int sl_s1=1,sl_s2=2,sl_s3=3,sl_m=1,sl_escape=0;

  int model_number;

  /* Try all eight multipliers */
  if (1)
    {
      /* XXX Hard wired to these parameters for now */
      sl_m=3;
      sl_s1=1;
      sl_s2=2;
      sl_s3=4;
      sl_escape=0;
      shortest_length=-1;
    }
  else
    for(m=1;m<=8;m++)
      for(escape=0;escape<2;escape++)
	for(s3=3;s3<5;s3++)
	  {
	    /** Test Parameters */
	    range_coder *t=range_new_coder(8192);
	    
	    ic_encode_selector_r(s1,s2,s3,m,escape,
				 list,list_length,
				 frequencies,
				 word_positions,
				 corpus_document_count,
				 max_document_words,
				 t);

	    /* Remember these parameters if they give the shortest output so far */
	    if (t->entropy<shortest_length
		||shortest_length==-1)
	      {
		shortest_length=t->entropy;
		sl_s1=s1; sl_s2=s2; sl_s3=s3;
		sl_m=m; sl_escape=escape;
	      }
	    
	    range_coder_free(t);
	  }
  
  /* Output model description */
  model_number=sl_m-1;
  if (sl_s3==4) model_number|=1<<3;
  if (sl_escape) model_number|=1<<4;
  encode_bits(5,model_number,c);
  
  /* output encoded stream */
  ic_encode_selector_r(sl_s1,sl_s2,sl_s3,sl_m,sl_escape,
		       list,list_length,
		       frequencies,
		       word_positions,
		       corpus_document_count,
		       max_document_words,
		       c);
  
  return 0;
}

int ic_decode_selector(int *list,
		       int list_length,
		       int *frequencies,
		       int *word_positions,
		       int corpus_document_count,
		       int max_document_words,
		       range_coder *c)
{
  int max_bits=log2_ceil(corpus_document_count);
  
  int model_number;
  int m,s1=1,s2=2,s3=3,escape=0;
  
  int s1_m,s2_m,s3_m;
  int s3_m_ext[16];
  
  int i;
  
  int current_table=6;
  int current_width=max_bits;
  
  int code,count;
  int field_width_modifier,field_width;
  int s_num,s3_extension;
  
  int value=0;
  
  /* Read model description */
  decode_few_bits(5,(unsigned int *)&model_number,c);
  
  m=(model_number&7)+1;
  if (model_number&8) s3=4;
  if (model_number&16) escape=1;
  
  /* Get extended lengths ready for fast lookup */
  s1_m=s1*m; s2_m=s2*m; s3_m=s3*m;
  for(i=0;i<16;i++)
    s3_m_ext[i]=(s3+i)*m;
  
  /* Follow back-track to select optimal codes to describe this list */
  for(i=0;i<list_length;)
    {
      decode_few_bits(4,(unsigned int *)&code,c); code++;
      
      field_width_modifier
	=selector_tables[current_table].field_width[code];
      s_num
	=selector_tables[current_table].s_num[code];
      
      if (field_width_modifier!=99)
	field_width=current_width+field_width_modifier;
      else
	field_width=max_bits;
      
      switch(s_num)
	{
	case 1: count=s1_m; break;
	case 2: count=s2_m; break;
	case 3:
	  if (escape)
	    decode_few_bits(4,(unsigned int *)&s3_extension,c);
	  else s3_extension=0;
	  count=s3_m_ext[s3_extension];
	  break;
	default:
	  fprintf(stderr,
		  "ERROR: illegal s value (%d) encountered during decoding"
		  " @ #%d\n",
		  s_num,i);
	  fprintf(stderr,"       Selector table was #%d, code number was #%d\n"
		  ,current_table,code);
	  exit(3);
	}
      
      /* Decode each d-gap using the specified field width */
      if ((i+count>list_length)) count=list_length-i;
      count+=i;
      if (field_width<25)
	{
	  for(;i!=count;i++)
	    {
	      int dgap;
	      decode_few_bits(field_width,(unsigned int *)&dgap,c);
	      value=value+dgap+1;
	      list[i]=value;
	      /* if (i) list[i]=list[i-1]+dgap+1;
		 else  list[i]=dgap+1; */
	    }
	}
      else
	{
	  for(;i!=count;i++)
	    {
	      int dgap;
	      decode_bits(field_width,(unsigned int *)&dgap,c);
	      value=value+dgap+1;
	      list[i]=value;
	      /* if (i) list[i]=list[i-1]+dgap+1;
		 else  list[i]=dgap+1; */
	    }
	}
      
      /* Select field width and table context for next code */
      current_width=field_width;
      if (current_width<0)
	{
	  fprintf(stderr,
		  "ERROR: negative field width in stream @ offset #%d\n",i);
	  exit(3);
	}
      if (current_width<3) current_table=current_width;
      else if ( current_width>=(max_bits-3))
	current_table=6-(max_bits-current_width);
      else
	current_table=3;
    }
  
  return 0;
}

#endif

/*      -------------------------------------------------
		  Test Bed
	------------------------------------------------- */

#ifdef STANDALONE
#ifdef COMMON

void swap(int *a,int *b)
{
  int t=*a;
  *a=*b;
  *b=t;
}

#ifndef __sun__

#include <sys/time.h>

typedef long long hrtime_t;

hrtime_t gethrvtime()
{
  struct timeval tv;

  gettimeofday(&tv,NULL);

  return tv.tv_sec*1000000000+tv.tv_usec*1000;
}
#endif

/* Size of demo list for compression / decompression */
#define DEMO_SIZE (40*1024)
#define LIST_DENSITY 0.0005
#define REPEAT_COUNT 1

int main(int argc,char **argv)
{
  unsigned char out[DEMO_SIZE<<2];
  unsigned int out_bits=0,bit_count;

  unsigned int list1[7]={2,7,8,10,11,12,16};
  unsigned int list1_out[7];

  unsigned int list2[DEMO_SIZE];
  unsigned int list2_out[DEMO_SIZE];

  hrtime_t last_time,this_time;
  long long mean;
 
  int i,b,verrors;

  int fraction=0xffffff*LIST_DENSITY;

  if (0)
	 {
		ic_encode_recursive(list1,7,NULL,NULL,20,20,out,&out_bits);
		
		printf("%d bits written\n",out_bits);

		ic_decode_recursive(list1_out,7,NULL,NULL,20,20,out,&out_bits);
	 }

  /* Generate a series of gaps corresponding to a geometric series */
  b=0;
  for(i=0;i<DEMO_SIZE;i++)
	 {
		b++;
		while((random()&0xffffff)>fraction) b++;
		list2[i]=b;
	 }
  printf("Using %d integers ranging from %d to %d inclusive,"
			" requiring %d bits\n",
	 DEMO_SIZE,list2[0],list2[DEMO_SIZE-1],
			log2_ceil(list2[DEMO_SIZE-1]-list2[0]));

  if (DEMO_SIZE<20)
	 {
		printf("  ");
		for(i=0;i<DEMO_SIZE;i++) printf("%d ",list2[i]);
		printf("\n");
	 }

  if (1)
	 {
		printf("\n\nRecursive implementation:\n");
		printf("-------------------------\n\n");
		
		for(i=0;i<1;i++)
		  {
			 bzero(list2_out,sizeof(int)*DEMO_SIZE);
			 out_bits=0;
			 
			 /* Encode */
			 printf("Encoding ...  "); fflush(stdout);
			 
			 last_time=gethrvtime();
			 ic_encode_recursive(list2,DEMO_SIZE,NULL,NULL,b,b,out,&out_bits);
			 this_time=gethrvtime();
			 
			 printf("%d bits written in %lldns per pointer,"
					  " %d pointers in range [%d,%d]\n",
					  out_bits,(this_time-last_time)/DEMO_SIZE,
					  DEMO_SIZE,list2[0],b);
		  }
		
		mean=0;
		printf("Decoding "); fflush(stdout);
		for(i=0;i<REPEAT_COUNT;i++)
		  {
			 /* Decode */
			 bit_count=0;		
			 
			 last_time=gethrvtime();
			 ic_decode_recursive(list2_out,DEMO_SIZE,NULL,NULL,b,b,out,&bit_count);
			 this_time=gethrvtime();
			 
			 printf("."); fflush(stdout);
			 
			 mean+=(this_time-last_time)/DEMO_SIZE;
		  }
		
		printf(" Used %d of %d bits during decoding at %lldns per pointer.\n",
				 bit_count,out_bits,mean/REPEAT_COUNT);
		
		
		/* Verify */
		printf("Verifying ...\n");
		for(i=0;i<DEMO_SIZE;i++)
		  {
			 if (list2[i]!=list2_out[i])
				{
				  printf("Verify error at entry %d (found %d instead of %d)\n",
							i,list2_out[i],list2[i]);
				  break;
				}
		  }
	 }		
  
  printf("Writing gap distribution stats to dist.csv\n");
  {
	 FILE *f=fopen("dist.csv","w");
	 fprintf(f,"bin;counti;countl;cumulativei;cumulativel\n");
	 for(i=0;i<GD_COUNT;i++)
		{
		  fprintf(f,"%d;%d;%d;%d;%d\n",i,gap_dist_i[i],gap_dist_l[i],
					 i ? gap_dist_i[i]+gap_dist_i[i-1] : gap_dist_i[i],
					 i ? gap_dist_l[i]+gap_dist_l[i-1] : gap_dist_l[i]);
		  if (i) gap_dist_i[i]+=gap_dist_i[i-1];
		  if (i) gap_dist_l[i]+=gap_dist_l[i-1];
		}
	 fclose(f);
  }
  
  printf("\n\nHeirarchial implementation:\n");
  printf("---------------------------\n\n");

  for(i=0;i<1;i++)
	 {
		bzero(list2_out,sizeof(int)*DEMO_SIZE);

		/* Encode */
		printf("Encoding ...   "); fflush(stdout);
		out_bits=0;
		
		last_time=gethrvtime();
		ic_encode_heiriter(list2,DEMO_SIZE,NULL,NULL,b,b,
								 out,&out_bits,DEMO_SIZE);
		this_time=gethrvtime();
		
		printf("%d bits written in %lldns per pointer,"
				 " %d pointers in range [%d,%d]\n",
				 out_bits,(this_time-last_time)/DEMO_SIZE,
				 DEMO_SIZE,list2[0],b);
	 }

  printf("Decoding "); fflush(stdout);
  mean=0;
  for(i=0;i<REPEAT_COUNT;i++)
	 {
		/* Decode */
		bit_count=0;
		
		last_time=gethrvtime();
		ic_decode_heiriter(list2_out,DEMO_SIZE,NULL,NULL,b,b,out,&bit_count);
		this_time=gethrvtime();

		printf("."); fflush(stdout);

		mean+=(this_time-last_time)/DEMO_SIZE;
	 }

  printf("Used %d of %d bits during decoding at %lldns per pointer.\n",
			bit_count,out_bits,mean/REPEAT_COUNT);

  /* Verify */
  printf("Verifying ...\n");
  for(i=0;i<DEMO_SIZE;i++)
	 {
		if (list2[i]!=list2_out[i])
		  {
			 printf("Verify error at entry %d (found %d instead of %d)\n",
					  i,list2_out[i],list2[i]);
			 break;
		  }
	 }

  if (1)
  {
	 printf("\n\nencode_bits() and decode_bits():\n");
	 printf("--------------------------------\n\n");
	 bzero(list2_out,sizeof(int)*DEMO_SIZE);
	 for(i=0;i<=32;i++)
		{
		  int v_in,v_out,k;
		  int fw;
		  printf("."); fflush(stdout);
		  for(fw=0;fw<32;fw++)
			 {
				int j;
				for(j=0;j<1000;j++)
				  {
					 v_in=random()&mask[fw]; 
					 
					 out_bits=i;
					 encode_bits(fw,v_in,(unsigned char *)list2_out,
									 &out_bits,DEMO_SIZE);
					 encode_bits(fw,v_in,(unsigned char *)list2_out,
									 &out_bits,DEMO_SIZE);
					 encode_bits(fw,v_in,(unsigned char *)list2_out,
									 &out_bits,DEMO_SIZE);
					 out_bits=i; v_out=-1;
					 for(k=0;k<3;k++)
						{
						  decode_bits(fw,&v_out,(unsigned char *)list2_out,
										  &out_bits);
						  if (v_in!=v_out)
							 printf("decode_bits() error at phase %d, fw %d"
									  " : got %d instead of %d (d=%d), k=%d\n",
									  i,fw,v_out,v_in,v_in-v_out,k);
						}
					 if (out_bits!=(i+fw*3))
						printf("decode_bits() error at phase %d, fw %d"
								 " : read %d bits instead of %d\n",
								 i,fw,out_bits-i,fw);
					 
				  }
			 }
		}
  }

  if (1)
	 {
		printf("\n\nGreedy Selector implementation:\n");
		printf("----------------------------------\n\n");
		
		for(i=0;i<1;i++)
		  {
			 bzero(list2_out,sizeof(int)*DEMO_SIZE);
			 
			 /* Encode */
			 printf("Encoding ...   "); fflush(stdout);
			 out_bits=0;
			 
			 last_time=gethrvtime();
			 ic_encode_selector_greedy(list2,DEMO_SIZE,NULL,NULL,b,b,
												out,&out_bits,DEMO_SIZE<<2);
			 this_time=gethrvtime();
			 
			 printf("%d bits written in %lldns per pointer,"
					  " %d pointers in range [%d,%d]\n",
					  out_bits,(this_time-last_time)/DEMO_SIZE,
					  DEMO_SIZE,list2[0],b);
		  }
		
		printf("Decoding "); fflush(stdout);
		mean=0;
		for(i=0;i<REPEAT_COUNT;i++)
		  {
			 /* Decode */
			 bit_count=0;
			 
			 last_time=gethrvtime();
			 ic_decode_selector(list2_out,DEMO_SIZE,NULL,NULL,b,b,out,&bit_count);
			 this_time=gethrvtime();
			 
			 printf("."); fflush(stdout);
			 
			 mean+=(this_time-last_time)/DEMO_SIZE;
		  }
		
		printf("Used %d of %d bits during decoding at %lldns per pointer.\n",
				 bit_count,out_bits,mean/REPEAT_COUNT);
		
		/* Verify */
		printf("Verifying ...\n");
		verrors=0;
		for(i=0;i<DEMO_SIZE&&verrors<3;i++)
		  {
			 if (list2[i]!=list2_out[i])
				{
				  printf("Verify error at entry %d (found %d instead of %d)\n",
							i,list2_out[i],list2[i]);
				  verrors++;
				}
		  }
	 }

  if (1)
	 {
		printf("\n\nSelector implementation:\n");
		printf("---------------------------\n\n");
		
		for(i=0;i<1;i++)
		  {
			 bzero(list2_out,sizeof(int)*DEMO_SIZE);
			 
			 /* Encode */
			 printf("Encoding ...   "); fflush(stdout);
			 out_bits=0;
			 
			 last_time=gethrvtime();
			 ic_encode_selector(list2,DEMO_SIZE,NULL,NULL,b,b,
									  out,&out_bits,DEMO_SIZE<<2);
			 this_time=gethrvtime();
			 
			 printf("%d bits written in %lldns per pointer,"
					  " %d pointers in range [%d,%d]\n",
					  out_bits,(this_time-last_time)/DEMO_SIZE,
					  DEMO_SIZE,list2[0],b);
		  }
		
		printf("Decoding "); fflush(stdout);
		mean=0;
		for(i=0;i<REPEAT_COUNT;i++)
		  {
			 /* Decode */
			 bit_count=0;
			 
			 last_time=gethrvtime();
			 ic_decode_selector(list2_out,DEMO_SIZE,NULL,NULL,b,b,out,&bit_count);
			 this_time=gethrvtime();
			 
			 printf("."); fflush(stdout);
			 
			 mean+=(this_time-last_time)/DEMO_SIZE;
		  }
		
		printf("Used %d of %d bits during decoding at %lldns per pointer.\n",
				 bit_count,out_bits,mean/REPEAT_COUNT);
		
		/* Verify */
		printf("Verifying ...\n");
		verrors=0;
		for(i=0;i<DEMO_SIZE&&verrors<3;i++)
		  {
			 if (list2[i]!=list2_out[i])
				{
				  printf("Verify error at entry %d (found %d instead of %d)\n",
							i,list2_out[i],list2[i]);
				  verrors++;
				}
		  }
	 }

  return 0;
}
#endif
#endif
