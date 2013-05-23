/*
  (C) Paul Gardner-Stephen 2012-2013.

  Generate variable order statistics from a sample corpus.
  Designed to run over partly-escaped tweets as obtained from extract_tweets, 
  which should be run on the output from a run of:

  https://stream.twitter.com/1.1/statuses/sample.json

  Twitter don't like people redistributing piles of tweets, so we will just
  include our summary statistics.
*/

/*
Copyright (C) 2012-2013 Paul Gardner-Stephen
 
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
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <assert.h>

#include "arithmetic.h"
#include "charset.h"
#include "packed_stats.h"
#include "unicode.h"

int permutation_encode(range_coder *c,doublet *freqs, int permutation_length, 
		       int master_curve,int depth)
{
  int i;
  
  struct probability_vector pv_master;
  calcCurve(master_curve,NULL,&pv_master);

  struct probability_vector pv;

  range_encode_equiprobable(c,CHARCOUNT+1,permutation_length);

#ifdef UPDOWN
  int p_down=(0.35*0xffffff);

  unsigned int updown[2];
  updown[0]=p_down;
  updown[1]=0xffffff; 
#endif

  int used[CHARCOUNT];
  for(i=0;i<CHARCOUNT;i++) used[i]=0;
  for(i=0;i<permutation_length;i++) {
    int range=0,rank=0,j;
    int charids[CHARCOUNT];
    long long sum=0;

#ifdef UPDOWN 
    int up,down;
    if (i==0) { up=1; down=1; }
    else if (freqs[i].b>freqs[i-1].b) { up=1; down=0; } else { up=0; down=1; }

    if (i) range_encode_symbol(c,updown,2,up);
#endif
   
    for(j=0;j<CHARCOUNT;j++)
      if (!used[j]) {
	pv.v[range]=pv_master.v[j];
	sum+=pv.v[range];
	if (range) pv.v[range]+=pv.v[range-1];
	charids[range]=j;
#ifdef UPDOWN
	if (i) {
	  if (up&&j>freqs[i-1].b) range++;
	  if (down&&j<freqs[i-1].b) range++;
	} else 
#endif
	  range++;
      }

    double scale=sum*1.0/0xffffff;
    for(j=0;j<range;j++) pv.v[j]/=scale;
    if (0) fprintf(stderr,"sum=0x%llx, scale=%f\n",sum,scale);

    for(rank=0;rank<range;rank++) 
      if (charids[rank]==freqs[i].b) {
	break;
      }
    if (rank==range) {
      fprintf(stderr,"This shouldn't happen: Couldn't find symbol %02x in list.\n",
	      freqs[i].b);
      exit(-1);
    }
    if (0)
      fprintf(stderr,"Encoding %d of %d for symbol %02x (perm position %d of %d)\n",
	      rank,range,freqs[i].b,i,permutation_length);
    range_encode_symbol(c,pv.v,range,rank);
    used[freqs[i].b]=1;
  }
  return 0;
}

int permutation_decode(range_coder *c,doublet *freqs,int master_curve)
{
  int i;
  int used[CHARCOUNT];
  for(i=0;i<CHARCOUNT;i++) used[i]=0;

  struct probability_vector pv_master;
  calcCurve(master_curve,NULL,&pv_master);

  struct probability_vector pv;

  int permutation_length=range_decode_equiprobable(c,CHARCOUNT+1);

  int p_down=(0.35*0xffffff);

#ifdef UPDOWN
  unsigned int updown[2];
  updown[0]=p_down;
  updown[1]=0xffffff;
#endif

  for(i=0;i<permutation_length;i++) {
    int range=0,rank=0,j;
    int charids[CHARCOUNT];
    long long sum=0;
    
#ifdef UPDOWN
    int up,down;
    if (i==0) { up=1; down=1; }
    else {
      up=range_decode_symbol(c,updown,2);
      down=1-up;
    }
#endif
   
    for(j=0;j<CHARCOUNT;j++)
      if (!used[j]) {
	pv.v[range]=pv_master.v[j];
	sum+=pv.v[range];
	if (range) pv.v[range]+=pv.v[range-1];
	charids[range]=j;
#ifdef UPDOWN
	if (i) {
	  if (up&&j>freqs[i-1].a) range++;
	  if (down&&j<freqs[i-1].a) range++;
	} else 
#endif
	  range++;
      }

    double scale=sum*1.0/0xffffff;
    for(j=0;j<range;j++) pv.v[j]/=scale;
    
    rank=range_decode_symbol(c,pv.v,range);

    used[charids[rank]]=1;
    freqs[i].a=charids[rank];
  }
  // Reassemble tail
  for(;i<CHARCOUNT;++i) {
    int j;
    for(j=0;j<CHARCOUNT;j++) if (!used[j]) break;
    freqs[i].a=j; used[j]=1;
    }

  return 0;
}

