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
#include <sys/mman.h>

#include "arithmetic.h"
#include "packed_stats.h"
#include "charset.h"

void stats_handle_free(stats_handle *h)
{
  if (h->mmap) munmap(h->mmap,h->fileLength);
  if (h->buffer) free(h->buffer);
  if (h->bufferBitmap) free(h->bufferBitmap);
  free(h);
  return;
}

stats_handle *stats_new_handle(char *file)
{
  int i;
  stats_handle *h=calloc(sizeof(stats_handle),1);
  h->file=fopen(file,"r");
  if(!h->file) {
    free(h);
    return NULL;
  }
  
  /* Get size of file */
  fseek(h->file,0,SEEK_END);
  h->fileLength=ftello(h->file);

  fseek(h->file,4,SEEK_SET);
  for(i=0;i<4;i++) h->rootNodeAddress=(h->rootNodeAddress<<8)
		     |(unsigned char)fgetc(h->file);
  for(i=0;i<4;i++) h->totalCount=(h->totalCount<<8)
		     |(unsigned char)fgetc(h->file);
  fprintf(stderr,"rootNodeAddress=0x%x, totalCount=%d\n",
	  h->rootNodeAddress,h->totalCount);

  /* Try to mmap() */
  h->mmap=mmap(NULL, h->fileLength, PROT_READ, MAP_SHARED, fileno(h->file), 0);
  if (h->mmap!=MAP_FAILED) return h;
  
  /* mmap failed, so create buffer and bitmap for keeping track of which parts have 
     been loaded. */
  h->mmap=NULL;

  h->buffer=malloc(h->fileLength);
  h->bufferBitmap=calloc((h->fileLength+1)>>10,1);
  return h;
}

unsigned char *getCompressedBytes(stats_handle *h,int start,int count)
{
  if (!h) { fprintf(stderr,"failed test at line #%d\n",__LINE__); return NULL; }
  if (!h->file) 
    { fprintf(stderr,"failed test at line #%d\n",__LINE__); return NULL; }
  if (start<0||start>=h->fileLength) 
    { fprintf(stderr,"failed test at line #%d\n",__LINE__); return NULL; }

  if ((start+count)>h->fileLength) 
    count=h->fileLength-start;

  /* If file is memory mapped, just return the address to the piece in question */
  if (h->mmap) return &h->mmap[start];
  
  /* not memory mapped, so pull in the appropriate part of the file as required */
  int i;
  for(i=(start>>10);i<=((start+count)>>10);i++)
    {
      if (!h->bufferBitmap[i]) {
	fread(&h->buffer[i<<10],1024,1,h->file);
	h->bufferBitmap[i]=1;
      }
    }
  return &h->buffer[start];
}

struct node *extractNodeAt(char *s,unsigned int nodeAddress,int count,
			   stats_handle *h)
{
  range_coder *c=range_new_coder(0);
  c->bit_stream=getCompressedBytes(h,nodeAddress,1024);
  c->bit_stream_length=1024*8;
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
  if (1)
    fprintf(stderr,"children=%d, storedChildren=%d, highChild=%d\n",
	    children,storedChildren,highChild);

  struct node *n=calloc(sizeof(struct node),1);

  n->count=count;

  int childrenRemaining=children;
  for(i=0;i<children;i++) {
    if (i<(children-1)) {
      thisChild=lastChild+1
	+range_decode_equiprobable(c,highChild+1
				   -(childrenRemaining-1)-(lastChild+1));
      if (1) fprintf(stderr,"decoded thisChild=%d (as %d of %d)\n",
		     thisChild,thisChild-(lastChild+1),
		     highChild+1-(childrenRemaining-1)-(lastChild+1));
    }
    else {
      thisChild=highChild;
      if (1) fprintf(stderr,"inferred thisChild=%d\n",thisChild);
    }
    lastChild=thisChild;
    childrenRemaining--;

    thisCount=range_decode_equiprobable(c,(count-progressiveCount)+1);
    if (1)
      fprintf(stderr,"  decoded %d of %d for '%c'\n",thisCount,
	      (count-progressiveCount)+1,chars[thisChild]);
    progressiveCount+=thisCount;

    n->counts[thisChild]=thisCount;

    int addrP=range_decode_equiprobable(c,2);

    if (1) fprintf(stderr,"    decoded addrP=%d\n",addrP);
    if (addrP) {
      childAddress=lowAddr+range_decode_equiprobable(c,highAddr-lowAddr+1);
      if (1) fprintf(stderr,"    decoded addr=%d of %d\n",
		     childAddress-lowAddr,highAddr-lowAddr+1);
      lowAddr=childAddress;
      if (s[0]&&chars[thisChild]==s[0]) {
	n->children[thisChild]=extractNodeAt(&s[1],childAddress,
					     n->counts[thisChild],h);
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
  /* c->bit_stream is provided locally, so we must free the range coder manually,
     instead of using range_coder_free() */
  c->bit_stream=NULL;
  free(c);

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

struct node *extractNode(char *string,int len,stats_handle *h)
{
  int i;

  unsigned int rootNodeAddress=h->rootNodeAddress;
  unsigned int totalCount=h->totalCount;

  struct node *n=extractNodeAt(string,rootNodeAddress,totalCount,h);
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

int extractVector(char *string,int len,stats_handle *h,unsigned int v[69])
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
  struct node *n=extractNode(&string[ofs],len-ofs,h);
  if (0) fprintf(stderr,"  n=%p\n",n);
  while(!n) {
    ofs++;
    if (ofs>len) break;
    n=extractNode(&string[ofs],len-ofs,h);
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

  if (0) {
    fprintf(stderr,"probability of characters following '");
    for(i=ofs;i<len;i++) fprintf(stderr,"%c",string[i]);
    fprintf(stderr,"' (offset=%d):\n",ofs);
  }

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


int vectorReport(char *name,unsigned int v[69],int s)
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
