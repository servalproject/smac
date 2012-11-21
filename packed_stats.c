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
  if (h->mmap) return &h->mmap[start-h->dummyOffset];
  
  /* not memory mapped, so pull in the appropriate part of the file as required */
  int i;
  for(i=((start)>>10);i<=((start+count)>>10);i++)
    {
      if (!h->bufferBitmap[i]) {
	fread(&h->buffer[i<<10],1024,1,h->file);
	h->bufferBitmap[i]=1;
      }
    }
  return &h->buffer[start];
}

struct node *extractNodeAt(char *s,int len,unsigned int nodeAddress,int count,
			   stats_handle *h,int debug)
{
  if (s[len]) fprintf(stderr,"Extracting node '%c'\n",s[len]);

  range_coder *c=range_new_coder(0);
  c->bit_stream=getCompressedBytes(h,nodeAddress,1024);
  c->bit_stream_length=1024*8;
  c->bits_used=0;
  c->low=0; c->high=0xffffffff;
  range_decode_prefetch(c);

  int children=range_decode_equiprobable(c,69+1);
  int storedChildren=range_decode_equiprobable(c,children+1);
  unsigned int progressiveCount=0;
  unsigned int thisCount;

  unsigned int highAddr=nodeAddress;
  unsigned int lowAddr=0;
  unsigned int childAddress;
  int i;

  unsigned int hasCount=(69-children)*0xffffff/69;
  unsigned int isStored=(69-storedChildren)*0xffffff/69;

  if (debug)
    fprintf(stderr,"children=%d, storedChildren=%d\n",
	    children,storedChildren);

  struct node *n=calloc(sizeof(struct node),1);

  struct node *ret=n;

  n->count=count;

  for(i=0;i<69;i++) {
    hasCount=(69-i-children)*0xffffff/(69-i);
    isStored=(69-i-storedChildren)*0xffffff/(69-i);

    int countPresent=range_decode_symbol(c,&hasCount,2);
    if (countPresent) {
      thisCount=range_decode_equiprobable(c,(count-progressiveCount)+1);
      if (debug)
	fprintf(stderr,"  decoded %d of %d for '%c'\n",thisCount,
		(count-progressiveCount)+1,chars[i]);
      progressiveCount+=thisCount;
      children--;
      n->counts[i]=thisCount;
    }
    
    int addrP=range_decode_symbol(c,&isStored,2);
    if (addrP) {
      childAddress=lowAddr+range_decode_equiprobable(c,highAddr-lowAddr+1);      
      if (debug) fprintf(stderr,"    decoded addr=%d of %d (lowAddr=%d)\n",
			 childAddress-lowAddr,highAddr-lowAddr+1,lowAddr);
      lowAddr=childAddress;
      if (len>0&&chars[i]==s[len]) {
	/* Only extract children if not in dummy mode, as in dummy mode
	   the rest of the file is unlikely to be present, and so extracting
	   children will most likely result in segfault. */
	if (!h->dummyOffset) {
	  n->children[i]=extractNodeAt(s,len-1,childAddress,
					       n->counts[i],h,debug);
	  if (n->children[i])
	    {
	      fprintf(stderr,"Found deeper stats for string offset %d\n",len-1);
	      ret=n->children[i];
	      dumpNode(ret);
	    }
	}
      }
      storedChildren--;
    } else childAddress=0;
  }

  if (debug) {
    fprintf(stderr,"Extract '%s' @ 0x%x (children=%d, storedChildren=%d)\n",
	    s,nodeAddress,children,storedChildren);
    dumpNode(n);
  }
  /* c->bit_stream is provided locally, so we must free the range coder manually,
     instead of using range_coder_free() */
  c->bit_stream=NULL;
  free(c);

  return ret;
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

  struct node *n=extractNodeAt(string,len,rootNodeAddress,totalCount,h,0);
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
  if (1)
    printf("extractVector('%s',%d,...)\n",
	   string,len-1);

  if (0) fprintf(stderr,"extractVector(%d,%s)\n",len-1,string);
  struct node *n=extractNode(string,len-1,h);
  if (0) fprintf(stderr,"  n=%p\n",n);
  if (!n) {
    fprintf(stderr,"Could not obtain any statistics (including zero-order frequencies). Broken stats data file?\n");
    fprintf(stderr,"  len=%d\n",len);
    exit(-1);
  } 

  int i;

  if (0) {
    fprintf(stderr,"probability of characters following '");
    for(i=0;i<len;i++) fprintf(stderr,"%c",string[i]);
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
