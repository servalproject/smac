/*
  (C) Paul Gardner-Stephen 2012.

  Generate variable order statistics from a sample corpus.
  Designed to run over partly-escaped tweets as obtained from extract_tweets, 
  which should be run on the output from a run of:

  https://stream.twitter.com/1.1/statuses/sample.json

  Twitter don't like people redistributing piles of tweets, so we will just
  include our summary statistics.
*/

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
#include <assert.h>

#include "arithmetic.h"
#include "charset.h"
#include "packed_stats.h"
#include "unicode.h"

/* Only allocate a few entries for nodes by default, because we expect most
   nodes to be sparse. */
#define FEW 4
struct countnode {
  unsigned char fewCountIds[FEW];
  unsigned int fewCounts[FEW];
  unsigned char fewChildIds[FEW];
  long long count;

  struct countnode *fewChildren[FEW];
  unsigned int *allCounts;
  struct countnode **allChildren;
};

struct countnode *nodeTree=NULL;
long long nodeCount=0;
long long nodeBigCountsCount=0;
long long nodeBigChildrenCount=0;

/* 3rd - 1st order frequency statistics for all characters */
unsigned int counts3[CHARCOUNT][CHARCOUNT][CHARCOUNT];
unsigned int counts2[CHARCOUNT][CHARCOUNT];
unsigned int counts1[CHARCOUNT];

/* Frequency of letter case in each position of a word. */
long long caseposn3[2][2][80][2]; // position in word
long long caseposn2[2][80][2]; // position in word
long long caseposn1[80][2]; // position in word
long long caseend[2]; // end of word
long long casestartofmessage[2]; // start of message
long long casestartofword2[2][2]; // case of start of word based on case of start of previous word
long long casestartofword3[2][2][2]; // case of start of word based on case of start of previous word
int messagelengths[1024];

long long wordBreaks=0;

long long unicode_counts[65536];
long long unicode_page_counts[512]; // = 65536 / unicode page size (128)
long long unicode_page_changes[512][513]; /* extra code is for switching back to
					     previously used code page */
int lastPage=0;  // page of last unicode character seen
int lastLastPage=0; // page of unicode character before the last one

int countUnicode(unsigned short codePoint)
{
  int codePage=codePoint/0x80;
  unicode_counts[codePoint]++;
  unicode_page_counts[codePage]++;

  /* Record statistics of code page changes */
  if (codePage!=lastPage&&codePage==lastLastPage) 
    unicode_page_changes[lastPage][512]++;
  else 
    unicode_page_changes[lastPage][codePage]++;

  lastLastPage=lastPage;
  lastPage=codePage;
  return 0;
}

int unicodeNewLine()
{
  lastPage=0;
  return 0;
}

unsigned int getCount(struct countnode *n,int s)
{
  int i;
  if (n->allCounts) return n->allCounts[s];
  for(i=0;i<FEW;i++) if (n->fewCountIds[i]==1+s) return n->fewCounts[i];
  return 0;
}
int setCount(struct countnode *n,int s, unsigned int count)
{
  int i;
  if (n->allCounts) { n->allCounts[s]=count; return 0; }
  for(i=0;i<FEW;i++) 
    if ((!n->fewCountIds[i])||n->fewCountIds[i]==1+s) 
      { 
	n->fewCountIds[i]=1+s;
	n->fewCounts[i]=count; 
	return 0;
      }
  
  nodeBigCountsCount++;
  n->allCounts=calloc(sizeof(unsigned int),CHARCOUNT);
  for(i=0;i<FEW;i++) n->allCounts[n->fewCountIds[i]-1]=n->fewCounts[i];
  n->allCounts[s]=count; 
  return 0;

}

int setChild(struct countnode *n,int s, struct countnode *child)
{
  int i;
  if (n->allChildren) { n->allChildren[s]=child; return 0; }
  for(i=0;i<FEW;i++) 
    if ((!n->fewChildIds[i])||n->fewChildIds[i]==1+s) 
      {
	n->fewChildIds[i]=1+s;
	n->fewChildren[i]=child; 
	return 0;
      }

  nodeBigChildrenCount++;
  n->allChildren=calloc(sizeof(struct countnode *),CHARCOUNT);
  for(i=0;i<FEW;i++) n->allChildren[n->fewChildIds[i]-1]=n->fewChildren[i];
  n->allChildren[s]=child; 
  return 0;
}
struct countnode **getChild(struct countnode *n,int s,int createP)
{
  int i;
  if (n->allChildren) return &n->allChildren[s];
  for(i=0;i<FEW;i++) if (n->fewChildIds[i]==1+s) return &n->fewChildren[i];
  if (createP) {
    /* no child pointer, so set it to NULL and return it by calling ourselves again */
    setChild(n,s,NULL);
    return getChild(n,s,0);
  } else
    return NULL;
}

int dumpTree(struct countnode *n,int indent)
{
  if (indent==0) fprintf(stderr,"dumpTree:\n");
  int i;
  for(i=0;i<CHARCOUNT;i++) {
    if (getCount(n,i)) {
      fprintf(stderr,"%s'%c' x%d\n",
	      &"                                        "[40-indent],
	      chars[i],getCount(n,i));
    }
    if (getChild(n,i,0)&&*getChild(n,i,0)) {
      fprintf(stderr,"%s'%c':\n",
	      &"                                        "[40-indent],
	      chars[i]);
      dumpTree(*getChild(n,i,0),indent+2);
    }
  }
  return 0;
}

int countChars(unsigned short *s,int len,int maximumOrder)
{
  int j;

  struct countnode **n=&nodeTree;
  
  if (!*n) *n=calloc(sizeof(struct countnode),1);

  /*
    Originally, we inserted strings in a forward direction, e.g., inserting
    "lease" would have nodes root->l->e->a->s->e.  
    But we also had to insert the partial strings root->e->a->s->e, root->a->s->e,
    root->s->e and root->e.
    This is necessary because there may not be enough occurrences of "lease" to 
    have the full depth stored in the compressed file.  Also, when querying on,
    e.g., "amylase" or even just a misspelling of lease, a full depth entry may
    not exist.  Also, at the beginning of a message there are not enough characters
    to provide a full-depth context. 

    So for all these reasons, we not only had to store all the partial strings, but
    also query the partial strings if a full depth match does not exist when using
    the compressed statistics file.  This contributed to very slow compression and
    decompression performance.

    If we instead insert the strings backwards, it seems that things should be
    substantially better. Now, we would insert "lease" as root->e->s->a->e->l.
    Querying any length string with any length of match will return the deepest
    statistics possible in just one query.  We also don't need to store the partial
    strings, which should reduce the size of the compressed file somewhat.

    Storing strings backwards also introduces a separation between the tree structure
    and the counts.
  */
  int order=0;
  int symbol=charIdx(tolower(s[len-1]));
  if (symbol<0) return 0;
  for(j=len-2;j>=0;j--) {
    int c=charIdx(s[j]);
    if (0) fprintf(stderr,"  %d (%c)\n",c,s[j]);
    if (c<0) break;
    if (!(*n)) {
      *n=calloc(sizeof(struct countnode),1);
      if (0) fprintf(stderr,"    -- create node %p\n",*n);
      nodeCount++;
      if (!(nodeCount&0x3fff)) 
	fprintf(stderr,"[%lld,%lld,%lld]",
		nodeCount,nodeBigCountsCount,nodeBigChildrenCount);
    }
    (*n)->count++;
    setCount((*n),symbol,getCount((*n),symbol)+1);
    if (0) 
      fprintf(stderr,"   incrementing count of %d (0x%02x = '%c') @ offset=%d *n=%p (now %d)\n",
	      symbol,s[len-1],s[len-1],j,*n,getCount((*n),symbol));
    n=getChild(*n,c,1 /* create pointer if not already existing */); 
    if (order>=maximumOrder) 
      {
	break;
      }
    order++;
  }

  if (!(*n)) {
    *n=calloc(sizeof(struct countnode),1);
    if (0) fprintf(stderr,"    -- create terminal node %p\n",*n);
    nodeCount++;
  }
  (*n)->count++;
  setCount(*n,symbol,getCount(*n,symbol)+1);

  return 0;
}

int nodesWritten=0;
unsigned int writeNode(FILE *out,struct countnode *n,char *s,
		       /* Terminations don't get counted internally in a node,
			  but are used when encoding and decoding the node,
			  so we have to pass it in here. */
		       int totalCountIncludingTerminations,int threshold)
{
  nodesWritten++;
  char schild[128];
  int i;

  long long totalCount=0;

  int debug=0;

  for(i=0;i<CHARCOUNT;i++) totalCount+=getCount(n,i);
  if (totalCount!=n->count) {
    fprintf(stderr,"Sequence '%s' counts don't add up: %lld vs %lld\n",
	    s,totalCount,n->count);
  }

  if (debug) fprintf(stderr,"sequence '%s' occurs %lld times (%d inc. terminals).\n",
		 s,totalCount,totalCountIncludingTerminations);
  /* Don't go any deeper if the sequence is too rare */
  if (totalCount<threshold) return 0;

  int children=0;
  if (n->allChildren) {
    for(i=0;i<CHARCOUNT;i++) 
      if (n->allChildren[i]) children++;
  } else {
    for(i=0;i<FEW;i++) if (n->fewChildIds[i]) children++;
  }
      
  range_coder *c=range_new_coder(1024);

  int childAddresses[CHARCOUNT];
  int childCount=0;
  int storedChildren=0;

  /* Encode children first so that we know where they live */
  for(i=0;i<CHARCOUNT;i++) {
    childAddresses[i]=0;

    struct countnode **nn;
    nn=getChild(n,i,0);
    if (nn&&*nn&&(*nn)->count>=threshold) {
      if (0) fprintf(stderr,"n->children[%d]->count=%lld\n",i,(*nn)->count);
      snprintf(schild,128,"%s%c",s,chars[i]);
      childAddresses[i]=writeNode(out,*nn,schild,totalCount,threshold);
      storedChildren++;
    }
    if (getCount(n,i)) {
      childCount++;
    }
  }
  
  /* Write total count in this node */
  range_encode_equiprobable(c,totalCountIncludingTerminations+1,totalCount);
  /* Write number of children with counts */
  range_encode_equiprobable(c,CHARCOUNT+1,childCount);
  /* Now number of children that we are storing sub-nodes for */
  range_encode_equiprobable(c,CHARCOUNT+1,storedChildren);

  unsigned int highAddr=ftell(out);
  unsigned int lowAddr=0;
  if (debug) fprintf(stderr,"  lowAddr=0x%x, highAddr=0x%x\n",lowAddr,highAddr);

  if (debug)
    fprintf(stderr,
	    "wrote: childCount=%d, storedChildren=%d, count=%lld, superCount=%d @ 0x%x\n",
	    childCount,storedChildren,totalCount,totalCountIncludingTerminations,
	    (unsigned int)ftello(out));

  unsigned int remainingCount=totalCount;
  // XXX - we can improve on these probabilities by adjusting them
  // according to the remaining number of children and stored children.
  unsigned int hasCount=(CHARCOUNT-childCount)*0xffffff/CHARCOUNT;
  unsigned int isStored=(CHARCOUNT-storedChildren)*0xffffff/CHARCOUNT;
  for(i=0;i<CHARCOUNT;i++) {
    hasCount=(CHARCOUNT-i-childCount)*0xffffff/(CHARCOUNT-i);

    if (getCount(n,i)) {
      snprintf(schild,128,"%c%s",chars[i],s);
      if (debug) 
	fprintf(stderr, "writing: '%s' x %d\n",
		schild,getCount(n,i));
      if (debug) fprintf(stderr,":  writing %d of %d count for '%c'\n",
			 getCount(n,i),remainingCount+1,chars[i]);

      range_encode_symbol(c,&hasCount,2,1);
      range_encode_equiprobable(c,remainingCount+1,getCount(n,i));

      remainingCount-=getCount(n,i);
      childCount--;
    } else {
      range_encode_symbol(c,&hasCount,2,0);
    }
  }
      
  for(i=0;i<CHARCOUNT;i++) {
    isStored=(CHARCOUNT-i-storedChildren)*0xffffff/(CHARCOUNT-i);
    if (childAddresses[i]) {
      range_encode_symbol(c,&isStored,2,1);
      if (debug) fprintf(stderr,":    writing child %d (address attached)\n",i);
	
      /* Encode address of child node compactly.
	 For starters, we know that it must preceed us in the bit stream.
	 We also know that we write them in order, so once we know the address
	 of a previous one, we can narrow the range further. */
      range_encode_equiprobable(c,highAddr-lowAddr+1,childAddresses[i]-lowAddr);
      if (debug) fprintf(stderr,":    writing addr = %d of %d (lowAddr=%d)\n",
			 childAddresses[i]-lowAddr,highAddr-lowAddr+1,lowAddr);
      lowAddr=childAddresses[i];
      storedChildren--;
    } else {
      range_encode_symbol(c,&isStored,2,0);
    }  
  }

  range_conclude(c);

  /* Unaccounted for observations are observations that terminate at this point.
     They are totall normal and expected. */
  if (debug)
    if (remainingCount) {    
      fprintf(stderr,"'%s' Count incomplete: %d of %lld not accounted for.\n",
	      s,remainingCount,totalCount);
    }
  
  unsigned int addr = ftello(out);
  int bytes=c->bits_used>>3;
  if (c->bits_used&7) bytes++;
  fwrite(c->bit_stream,bytes,1,out);

  /* Verify */
  {
    /* Make pretend stats handle to extract from */
    stats_handle h;
    h.file=(FILE*)0xdeadbeef;
    h.mmap=c->bit_stream;
    h.dummyOffset=addr;
    h.fileLength=addr+bytes;
    if (0) fprintf(stderr,"verifying node @ 0x%x\n",addr);
    struct node *v=extractNodeAt(NULL,0,addr,totalCountIncludingTerminations,&h,
				 0 /* don't extract whole tree */,debug);

    int i;
    int error=0;
    for(i=0;i<CHARCOUNT;i++)
      {
	if (v->counts[i]!=getCount(n,i)) {
	  if (!error) {
	    fprintf(stderr,"Verify error writing node for '%s'\n",s);
	    fprintf(stderr,"  n->count=%lld, totalCount=%lld\n",
		    n->count,totalCount);
	  }
	  fprintf(stderr,"  '%c' (%d) : %d versus %d written.\n",
		  chars[i],i,v->counts[i],getCount(n,i));
	  error++;
	}
      }
    if (error) {
      fprintf(stderr,"Bit stream (%d bytes):",bytes);
      for(i=0;i<bytes;i++) fprintf(stderr," %02x",c->bit_stream[i]);
      fprintf(stderr,"\n");
      exit(-1);
    }
#ifdef DEBUG
    if ((!strcmp(s,"esae"))||(!strcmp(s,"esael")))
      {
	fprintf(stderr,"%s 0x%x (%f bits) totalCountIncTerms=%d\n",
		s,addr,c->entropy,totalCountIncludingTerminations);
	dumpNode(v);
      }
#endif
    node_free(v);
  }
  range_coder_free(c);

  return addr;
}

int rescaleCounts(struct countnode *n,double f)
{
  int i;
  n->count=0;
  if (n->count>=(0xffffff-CHARCOUNT)) { fprintf(stderr,"Rescaling failed (1).\n"); exit(-1); }
  for(i=0;i<CHARCOUNT;i++) {
    if (getCount(n,i)) {
      setCount(n,i,getCount(n,i)/f);
      if (getCount(n,i)==0) setCount(n,i,1);
    }
    n->count+=getCount(n,i);
    if (getCount(n,i)>=(0xffffff-CHARCOUNT))
      { fprintf(stderr,"Rescaling failed (2).\n"); exit(-1); }  
  }
  if (n->allChildren) {
    for(i=0;i<CHARCOUNT;i++)
      if (n->allChildren[i]) rescaleCounts(n->allChildren[i],f);
  } else {
    for(i=0;i<FEW;i++)
      if (n->fewChildIds[i]) rescaleCounts(n->fewChildren[i],f);
  }
  return 0;
}

int writeInt(FILE *out,unsigned int v)
{
  fputc((v>>24)&0xff,out);
  fputc((v>>16)&0xff,out);
  fputc((v>> 8)&0xff,out);
  fputc((v>> 0)&0xff,out);
  return 0;
}

int write24bit(FILE *out,unsigned int v)
{
  fputc((v>>16)&0xff,out);
  fputc((v>> 8)&0xff,out);
  fputc((v>> 0)&0xff,out);
  return 0;
}

int writeUnicodeStats(FILE *out,int frequencyThreshold,int rootNodeAddress)
{
  /* For each code page we need:
     1. Frequency of each symbol
     2. Frequency of switch to every other code page

     Then when encountering a unicode character, and knowing the 
     previous code page, we can work out the probability of it being
     either a character in page (and apply relative frequency), and
     the probability of it being a codepoint from another page (and
     apply relative frequency of switching to each page from each page)
  */

  /* We ignore code page zero, since that is the ASCII7 characters. */
  int codePage;
  int unicodeRowAddress[512];
  int unicodeBytes=0;

  unicodeRowAddress[0]=rootNodeAddress;
  for(codePage=1;codePage<512;codePage++)
    {
      unicodeRowAddress[codePage]=unicodeRowAddress[codePage-1];
      int i;
      long long totalCount=0;
      for(i=0;i<128;i++) totalCount+=unicode_counts[codePage*128+i];
      for(i=1;i<513;i++)
	{
	  if (i!=codePage)
	    totalCount+=unicode_page_changes[codePage][i];
	}

      if (totalCount>=frequencyThreshold) {
	//	 fprintf(stderr,"%lld events for code page 0x%04x--0x%04x\n",
	//		totalCount,codePage*128,codePage*128+127);
	
	int frequencies[128+512+1];
	for(i=0;i<128;i++) frequencies[i]=unicode_counts[codePage*128+i];
	for(i=0;i<513;i++) 
	  if (i!=codePage)
	    frequencies[128+i]=unicode_page_changes[codePage][i];
	  else
	    frequencies[128+i]=0;
	
	if (totalCount>=0xfff000) {
	  // Rescale counts
	  float scaleFactor=totalCount*1.0/0xfff000;
	  for(i=0;i<(128+513);i++) frequencies[i]=frequencies[i]/scaleFactor;
	}

	// Now convert to cumulative totals for interpolative coding
	int runningTotal=0;
	for(i=0;i<128+513;i++) {
	  // add one to make sure that they are strictly increasing.
	  // also adds in our damping to prevent zero counts being considered
	  // impossible.
	  frequencies[i]=runningTotal+frequencies[i]+1; 
	  runningTotal=frequencies[i];
	}

	totalCount=runningTotal;

	// Remember where we are writing this entry
	unicodeRowAddress[codePage]=ftello(out);

	// Build compressed list of frequency information
	range_coder *c=range_new_coder(8192);
	assert(totalCount>=frequencies[128+512]);
	range_encode_equiprobable(c,0xffffff,totalCount);
	ic_encode_recursive(frequencies,128+512+1,totalCount+1,c);	
	range_conclude(c);

	// Now write compressed list to stats file
	int bytes=c->bits_used>>3;
	if (c->bits_used&7) bytes++;
	fwrite(c->bit_stream,bytes,1,out);
	//	fprintf(stderr,"Code page 0x%04x -- 0x%04x written in %d bytes.\n",
	//		codePage*128,codePage*128+127,bytes);
	range_coder_free(c);
	unicodeBytes+=bytes;
      }
    }

  // Now write out table of 511 addresses.
  unsigned int unicodeAddress=ftello(out);
  range_coder *c=range_new_coder(8192);
  int i;
  for(i=1;i<512;i++) {
    unicodeRowAddress[i]=(unicodeRowAddress[i]-rootNodeAddress)+i;
  }
  assert(unicodeAddress-rootNodeAddress+512+1>=unicodeRowAddress[511]);
  ic_encode_recursive(&unicodeRowAddress[1],511,
		      unicodeAddress-rootNodeAddress+512+1,c);
  range_conclude(c);
  int bytes=c->bits_used>>3; if (c->bits_used&7) bytes++;
  fwrite(c->bit_stream,bytes,1,out);
  range_coder_free(c);

  fprintf(stderr,"%d+%d bytes required to write 511 compressed unicode page statistics.\n",unicodeBytes,bytes);  
  
  return unicodeAddress;
}

int dumpVariableOrderStats(int maximumOrder,int frequencyThreshold)
{
  char filename[1024];
  snprintf(filename,1024,"stats-o%d-t%d.dat",maximumOrder,frequencyThreshold);

  fprintf(stderr,"Writing compressed stats file '%s'\n",filename);
  FILE *out=fopen(filename,"w+");
  if (!out) {
    fprintf(stderr,"Could not write to '%s'",filename);
    return -1;
  }

  /* Normalise counts if required */
  if (nodeTree->count>=(0xffffff-CHARCOUNT)) {
    double factor=nodeTree->count*1.0/(0xffffff-CHARCOUNT);
    fprintf(stderr,"Dividing all counts by %.1f (saw 0x%llx = %lld observations)\n",
	    factor,nodeTree->count,nodeTree->count);
    rescaleCounts(nodeTree,factor);
  }

  /* Keep space for our header */
  fprintf(out,"STA1XXXXYYYYUUUUZ");

  /* Write case statistics. No way to compress these, so just write them out. */
  unsigned int tally,vv;
  int i,j;
  /* case of first character of message */
  tally=casestartofmessage[0]+casestartofmessage[1];
  vv=casestartofmessage[0]*1.0*0xffffff/tally;
  fprintf(stderr,"casestartofmessage: wrote 0x%x\n",vv);
  write24bit(out,vv);
  /* case of first character of word, based on case of first character of previous
     word, i.e., 2nd-order. */
  for(i=0;i<2;i++) {
    tally=casestartofword2[i][0]+casestartofword2[i][1];
    write24bit(out,casestartofword2[i][0]*1.0*0xffffff/tally);
  }
  /* now 3rd order case */
  for(i=0;i<2;i++)
    for(j=0;j<2;j++) {
      tally=casestartofword3[i][j][0]+casestartofword3[i][j][1];
      write24bit(out,casestartofword3[i][j][0]*1.0*0xffffff/tally);
    }
  /* case of i-th letter of a word (1st order) */
  for(i=0;i<80;i++) {
    tally=caseposn1[i][0]+caseposn1[i][1];
    write24bit(out,caseposn1[i][0]*1.0*0xffffff/tally);
  }
  /* case of i-th letter of a word, conditional on case of previous letter
     (2nd order).
     Position 0 is not valid, so don't waste space on it. */
  for(i=1;i<80;i++)
    for(j=0;j<2;j++) {
      tally=caseposn2[j][i][0]+caseposn2[j][i][1];
      if ((!caseposn2[j][i][0])||(!caseposn2[j][i][1])) 
	write24bit(out,0x7fffff);
      else 
	write24bit(out,caseposn2[j][i][0]*1.0*0xffffff/tally);
    }

  fprintf(stderr,"Wrote %d bytes of fixed header (including case prediction statistics)\n",(int)ftello(out));

  /* Write out message length probabilities.  These can be interpolatively coded. */
  {
    range_coder *c=range_new_coder(8192);
    {
      int lengths[1024];
      int tally=0;
      int cumulative=0;
      for(i=0;i<=1024;i++) {
	if (!messagelengths[i]) messagelengths[i]=1;
	tally+=messagelengths[i];
      }
      if (tally>=(0xffffff-CHARCOUNT)) {
	fprintf(stderr,"ERROR: Need to add support for rescaling message counts if training using more then 2^24-1 messages.\n");
	exit(-1);
      }
      write24bit(out,tally);
      for(i=0;i<=1024;i++) {
	cumulative+=messagelengths[i];
	lengths[i]=cumulative;
      }	
      ic_encode_recursive(lengths,1024,tally,c);
    }
    range_conclude(c);
    int bytes=(c->bits_used>>3)+((c->bits_used&7)?1:0);
    fwrite(c->bit_stream,bytes,1,out);
    fprintf(stderr,
	    "Wrote %d bytes of message length probabilities (%d bits used).\n",
	    bytes,c->bits_used);
    range_coder_free(c);
  }

  /* Write compressed data out */
  unsigned int topNodeAddress=writeNode(out,nodeTree,"",
					nodeTree->count,
					frequencyThreshold);

  unsigned int unicodeAddress
    =writeUnicodeStats(out,frequencyThreshold,topNodeAddress);

  unsigned int totalCount=0;
  for(i=0;i<CHARCOUNT;i++) totalCount+=getCount(nodeTree,i);

  /* Rewrite header bytes with final values */
  fseek(out,4,SEEK_SET);
  writeInt(out,topNodeAddress);
  writeInt(out,totalCount);
  writeInt(out,unicodeAddress);
  fputc(maximumOrder+1,out);

  fclose(out);

  snprintf(filename,1024,"stats-o%d-t%d.dat",maximumOrder,frequencyThreshold);
  fprintf(stderr,"Wrote %d nodes to '%s'\n",nodesWritten,filename);

  stats_handle *h=stats_new_handle(filename);
  if (!h) {
    fprintf(stderr,"Failed to load stats file '%s'\n",filename);
    exit(-1);
  }

  /* some simple tests */
  struct probability_vector *v;
  v=extractVector(ascii2utf16("http"),strlen("http"),h);
  // vectorReport("http",v,charIdx(':'));
  v=extractVector(ascii2utf16(""),strlen(""),h);

  stats_load_tree(h);

  v=extractVector(ascii2utf16("http"),strlen("http"),h);
  // vectorReport("http",v,charIdx(':'));
  v=extractVector(ascii2utf16(""),strlen(""),h);
  int *codePage=getUnicodeStatistics(h,0x0400/0x80);

  stats_handle_free(h);

  return 0;
}

double entropyOfSymbol3(unsigned int v[CHARCOUNT],int symbol)
{
  int i;
  long long total=0;
  for(i=0;i<CHARCOUNT;i++) total+=v[i]?v[i]:1;
  double entropy=-log((v[symbol]?v[symbol]:1)*1.0/total)/log(2);
  // fprintf(stderr,"Entropy of %c = %f\n",chars[symbol],entropy);
  return entropy;
}

int main(int argc,char **argv)
{
  unsigned char utf8line[8192];
  int utf8len;
  unsigned short utf16line[8192];
  int utf16len;

  int i,j,k;
  /* Zero statistics */
  for(i=0;i<CHARCOUNT;i++) for(j=0;j<CHARCOUNT;j++) for(k=0;k<CHARCOUNT;k++) counts3[i][j][k]=0;
  for(j=0;j<CHARCOUNT;j++) for(k=0;k<CHARCOUNT;k++) counts2[j][k]=0;
  for(k=0;k<CHARCOUNT;k++) counts1[k]=0;
  for(i=0;i<2;i++) { caseend[i]=0; casestartofmessage[i]=0;
    for(k=0;k<2;k++) {
      casestartofword2[i][k]=0;
      for(j=0;j<2;j++) casestartofword3[i][j][k]=0;
    }
    for(j=0;j<80;j++) { caseposn1[j][i]=0; 
      for(k=0;k<2;k++) caseposn2[k][j][i]=0;
    }
  }
  for(i=0;i<1024;i++) messagelengths[i]=0;
  for(i=0;i<65536;i++) unicode_counts[i]=0;
  for(i=0;i<512;i++) unicode_page_counts[i]=0;
  for(i=0;i<512;i++) for(j=0;j<513;j++) unicode_page_changes[i][j]=0;

  if (argc<4) {
    fprintf(stderr,"usage: gen_stats <maximum order> <word model> [training_corpus ...]\n");
    fprintf(stderr,"       maximum order - length of preceeding string used to bin statistics.\n");
    fprintf(stderr,"                       Useful values: 1 - 6\n");
    fprintf(stderr,"          word model - 0=no word list (only supported option)\n");
    fprintf(stderr,"                       3=build using 3rd order entropy estimate,\n");
    fprintf(stderr,"                       v=build using variable order entropy estimate.\n");
    fprintf(stderr,"\n");
    exit(-1);
  }

  int argn=3;
  FILE *f=stdin;
  int maximumOrder=atoi(argv[1]); 
  int wordModel=0;
  switch (argv[2][0])
    {
    case '0': wordModel=0; break;
    case '3': wordModel=3; break;
    case 'v': wordModel=99; break;
    }

  if (argc>3) {
    f=fopen(argv[argn],"r");
    if (!f) {
      fprintf(stderr,"Could not read '%s'\n",argv[argn]);
      exit(-1);
    }
    fprintf(stderr,"Reading corpora from command line options.\n");
    argn++;
  }

  utf8line[0]=0; fgets((char *)utf8line,8192,f);
  utf8len=strlen((char *)utf8line);

  int lineCount=0;
  int wordPosn=-1;
  char word[1024];

  int wordCase[8]; for(i=0;i<8;i++) wordCase[i]=0;
  int wordCount=0;

  fprintf(stderr,"Reading corpus [.=5k lines]: ");
  while(utf8len) {    

    unEscape(utf8line,&utf8len);

    int som=1;
    lineCount++;
    int c1=charIdx(' ');
    int c2=charIdx(' ');
    int lc=0;

    if (!(lineCount%5000)) { fprintf(stderr,"."); fflush(stderr); }

    /* Chop CR/LF from end of line */
    utf8line[utf8len-1]=0;
    utf8toutf16(utf8line,utf8len,utf16line,&utf16len);

    /* record occurrance of message of this length.
       (minus one for the LF at end of line that we chop) */
    messagelengths[utf16len]++;

    /* Insert each string suffix into the tree.
       We provide full length to the counter, because we don't know
       it's maximum order/depth of recording. */
    for(i=utf16len;i>0;i--) {
      countChars(utf16line,i,maximumOrder);
      // dumpTree(nodeTree,0);
    }

    unicodeNewLine();
    for(i=0;i<utf16len;i++)
      {       
	if (utf16line[i]>0x7f) countUnicode(utf16line[i]);
	//	printf("char '%c'\n",line[i]);
	int wc=charInWord(utf16line[i]);
	if (!wc) {
	  wordBreaks++;
	  wordPosn=-1; lc=0;
	  //	  printf("word break\n");
	} else {
	  if (isalpha(utf16line[i])) {
	    wordPosn++;
	    word[wordPosn]=tolower(utf16line[i]);
	    word[wordPosn+1]=0;
	    int upper=0;
	    if (isupper((char)utf16line[i])) upper=1;
	    if (wordPosn<80) caseposn1[wordPosn][upper]++;
	    if (wordPosn<80) {
	      caseposn2[lc][wordPosn][upper]++;
	      //	      printf("casposn2[%d][%d][%d]++\n",lc,wordPosn,upper);
	    }
	    /* note if end of word (which includes end of message,
	       implicitly detected here by finding null at end of string */
	    if (!charInWord(utf16line[i+1])) caseend[upper]++;
	    lc=upper;
	    if (som) {
	      casestartofmessage[upper]++;
	      som=0;
	    }
	    if (wordPosn==0) {
	      if (wordCount>0) casestartofword2[wordCase[0]][upper]++;
	      if (wordCount>1) casestartofword3[wordCase[1]][wordCase[0]][upper]++;
	      int i;
	      for(i=1;i<8;i++) wordCase[i]=wordCase[i-1];
	      wordCase[0]=upper;
	      wordCount++;
	    }
	  }
	}

	/* fold all letters to lower case */
	if (utf16line[i]>='A'&&utf16line[i]<='Z') utf16line[i]|=0x20;

	/* process if it is a valid char */
	if (charIdx(utf16line[i])>=0) {
	  int c3=charIdx(utf16line[i]);
	  counts3[c1][c2][c3]++;
	  counts2[c2][c3]++;
	  counts1[c3]++;
	  c1=c2; c2=c3;
	}
      }

  trynextfile:
    utf8line[0]=0; fgets((char *)utf8line,8192,f);
    utf8len=strlen((char *)utf8line);    
    if (!utf8line[0]) {
      fclose(f); f=NULL;
      while(f==NULL&&argn<argc) {
	f=fopen(argv[argn++],"r");	
      }
      if (f) goto trynextfile;
    }
  }
  fprintf(stderr,"\n");
  
  fprintf(stderr,"Created %lld nodes.\n",nodeCount);

  dumpVariableOrderStats(maximumOrder,1000);
  dumpVariableOrderStats(maximumOrder,500);
  dumpVariableOrderStats(maximumOrder,200);
  dumpVariableOrderStats(maximumOrder,100);
  dumpVariableOrderStats(maximumOrder,50);
  dumpVariableOrderStats(maximumOrder,20);
  dumpVariableOrderStats(maximumOrder,10);
  // dumpVariableOrderStats(maximumOrder,5);

  return 0;
}
