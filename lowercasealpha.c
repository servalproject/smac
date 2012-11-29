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
#include "packed_stats.h"

#undef DEBUG

/*
  TODO: Unicode not yet handled.
  TODO: Currently uses flat distribution for digit probabilities.  Should use "rule of 9" or similar.
*/
int decodeLCAlphaSpace(range_coder *c,unsigned short *s,int length,stats_handle *h)
{
  FILE *stats_file=fopen("stats.dat","r");
  int c1=charIdx(' ');
  int c2=charIdx(' ');
  int c3;
  int o;
  for(o=0;o<length;o++) {
#ifdef DEBUG
    printf("so far: '%s', c1=%d(%c), c2=%d(%c)\n",s,c1,chars[c1],c2,chars[c2]);
#endif

    s[o]=0;
    struct probability_vector *v=extractVector(s,o,h);
    c3=range_decode_symbol(c,v->v,CHARCOUNT);
    s[o]=chars[c3];
    if (s[o]=='0') s[o]='0'+range_decode_equiprobable(c,10);
#ifdef DEBUG
    printf("  decode alpha char %d = %c\n",c3,s[o]);
#endif
    c1=c2; c2=c3;
  }
  fclose(stats_file);
  return 0;
}

int strncmp816(char *s1,unsigned short *s2,int len)
{
  int j;
  for(j=0;j<len;j++) {
    int d=(unsigned char)s1[j]-(unsigned short)s2[j];
    if (d) return d;
  }	  
  return 0;
}

int encodeLCAlphaSpace(range_coder *c,unsigned short *s,int len,stats_handle *h)
{
  int c1=charIdx(' ');
  int c2=charIdx(' ');
  int o;

  for(o=0;o<len;o++) {
    int c3=charIdx(s[o]);
    
#ifdef DEBUG
      printf("  encoding @ %d, c1=%d(%c) c2=%d(%c), c3=%d(%c)\n",
	     o,c1,chars[c1],c2,chars[c2],c3,chars[c3]);
#endif

    if (0)
      {
	int i;
	fprintf(stderr,"After '");
	for(i=0;i<o;i++) fprintf(stderr,"%c",s[i]);
	fprintf(stderr,"'\n");
      }
    int t=s[o]; 
    s[o]=0;
    struct probability_vector *v=extractVector(s,o,h);
    s[o]=t;
    range_encode_symbol(c,v->v,CHARCOUNT,c3);
    if (chars[c3]=='0') range_encode_equiprobable(c,10,s[o]-'0');
    if (0) vectorReport("var",v,c3);    
    c1=c2; c2=c3;
  }
  return 0;
}
