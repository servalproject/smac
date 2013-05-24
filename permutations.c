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

long long delta_savings=0;

int permutation_build_sublist(int alphabet_size,int *used,int *range,
			      struct probability_vector *pv_master,
			      struct probability_vector *pv,
			      int *charids,
			      int i,int up,int down)
{
  int j;
  long long sum=0;
    
  for(j=0;j<alphabet_size;j++)
    if (!used[j]) {
      pv->v[*range]=pv_master->v[j];
      sum+=pv->v[*range];
      if (range) pv->v[*range]+=pv->v[(*range)-1];
      charids[*range]=j;
#ifdef UPDOWN
      if (i) {
	if (up&&j>freqs[i-1].b) (*range)++;
	if (down&&j<freqs[i-1].b) (*range)++;
      } else 
#endif
	(*range)++;
    }
  
  double scale=sum*1.0/0xffffff;
  for(j=0;j<*range;j++) pv->v[j]/=scale;
  if (0) fprintf(stderr,"sum=0x%llx, scale=%f\n",sum,scale);
  
  return 0;
}

int parsehexdigit(int d)
{
  if (d>='0'&&d<='9') return d-'0';
  if (d>='A'&&d<='F') return d-'A'+0xa;
  if (d>='a'&&d<='f') return d-'a'+0xa;
  return 0;
}

int permutation_best_delta_cost(int alphabet_size,
				int permutation_length,doublet *freqs,
				char **permutations,int permutation_count)
{
  int i,j;
  int p;
  int porder[alphabet_size];
  int used[alphabet_size];
  double best_cost;
  int best_perm=-1;
  for(p=0;p<permutation_count;p++)
    {
      double cost=0;
      // Parse string representation of permutation
      // fprintf(stderr,"Parsing %s\n",permutations[p]);
      for(i=0;i<alphabet_size;i++) used[i]=0;
      for(i=0;i<strlen(permutations[p]);i+=2) {
	porder[i/2]=(parsehexdigit(permutations[p][i])<<4)
		   |parsehexdigit(permutations[p][i+1]);
	used[porder[i/2]]=1;
	// fprintf(stderr,"[%02x]",porder[i/2]);
      }
      // Reassemble implied tail distribution
      i/=2; 
      for(j=0;j<alphabet_size;j++) {
	if (!used[j]) { porder[i++]=j; 	
	  /* fprintf(stderr,"{%02x}",porder[i-1]); */ }
      }
      // fprintf(stderr,"\n");

      // Compute distance in terms of the bits required to encode it
      int previ=0;
      for(i=0;i<alphabet_size;i++) {
	if (porder[i]!=freqs[i].b) {
	  for(j=i+1;j<alphabet_size;j++) if (porder[j]==freqs[i].b) break;
	  if (j==alphabet_size) {
	    fprintf(stderr,"This should not happen.\n");
	    exit(-1);
	  }
	  // fprintf(stderr,"  move #%d from rank %d to rank %d\n",freqs[i].b,j,i);
	  // Move element at j to i, and shuffle the rest down.
	  while(j>i) { porder[j]=porder[j-1]; j--; }
	  porder[i]=freqs[i].b;
	  // Cost of specifying position of start point of rotate
	  cost+=log(alphabet_size-previ)/log(2);
	  // Cost of specifying position of end point of rotate
	  cost+=log(alphabet_size-(i-1))/log(2);
	  previ=i;
	}
      }
      if (cost<best_cost||best_perm==-1) { best_cost=cost; best_perm=p; }
    }
  if (best_perm!=-1)
    return best_cost+1; 
  else return -1;
}

int permutation_encode(range_coder *c,doublet *freqs,
		       int alphabet_size,int permutation_length, 
		       int master_curve,int depth,
		       char **permutations, int *permutation_addresses,
		       int permutation_count, off_t max_address)
{
  int i;
  int charids[alphabet_size];
  struct probability_vector pv_master;
  calcCurve(master_curve,NULL,&pv_master);

  struct probability_vector pv;

  int best_delta=permutation_best_delta_cost(alphabet_size,
					     permutation_length,freqs,
					     permutations,permutation_count);
  int coding_cost=c->bits_used;

  range_encode_equiprobable(c,alphabet_size+1,permutation_length);

#ifdef UPDOWN
  int p_down=(UPDOWN*0xffffff);

  unsigned int updown[2];
  updown[0]=p_down;
  updown[1]=0xffffff; 
#endif

  int used[alphabet_size]; for(i=0;i<alphabet_size;i++) used[i]=0;
  for(i=0;i<permutation_length;i++) {
    int range=0,rank=0;

#ifdef UPDOWN 
    int up,down;
    if (i==0) { up=1; down=1; }
    else if (freqs[i].b>freqs[i-1].b) { up=1; down=0; } else { up=0; down=1; }
    
    if (i) range_encode_symbol(c,updown,2,up);
#else
    int up=1,down=1;
#endif

    permutation_build_sublist(alphabet_size,used,&range,
			      &pv_master,&pv,charids,
			      i,up,down);

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

  coding_cost=c->bits_used-coding_cost;
  if (best_delta<coding_cost) {
    fprintf(stderr,"Saving %d bits (%d bits instead of %d) by delta coding.\n",
	    coding_cost-best_delta,best_delta,coding_cost);
    delta_savings+=coding_cost-best_delta;
  }
  return 0;
}

int permutation_decode(range_coder *c,doublet *freqs,
		       int alphabet_size,int master_curve)
{
  int i;
  int used[alphabet_size];
  for(i=0;i<alphabet_size;i++) used[i]=0;

  struct probability_vector pv_master;
  calcCurve(master_curve,NULL,&pv_master);

  struct probability_vector pv;

  int permutation_length=range_decode_equiprobable(c,alphabet_size+1);

#ifdef UPDOWN
  int p_down=(UPDOWN*0xffffff);

  unsigned int updown[2];
  updown[0]=p_down;
  updown[1]=0xffffff;
#endif

  for(i=0;i<permutation_length;i++) {
    int range=0,rank=0,j;
    int charids[alphabet_size];
    long long sum=0;
    
#ifdef UPDOWN
    int up,down;
    if (i==0) { up=1; down=1; }
    else {
      up=range_decode_symbol(c,updown,2);
      down=1-up;
    }
#else
    int up=1, down=1;
#endif

    permutation_build_sublist(alphabet_size,used,&range,
			      &pv_master,&pv,charids,
			      i,up,down);

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

