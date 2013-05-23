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
  
  struct probability_vector pv;
  calcCurve(master_curve,NULL,&pv);

  range_encode_equiprobable(c,CHARCOUNT+1,permutation_length);

  int used[CHARCOUNT];
  for(i=0;i<CHARCOUNT;i++) used[i]=0;
  for(i=0;i<permutation_length;i++) {
    int range=0,rank=0,j;
    int charids[CHARCOUNT];
    long long sum=0;
    
    for(j=0;j<CHARCOUNT;j++)
      if (!used[freqs[j].b]) {
	sum+=pv.v[range];
	if (0) fprintf(stderr,"j=%d %02x %02x 0x%x sum=%llx\n",
		       j,range,freqs[j].b,pv.v[range],sum);
	if (range) pv.v[range]+=pv.v[range-1];
	charids[range]=freqs[j].b;
	range++;
      }

    double scale=sum*1.0/0xffffff;
    for(j=0;j<range;j++) pv.v[j]/=scale;
    if (0) fprintf(stderr,"sum=0x%llx, scale=%f\n",sum,scale);

    for(rank=0;rank<range;rank++) 
      if (charids[rank]==freqs[i].b) break;
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

int permutation_decode(range_coder *c,doublet *freqs,int permutation[CHARCOUNT],
		       int master_curve)
{
  int i;
  int used[CHARCOUNT];
  for(i=0;i<CHARCOUNT;i++) used[i]=0;

  struct probability_vector pv;  
  calcCurve(master_curve,&pv,NULL);

  int permutation_length=range_decode_equiprobable(c,CHARCOUNT+1);
  fprintf(stderr,"permutation_length=%d\n",permutation_length);

  for(i=0;i<permutation_length;i++) {
    int range=0,rank=0,j;
    int charids[CHARCOUNT];
    long long sum=0;
    
    for(j=0;j<CHARCOUNT;j++)
      if (!used[freqs[j].b]) {
	sum+=pv.v[range];
	if (0) fprintf(stderr,"j=%d %02x %02x 0x%x sum=%llx\n",
		       j,range,freqs[j].b,pv.v[range],sum);
	if (range) pv.v[range]+=pv.v[range-1];
	charids[range]=freqs[j].b;
	range++;
      }

    double scale=sum*1.0/0xffffff;
    for(j=0;j<range;j++) pv.v[j]/=scale;
    if (0) fprintf(stderr,"sum=0x%llx, scale=%f\n",sum,scale);

    for(rank=0;rank<range;rank++) 
      if (charids[rank]==freqs[i].b) break;
    if (rank==range) {
      fprintf(stderr,"This shouldn't happen: Couldn't find symbol %02x in list.\n",
	      freqs[i].b);
      exit(-1);
    }
    if (0)
      fprintf(stderr,"Encoding %d of %d for symbol %02x (perm position %d of %d)\n",
	      rank,range,freqs[i].b,i,permutation_length);
    rank=range_decode_symbol(c,pv.v,range);
    used[charids[rank]]=1;
    freqs[i].b=charids[rank];
  }
  return 0;
}

