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
#include "message_stats.h"
#include "charset.h"

int stripCase(unsigned char *in,unsigned char *out)
{
  int l=0;
  int i;
  for(i=0;in[i];i++) out[l++]=tolower(in[i]);
  out[l]=0;
  return 0;
}

int mungeCase(char *m)
{
  int i;

  /* Change isolated I's to i, provided preceeding char is lower-case
     (so that we don't mess up all-caps).
  */
  for(i=1;m[i+1];i++)
    if (m[i]=='I'&&(!isalpha(m[i-1]))&&(!isalpha(m[i+1])))
      {
	m[i]^=0x20;
      }
     
  return 0;
}

int decodeCaseModel1(range_coder *c,unsigned char *line)
{
 int wordNumber=0;
  int wordPosn=-1;
  int lastWordInitialCase=0;
  int lastWordInitialCase2=0;
  int lastCase=0;

  int i;
  //  printf("caps eligble chars: ");
  for(i=0;line[i];i++) {
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
	  unsigned int frequencies[1]={caseposn1[0][0]};
	  if (i==0) frequencies[0]=casestartofmessage[0][0];
	  else if (wordNumber>1&&wordPosn==0) {
	    /* start of word, so use model that considers initial case of
	       previous word */
	    frequencies[0]=casestartofword2[lastWordInitialCase][0];
	    if (wordNumber>2)
	      frequencies[0]=
		casestartofword3[lastWordInitialCase2][lastWordInitialCase][0];
	    if (1)
	      printf("last word began with case=%d, p_lower=%f\n",
		     lastWordInitialCase,
		     (frequencies[0]*1.0)/0x1000000
		     );
	  }
	  if (1) printf("case of first letter of word/message @ %d: p=%f\n",
			i,(frequencies[0]*1.0)/0x1000000);
	  upper=range_decode_symbol(c,frequencies,2);
	} else {
	  /* subsequent letter, so can use case of previous letter in model */
	  if (wordPosn>79) wordPosn=79;
	  if (1) printf("case of first letter of word/message @ %d.%d: p=%f\n",
			i,wordPosn,
			(caseposn2[lastCase][wordPosn][0]*1.0)/0x1000000);
	  printf("  lastCase=%d, wordPosn=%d\n",lastCase,wordPosn);
	  upper=range_decode_symbol(c,caseposn2[lastCase][wordPosn],2);
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

int encodeCaseModel1(range_coder *c,unsigned char *line)
{
  /*
    Have previously looked at flipping case of isolated I's 
    first, but makes only 1% difference in entropy of case,
    which doesn't seem enough to give confidence that it will
    typically help.  This might change if we model probability of
    case of first letter of a word based on case of first letter of
    previous word.
  */

  int wordNumber=0;
  int wordPosn=-1;
  int lastWordInitialCase=0;
  int lastWordInitialCase2=0;
  int lastCase=0;

  int i;
  //  printf("caps eligble chars: ");
  for(i=0;line[i];i++) {
    int wordChar=charInWord(line[i]);
    if (!wordChar) {	  
      wordPosn=-1; lastCase=0;
    } else {
      if (isalpha(line[i])) {
	if (wordPosn<0) wordNumber++;
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
	  else if (wordNumber>1&&wordPosn==0) {
	    /* start of word, so use model that considers initial case of
	       previous word */
	    frequencies[0]=casestartofword2[lastWordInitialCase][0];
	    if (wordNumber>2)
	      frequencies[0]=
		casestartofword3[lastWordInitialCase2][lastWordInitialCase][0];
	    if (1)
	      printf("last word began with case=%d, p_lower=%f\n",
		     lastWordInitialCase,
		     (frequencies[0]*1.0)/0x1000000
		     );
	  }
	  if (1) printf("case of first letter of word/message @ %d: p=%f\n",
			i,(frequencies[0]*1.0)/0x1000000);
	  range_encode_symbol(c,frequencies,2,upper);
	} else {
	  /* subsequent letter, so can use case of previous letter in model */
	  if (wordPosn>79) wordPosn=79;
	  if (1) printf("case of first letter of word/message @ %d.%d: p=%f\n",
			i,wordPosn,
			(caseposn2[lastCase][wordPosn][0]*1.0)/0x1000000);
	  printf("  lastCase=%d, wordPosn=%d\n",lastCase,wordPosn);
	  range_encode_symbol(c,caseposn2[lastCase][wordPosn],2,upper);
	}
	if (isupper(line[i])) lastCase=1; else lastCase=0;
	if (wordPosn==0) {
	  lastWordInitialCase2=lastWordInitialCase;
	  lastWordInitialCase=lastCase;
	}
      }
    }
    
    /* fold all letters to lower case */
    if (line[i]>='A'&&line[i]<='Z') line[i]|=0x20;
  }
  //  printf("\n");

  return 0;
}
