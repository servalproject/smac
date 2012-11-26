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
#include "charset.h"
#include "packed_stats.h"

#include "message_stats.h"

void node_free_recursive(struct node *n)
{
  int i;
  for(i=0;i<CHARCOUNT;i++) if (n->children[i]) node_free_recursive(n->children[i]);
  node_free(n);
  return;
}


void node_free(struct node *n)
{
  if (n->count==0xdeadbeef) {
    fprintf(stderr,"node double freed.\n");
    exit(-1);
  }
  n->count=0xdeadbeef;
  free(n);
  return;
}

void stats_handle_free(stats_handle *h)
{
  if (h->mmap) munmap(h->mmap,h->fileLength);
  if (h->buffer) free(h->buffer);
  if (h->bufferBitmap) free(h->bufferBitmap);
  if (h->tree) node_free_recursive(h->tree);

  free(h);
  return;
}

unsigned int read24bits(FILE *f)
{
  int i;
  int v=0;
  for(i=0;i<3;i++) v=(v<<8)|(unsigned char)fgetc(f);
  return v;
}

stats_handle *stats_new_handle(char *file)
{
  int i,j;
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
  h->maximumOrder=fgetc(h->file);
  if (1)
    fprintf(stderr,"rootNodeAddress=0x%x, totalCount=%d, maximumOrder=%d\n",
	    h->rootNodeAddress,h->totalCount,h->maximumOrder);

  /* Read in letter case prediction statistics */
  h->casestartofmessage[0][0]=read24bits(h->file);
  for(i=0;i<2;i++)
    {
      h->casestartofword2[i][0]=read24bits(h->file);
    }
  for(i=0;i<2;i++)
    for(j=0;j<2;j++)
      h->casestartofword3[i][j][0]=read24bits(h->file);
  for(i=0;i<80;i++)
    h->caseposn1[i][0]=read24bits(h->file);
  for(i=0;i<80;i++)
    for(j=0;j<2;j++)
      h->caseposn2[j][i][0]=read24bits(h->file);
  /* Read in message length stats */
  {
    /* 1024 x 24 bit values interpolative coded cannot
       exceed 4KB (typically around 1.3KB) */
    int tally=read24bits(h->file);
    range_coder *c=range_new_coder(4096);
    fread(c->bit_stream,4096,1,h->file);
    c->bit_stream_length=4096*8;
    c->low=0; c->high=0;
    range_decode_prefetch(c);
    ic_decode_recursive(h->messagelengths,1024,tally,c);
    range_coder_free(c);
    for(i=0;i<1024;i++) 
      h->messagelengths[i]=h->messagelengths[i]*1.0*0xffffff/tally;
  }
  fprintf(stderr,"Read case and message length statistics.\n");

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

int stats_load_tree(stats_handle *h)
{
  extractNodeAt("",0,h->rootNodeAddress,h->totalCount,h,
		1 /* extract all */,0);
  return 0;
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
			   stats_handle *h,int extractAllP,int debug)
{
  if (len<(0-(int)h->maximumOrder)) {
    // We are diving deeper than the maximum order that we expected to see.
    // This indicates an error.
    fprintf(stderr,"len=%d, maximumOrder=0x%x\n",len,h->maximumOrder);
    return NULL;
  }
  if (nodeAddress<700) {
    // The first 700 or so bytes are all fixed header.
    // If we have been asked to extract a node from here, it indicates an error.
    return NULL;
  }
  
  if (0) {
    if (s[len]) fprintf(stderr,"Extracting node '%c' @ 0x%x\n",s[len],nodeAddress);
    else fprintf(stderr,"Extracting root node @ 0x%x?\n",nodeAddress);
  }
 
  range_coder *c=range_new_coder(0);
  c->bit_stream=getCompressedBytes(h,nodeAddress,1024);
  c->bit_stream_length=1024*8;
  c->bits_used=0;
  c->low=0; c->high=0xffffffff;
  range_decode_prefetch(c);

  unsigned int totalCount=range_decode_equiprobable(c,count+1);
  int children=range_decode_equiprobable(c,CHARCOUNT+1);
  int storedChildren=range_decode_equiprobable(c,CHARCOUNT+1);
  unsigned int progressiveCount=0;
  unsigned int thisCount;

  unsigned int highAddr=nodeAddress;
  unsigned int lowAddr=0;
  unsigned int childAddress;
  int i;

  unsigned int hasCount=(CHARCOUNT-children)*0xffffff/CHARCOUNT;
  unsigned int isStored=(CHARCOUNT-storedChildren)*0xffffff/CHARCOUNT;

  if (debug)
    fprintf(stderr,
	    "children=%d, storedChildren=%d, count=%d, superCount=%d @ 0x%x\n",
	    children,storedChildren,totalCount,count,nodeAddress);

  struct node *n=calloc(sizeof(struct node),1);

  struct node *ret=n;
  /* If extracting all of tree, then retain pointer to root of tree */
  if (extractAllP&&(!h->tree)) h->tree=n;

  n->count=totalCount;

  for(i=0;i<CHARCOUNT;i++) {
    hasCount=(CHARCOUNT-i-children)*0xffffff/(CHARCOUNT-i);

    int countPresent=range_decode_symbol(c,&hasCount,2);
    if (countPresent) {
      thisCount=range_decode_equiprobable(c,(totalCount-progressiveCount)+1);
      if (debug)
	fprintf(stderr,"  decoded %d of %d for '%c'\n",thisCount,
		(totalCount-progressiveCount)+1,chars[i]);
      progressiveCount+=thisCount;
      children--;
      n->counts[i]=thisCount;
    } else {
      // fprintf(stderr,"  no count for '%c' %d\n",chars[i],i);
    }
  }

  if (debug) {
    int i;
    fprintf(stderr,"Extracted counts for: '");
    for(i=0;i<len;i++) fprintf(stderr,"%c",s[i]);
    fprintf(stderr,"' @ 0x%x\n",nodeAddress);
    dumpNode(n);
  }

  for(i=0;i<CHARCOUNT;i++) {
    isStored=(CHARCOUNT-i-storedChildren)*0xffffff/(CHARCOUNT-i);    
    int addrP=range_decode_symbol(c,&isStored,2);
    if (addrP) {
      childAddress=lowAddr+range_decode_equiprobable(c,highAddr-lowAddr+1);      
      if (debug) fprintf(stderr,"    decoded addr=%d of %d (lowAddr=%d)\n",
			 childAddress-lowAddr,highAddr-lowAddr+1,lowAddr);
      lowAddr=childAddress;     
      if (extractAllP||(len>0&&chars[i]==s[len-1])) {
	/* Only extract children if not in dummy mode, as in dummy mode
	   the rest of the file is unlikely to be present, and so extracting
	   children will most likely result in segfault. */
	if (!h->dummyOffset) {
	  n->children[i]=extractNodeAt(s,len-1,childAddress,
				       progressiveCount,h,
				       extractAllP,debug);
	  if (n->children[i])
	    {
	      if (0) 
		fprintf(stderr,"Found deeper stats for string offset %d\n",len-1);
	      if (!extractAllP) {
		ret=n->children[i];
		node_free(n);
	      }
	      if (0) dumpNode(ret);
	    }
	}
      }
      storedChildren--;
    } else childAddress=0;
  }

  if (debug) {
    int i;
    fprintf(stderr,"Extracted children for: '");
    for(i=0;i<len;i++) fprintf(stderr,"%c",s[i]);
    fprintf(stderr,"' @ 0x%x\n",nodeAddress);
    dumpNode(ret);
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
  for(i=0;i<CHARCOUNT;i++) {
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

  struct node *n;

  if (h->tree) {
    n=h->tree;
    for(i=len-1;i>=0;i--) {
      if (!n->children[charIdx(string[i])]) return n;
      n=n->children[charIdx(string[i])];
    }
    return n;
  } else {
    n=extractNodeAt(string,len,rootNodeAddress,totalCount,h,0,0);
  }
  if (0) {
    fprintf(stderr,"n=%p\n",n);
    fflush(stderr);
  }

  if (0) {
    fprintf(stderr,"stats for what follows '%s' @ 0x%p\n",
	    &string[len-i],n);
    dumpNode(n);
  }
    
  if (n==NULL)
    {
      if (1) {
	fprintf(stderr,"No statistics for what comes after '");
	int j;
	for(j=0;j<=i;j++) fprintf(stderr,"%c",string[len-j-1]);
	fprintf(stderr,"'\n");
      }
      
      return NULL;
    } 

  return n;
}

struct probability_vector *extractVector(unsigned short *string,int len,
					 stats_handle *h)
{

  struct vector_cache **cache=&h->cache;
  struct probability_vector *v=&h->vector;
  
  if (0)
    fprintf(stderr,"extractVector('%s',%d,...)\n",
	   string,len);
  
  if (string[len]) {
    fprintf(stderr,"search strings for extractVector() must be null-terminated.\n");
    exit(-1);
  }

  /* Try to find in cache first */
  if (h->use_cache) {
    struct vector_cache **n=cache;
    char *compareString=string;
    int compareLen=len;
    if (compareLen>h->maximumOrder) {
      compareString+=(compareLen-h->maximumOrder);
      compareLen=h->maximumOrder;
    }
    while (*n) {
      int c=strcmp((*n)->string,compareString);
      if (c<0) n=&(*n)->left;
      else if (c>0) n=&(*n)->right;
      else break;
    }
    if (*n) {
      fprintf(stderr,"Using cached vector for '%s'\n",compareString);
      return &(*n)->v;
    } else {
      /* This is where it should go in the tree */
      (*n)=calloc(sizeof(struct vector_cache),1);
      v=&(*n)->v;
      (*n)->string=strdup(compareString);
      fprintf(stderr,"Caching vector for '%s'\n",compareString);
    }
  } else {
    if (!(v)) {
      fprintf(stderr,"%s() could not work out where to put extracted vector.\n",
	      __FUNCTION__);
      exit(-1);
    }
  }

  /* Wasn't in cache, or there is no cache, so exract it */
  struct node *n=extractNode(string,len,h);
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

  int scale=0xffffff/(n->count+CHARCOUNT);
  if (scale==0) {
    fprintf(stderr,"n->count+CHARCOUNT = 0x%llx > 0xffffff - this really shouldn't happen.  Your stats.dat file is probably corrupt.\n",n->count);
    exit(-1);
  }
  int cumulative=0;
  int sum=0;

  for(i=0;i<CHARCOUNT;i++) {
    v->v[i]=cumulative+(n->counts[i]+1)*scale;
    cumulative=v->v[i];
    sum+=n->counts[i]+1;
    if (0) 
      fprintf(stderr,"  '%c' %d : 0x%06x (%d/%lld) %d (*v)[i]=%p\n",
	      chars[i],i,v->v[i],
	      n->counts[i]+1,n->count+CHARCOUNT,sum,
	      &v->v[i]);
  }

  /* Higher level nodes have already been freed, so just free this one */
  // vectorReport("extracted",v,26);
  return v;
}

double entropyOfSymbol(struct probability_vector *v,int s)
{
  double extra=0;
  int high=0x1000000;
  int low=0;
  if (s) low=v->v[s-1];
 
  // If it's a digit, then add the extra bits for encoding the number
  if (chars[s]=='0') extra=-log(0.1)/log(2);
  if (chars[s]=='U') {
    // Estimate unicode symbol cost
    fprintf(stderr,"Estimating entropy of unicode characters not yet implemented.\n");
    exit(-1);
  }
  if (s<CHARCOUNT-1) high=v->v[s];
  double p=(high-low)*1.00/0x1000000;
  return -log(p)/log(2);
}

int vectorReportShort(char *name,struct probability_vector *v,int s)
{
  int high=0x1000000;
  int low=0;
  if (s) low=v->v[s-1];
  if (s<CHARCOUNT-1) high=v->v[s];
  double percent=(high-low)*100.00/0x1000000;
  fprintf(stderr,"P[%s](%c) = %.2f%%\n",name,chars[s],percent);
  return 0;
}

int vectorReport(char *name,struct probability_vector *v,int s)
{
  int high=0x1000000;
  int low=0;
  if (s) low=v->v[s-1];
  if (s<CHARCOUNT-1) high=v->v[s];
  double percent=(high-low)*100.00/0x1000000;
  fprintf(stderr,"P[%s](%c) = %.2f%%\n",name,chars[s],percent);
  int i;
  low=0;
  for(i=0;i<CHARCOUNT;i++) {
    if (i<CHARCOUNT-1) high=v->v[i]; else high=0x1000000;
    double p=(high-low)*100.00/0x1000000;

    fprintf(stderr," '%c' %.2f%% |",chars[i]>0x1f?chars[i]:'A'+chars[i],p);
    if (i%5==4) fprintf(stderr,"\n");

    low=high;
  }
  fprintf(stderr,"\n");
  return 0;
}
