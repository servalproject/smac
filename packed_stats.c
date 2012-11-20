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
#include <stdlib.h>
#include <ctype.h>
#include <math.h>

#include "arithmetic.h"
#include "packed_stats.h"
#include "charset.h"

struct node *extractNodeAt(char *s,unsigned int nodeAddress,int count,FILE *f)
{
  range_coder *c=range_new_coder(8192);
  fseek(f,nodeAddress,SEEK_SET);
  fread(c->bit_stream,8192,1,f);
  c->bit_stream_length=8192*8;
  c->bits_used=0;
  c->low=0; c->high=0xffffffff;
  range_decode_prefetch(c);

  int children=range_decode_equiprobable(c,69+1);
  int storedChildren=range_decode_equiprobable(c,children+1);
  int highChild=68;
  int lastChild=-1;
  unsigned int progressiveCount=0;
  unsigned int thisCount;

  unsigned int highAddr=nodeAddress;
  unsigned int lowAddr=0;
  unsigned int childAddress;
  int thisChild;
  int i;

  if (children) highChild=range_decode_equiprobable(c,69+1);
  // fprintf(stderr,"children=%d, storedChildren=%d, highChild=%d\n",
  // 	  children,storedChildren,highChild);

  struct node *n=calloc(sizeof(struct node),1);

  n->count=count;

  int childrenRemaining=children;
  for(i=0;i<children;i++) {
    if (i<(children-1)) {
      thisChild=lastChild+1
	+range_decode_equiprobable(c,highChild+1
				   -(childrenRemaining-1)-(lastChild+1));
      if (0) fprintf(stderr,"decoded thisChild=%d\n",thisChild);
    }
    else {
      thisChild=highChild;
      if (0) fprintf(stderr,"inferred thisChild=%d\n",thisChild);
    }
    lastChild=thisChild;
    childrenRemaining--;

    thisCount=range_decode_equiprobable(c,(count-progressiveCount)+1);
    if (0)
      fprintf(stderr,"  decoded %d of %d for '%c'\n",thisCount,
	      (count-progressiveCount)+1,chars[thisChild]);
    progressiveCount+=thisCount;

    n->counts[thisChild]=thisCount;

    if (range_decode_equiprobable(c,2)) {
      childAddress=lowAddr+range_decode_equiprobable(c,highAddr-lowAddr+1);
      lowAddr=childAddress;
      if (s[0]&&chars[thisChild]==s[0]) {
	n->children[thisChild]=extractNodeAt(&s[1],childAddress,
					     n->counts[thisChild],f);
      }

    } else childAddress=0;
    if (0) 
	fprintf(stderr,"%-5s : %d x '%c' @ 0x%x\n",s,
		thisCount,chars[thisChild],childAddress);
  }

  if (0) {
    fprintf(stderr,"Extract '%s' @ 0x%x (children=%d, storedChildren=%d, highChild=%d)\n",
	    s,nodeAddress,children,storedChildren,highChild);
    dumpNode(n);
  }

  return n;
}

int dumpNode(struct node *n)
{
  if (!n) return 0;
  fflush(stderr);
  fflush(stdout);
  int i;
  int c=0;
  int sum=0;
  fprintf(stderr,"Node's internal count=%lld\n",n->count);
  for(i=0;i<69;i++) {
    // 12 chars wide
    fprintf(stderr," %c% 8d%c |",chars[i],n->counts[i],n->children[i]?'*':' ');
    c++;
    if (c>4) {
      fprintf(stderr,"\n");
      c=0;
    }
    sum+=n->counts[i];
  }
  fprintf(stderr,"\n%d counted occurrences\n",sum);
  
  fflush(stdout);
  return 0;
}

struct node *extractNode(char *string,int len,FILE *f)
{
  int i;

  unsigned int rootNodeAddress=0;
  unsigned int totalCount=0;

  fseek(f,4,SEEK_SET);
  for(i=0;i<4;i++) rootNodeAddress=(rootNodeAddress<<8)|(unsigned char)fgetc(f);
  for(i=0;i<4;i++) totalCount=(totalCount<<8)|(unsigned char)fgetc(f);
  if (0)
    fprintf(stderr,"root node is at 0x%08x, total count = %d\n",
	    rootNodeAddress,totalCount);

  struct node *n=extractNodeAt(string,rootNodeAddress,totalCount,f);
  if (0) {
    fprintf(stderr,"n=%p\n",n);
    fflush(stderr);
  }

  /* Return zero-order stats if no string provided. */
  if (!len) return n;

  struct node *n2=n;

  for(i=0;i<=len;i++) {
    struct node *next=n2->children[charIdx(string[i])];
    // dumpNode(n2);
    
    if (i<len)
      if (0)
	fprintf(stderr,"%c occurs %d/%lld (%.2f%%)\n",
		string[i],
		n2->counts[charIdx(string[i])],n2->count,
		n2->counts[charIdx(string[i])]*100.00/n2->count);
    if (i==len)
      {
	return n2;
      }
    if (string[i+1]&&(i<len)&&((!next)||(next->counts[charIdx(string[i+1])]<1)))
      {
	if (0)
	  fprintf(stderr,"Next layer down doesn't have any counts for the next character ('%c').\n",string[i+1]);
	// dumpNode(next);
	free(n2);
	return NULL;
      }

    /* Free higher-level nodes when done with them */
    free(n2); 
    n2=next;
    if (!n2) break;
  }
  
  return NULL;
}

int extractVector(char *string,int len,FILE *f,unsigned int v[69])
{
  if (0)
    printf("extractVector('%s',%d,...)\n",
	   string,len);
  /* Limit match length to maximum depth of statistics */
  if (len>6) {
    string=&string[len-6];
    len=6;
  }

  int ofs=0;
  if (0) fprintf(stderr,"extractVector(%d,%s)\n",len-ofs,&string[ofs]);
  struct node *n=extractNode(&string[ofs],len-ofs,f);
  if (0) fprintf(stderr,"  n=%p\n",n);
  while(!n) {
    ofs++;
    if (ofs>len) break;
    n=extractNode(&string[ofs],len-ofs,f);
    if (0) {
      fprintf(stderr,"extractVector(%d,%s)\n",len-ofs,&string[ofs]);
      fprintf(stderr,"  n=%p\n",n);
    }
  }
  if (!n) {
    fprintf(stderr,"Could not obtain any statistics (including zero-order frequencies). Broken stats data file?\n");
    fprintf(stderr,"  ofs=%d, len=%d\n",ofs,len);
    exit(-1);
  } 

  int i;

  fprintf(stderr,"probability of characters following '");
  for(i=ofs;i<len;i++) fprintf(stderr,"%c",string[i]);
  fprintf(stderr,"' (offset=%d):\n",ofs);

  int scale=0xffffff/(n->count+69);
  int cumulative=0;
  int sum=0;

  for(i=0;i<69;i++) {
    v[i]=cumulative+(n->counts[i]+1)*scale;
    cumulative=v[i];
    sum+=n->counts[i]+1;
    if (0) 
      fprintf(stderr,"  '%c' %d : 0x%06x (%d/%lld) %d\n",
	      chars[i],i,v[i],
	      n->counts[i]+1,n->count+69,sum);
  }
  
  /* Higher level nodes have already been freed, so just free this one */
  free(n);
  return 0;
}


int vectorReport(char *name,int v[69],int s)
{
  int high=0x1000000;
  int low=0;
  if (s) low=v[s-1];
  if (s<68) high=v[s];
  double percent=(high-low)*100.00/0x1000000;
  fprintf(stderr,"P[%s](%c) = %.2f%%\n",name,chars[s],percent);
  int i;
  low=0;
  for(i=0;i<68;i++) {
    if (i<68) high=v[i]; else high=0x1000000;
    double p=(high-low)*100.00/0x1000000;

    fprintf(stderr," '%c' %.2f%% |",chars[i],p);
    if (i%5==4) fprintf(stderr,"\n");

    low=high;
  }
  fprintf(stderr,"\n");
  return 0;
}
