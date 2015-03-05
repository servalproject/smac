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

#ifndef UNDER_CONTROL
#define UNDER_CONTROL
#define COMMON
#undef FUNC
#undef ENCODING
#define FUNC(Y) decode ## Y
#include "case.c"
#undef COMMON
#undef FUNC
#define ENCODING
#define FUNC(Y) encode ## Y
#endif

#ifdef COMMON
#include <stdio.h>
#include <strings.h>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>

#include "arithmetic.h"
#include "charset.h"
#include "packed_stats.h"
#include "unicode.h"

int stripCase(unsigned short *in,int in_len,unsigned short *out)
{
  int l=0;
  int i;
  for(i=0;i<in_len;i++) {
    if (in[i]<0x80)
      out[l++]=tolower(in[i]);
    else out[l++]=in[i];
  }
  return 0;
}

int mungeCase(unsigned short *m,int len)
{
  int i;

  /* Change isolated I's to i, provided preceeding char is lower-case
     (so that we don't mess up all-caps).
  */
  for(i=1;i<(len-1);i++)
    if (m[i]<0x80)
      if (tolower(m[i])=='i'&&(!isalpha(m[i-1]))&&(!isalpha(m[i+1])))
	{
	  m[i]^=0x20;
	}
     
  return 0;
}

#endif

int FUNC(CaseModel1)(range_coder *c,unsigned short *line,int len,stats_handle *h)
{
  int wordNumber=0;
  int wordPosn=-1;
  int lastWordInitialCase=0;
  int lastWordInitialCase2=0;
  int lastCase=0;

  int i;

  //  printf("caps eligble chars: ");
  for(i=0;i<len;i++) {
    int wordChar=charInWord(line[i]);
    if (!wordChar) {	  
      wordPosn=-1; lastCase=0;
    } else {
      if (isalpha(line[i])) {
	if (wordPosn<0) wordNumber++;
	wordPosn++;
	int upper=-1;
	int caseEnd=0;

	/* note if end of word (which includes end of message,
	   implicitly detected here by finding null at end of string */
	if (!charInWord(line[i+1])) caseEnd=1;
	if (wordPosn==0) {
	  /* first letter of word, so can only use 1st-order model */
	  unsigned int frequencies[1]={h->caseposn1[0][0]};
	  if (i==0) frequencies[0]=h->casestartofmessage[0][0];
	  else if (wordNumber>1&&wordPosn==0) {
	    /* start of word, so use model that considers initial case of
	       previous word */
	    frequencies[0]=h->casestartofword2[lastWordInitialCase][0];
	    if (wordNumber>2)
	      frequencies[0]=
		h->casestartofword3[lastWordInitialCase2][lastWordInitialCase][0];
	    if (0)
	      printf("last word began with case=%d, p_lower=%f\n",
		     lastWordInitialCase,
		     (frequencies[0]*1.0)/0x1000000
		     );
	  }
	  if (0) printf("case of first letter of word/message @ %d: p=%f\n",
			i,(frequencies[0]*1.0)/0x1000000);
#ifdef ENCODING
	  upper=isupper(line[i]);
	  range_encode_symbol(c,frequencies,2,upper);
#else
	  upper=range_decode_symbol(c,frequencies,2);
#endif
	} else {
	  /* subsequent letter, so can use case of previous letter in model */
	  if (wordPosn>79) wordPosn=79;
	  if (0) {
	    printf("case of first letter of word/message @ %d.%d: p=%f\n",
		   i,wordPosn,
		   (h->caseposn2[lastCase][wordPosn][0]*1.0)/0x1000000);
	    printf("  lastCase=%d, wordPosn=%d\n",lastCase,wordPosn);
	  }
	  int pos=wordPosn;
	  while ((!h->caseposn2[lastCase][pos][0])&&pos) pos--;
#ifdef ENCODING
	  upper=isupper(line[i]);
	  range_encode_symbol(c,h->caseposn2[lastCase][pos],2,upper);
#else
	  upper=range_decode_symbol(c,h->caseposn2[lastCase][pos],2);
#endif
	}
	if (upper==1) line[i]=toupper(line[i]);

	if (isupper(line[i])) lastCase=1; else lastCase=0;
	if (wordPosn==0) {
	  lastWordInitialCase2=lastWordInitialCase;
	  lastWordInitialCase=lastCase;
	}      
	else if (upper==-1) {
	  fprintf(stderr,"%s(): character processed without determining case.\n",
		  __FUNCTION__);
	  exit(-1);
	}
      }
    }    
  }
  return 0;
}
