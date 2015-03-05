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
#include "lowercasealpha.c"
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

#undef DEBUG

extern long long total_unicode_millibits;
extern long long total_unicode_chars;

int strncmp816(char *s1,unsigned short *s2,int len)
{
  int j;
  for(j=0;j<len;j++) {
    int d=(unsigned char)s1[j]-(unsigned short)s2[j];
    if (d) return d;
  }	  
  return 0;
}

#endif

/*
  TODO: Currently uses flat distribution for digit probabilities.  Should use "rule of 9" or similar.
  TODO: We don't currently handle the situation where there are no statistics to
  return for a given code page.
*/
int FUNC(LCAlphaSpace)(range_coder *c,unsigned short *s,int length,stats_handle *h,
		       double *entropyLog)
{
  int o;
  int lastCodePage=0x0080/0x80;
  int lastLastCodePage=0x0080/0x80;
  int firstUnicode=1;

  for(o=0;o<length;o++) {
    double previousEntropy=c->entropy;
#ifdef ENCODING
    int t=s[o];
#endif
    s[o]=0;
    struct probability_vector *v=extractVector(s,o,h);
#ifdef ENCODING
    int symbol=charIdx(t);
    //    vectorReport(NULL,v,symbol);
    range_encode_symbol(c,v->v,CHARCOUNT,symbol);
    s[o]=t;
#else
    int symbol=range_decode_symbol(c,v->v,CHARCOUNT);
    s[o]=chars[symbol];
#endif
    if (s[o]>='0'&&s[o]<='9') {
#ifdef ENCODING
      range_encode_equiprobable(c,10,s[o]-'0');
#else
      s[o]='0'+range_decode_equiprobable(c,10);
#endif
    } else if (s[o]=='U'||s[o]>0x7f) {
      // unicode character
      unsigned int *counts=(unsigned int *)getUnicodeStatistics(h,lastCodePage);
#ifdef ENCODING
      double before=c->entropy;
      int switchedPage=0;
      if (firstUnicode) {
	range_encode_equiprobable(c,511,(s[o]/0x80)-1);
	switchedPage=1;
	firstUnicode=0;
	lastCodePage=s[o]/0x80;
      } else if (s[o]/0x80!=lastCodePage) {
	// character is not in current code page
	range_encode_symbol(c,counts,128+512,128+s[o]/0x80);      
	switchedPage=1;
	lastLastCodePage=lastCodePage;
	lastCodePage=s[o]/0x80;
      }
      // now character must be in code page, so encode
      counts=(unsigned int *)getUnicodeStatistics(h,lastCodePage);
      range_encode_symbol(c,counts,switchedPage?128:(128+512),s[o]&0x7f);
      double unicodeEntropy=c->entropy-before;
      //      fprintf(stderr,"encoded 0x%04x in %.2f bits\n",
      //	      s[o],unicodeEntropy);
      total_unicode_millibits+=unicodeEntropy*1000;
      total_unicode_chars++;
#else
      if (firstUnicode) {
	firstUnicode=0;
	lastCodePage=range_decode_equiprobable(c,511)+1;
	counts=(unsigned int *)getUnicodeStatistics(h,lastCodePage);
	symbol=range_decode_symbol(c,counts,128);
      } else {
	symbol=range_decode_symbol(c,counts,128+512);      
      }
      if (symbol>127) {
	lastLastCodePage=lastCodePage;
	lastCodePage=symbol-128;
	unsigned int *counts=(unsigned int *)getUnicodeStatistics(h,lastCodePage);
	if (counts) symbol=range_decode_symbol(c,counts,128);
	else return -1;
      } 
      s[o]=lastCodePage*0x80+symbol;
      //      fprintf(stderr,"decoded unicode char: 0x%04x\n",s[o]);
#endif
    }
    // Record entropy for this character if requested.
    if (entropyLog) entropyLog[o]=c->entropy-previousEntropy;
  }

  return 0;
}
