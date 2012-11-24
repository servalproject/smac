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

#include "arithmetic.h"
#include "charset.h"
#include "packed_stats.h"

struct node *nodeTree=NULL;
long long nodeCount=0;

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

int dumpTree(struct node *n,int indent)
{
  if (indent==0) fprintf(stderr,"dumpTree:\n");
  int i;
  for(i=0;i<CHARCOUNT;i++) {
    if (n->counts[i]) {
      fprintf(stderr,"%s'%c' x%d\n",
	      &"                                        "[40-indent],
	      chars[i],n->counts[i]);
    }
    if (n->children[i]) {
      fprintf(stderr,"%s'%c':\n",
	      &"                                        "[40-indent],
	      chars[i]);
      dumpTree(n->children[i],indent+2);
    }
  }
  return 0;
}

int countChars(unsigned char *s,int len,int maximumOrder)
{
  int j;

  struct node **n=&nodeTree;
  
  if (!*n) *n=calloc(sizeof(struct node),1);

  if (0) fprintf(stderr,"Count occurrence of '%s' (len=%d)\n",s,len);

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
      *n=calloc(sizeof(struct node),1);
      if (0) fprintf(stderr,"    -- create node %p\n",*n);
      nodeCount++;
      if (!(nodeCount&0x3fff)) fprintf(stderr,"[%lld]",nodeCount);
    }
    (*n)->count++;
    (*n)->counts[symbol]++;
    if (0) 
      fprintf(stderr,"   incrementing count of %d (0x%02x = '%c') @ offset=%d *n=%p (now %d)\n",
	      symbol,s[len-1],s[len-1],j,*n,(*n)->counts[symbol]);
    n=&(*n)->children[c];
    if (order>=maximumOrder) 
      {
	break;
      }
    order++;
  }

  if (!(*n)) {
    *n=calloc(sizeof(struct node),1);
    if (0) fprintf(stderr,"    -- create terminal node %p\n",*n);
    nodeCount++;
  }
  (*n)->count++;
  (*n)->counts[symbol]++;

  return 0;
}

int nodesWritten=0;
unsigned int writeNode(FILE *out,struct node *n,char *s,
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

  for(i=0;i<CHARCOUNT;i++) totalCount+=n->counts[i];
  if (totalCount!=n->count) {
    fprintf(stderr,"Sequence '%s' counts don't add up: %lld vs %lld\n",
	    s,totalCount,n->count);
  }

  if (debug) fprintf(stderr,"sequence '%s' occurs %lld times (%d inc. terminals).\n",
		 s,totalCount,totalCountIncludingTerminations);
  /* Don't go any deeper if the sequence is too rare */
  if (totalCount<threshold) return 0;

  int children=0;
  for(i=0;i<CHARCOUNT;i++) 
    if (n->children[i]) children++;
      
  range_coder *c=range_new_coder(1024);

  int childAddresses[CHARCOUNT];
  int childCount=0;
  int storedChildren=0;

  /* Encode children first so that we know where they live */
  for(i=0;i<CHARCOUNT;i++) {
    childAddresses[i]=0;

    if (n->children[i]&&n->children[i]->count>=threshold) {
      if (0) fprintf(stderr,"n->children[%d]->count=%lld\n",i,n->children[i]->count);
      snprintf(schild,128,"%s%c",s,chars[i]);
      childAddresses[i]=writeNode(out,n->children[i],schild,totalCount,threshold);
      storedChildren++;
    }
    if (n->counts[i]) {
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

    if (n->counts[i]) {
      snprintf(schild,128,"%c%s",chars[i],s);
      if (debug) 
	fprintf(stderr, "writing: '%s' x %d\n",
		schild,n->counts[i]);
      if (debug) fprintf(stderr,":  writing %d of %d count for '%c'\n",
			 n->counts[i],remainingCount+1,chars[i]);

      range_encode_symbol(c,&hasCount,2,1);
      range_encode_equiprobable(c,remainingCount+1,n->counts[i]);

      remainingCount-=n->counts[i];
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
    h.use_cache=0;
    if (0) fprintf(stderr,"verifying node @ 0x%x\n",addr);
    struct node *v=extractNodeAt("",0,addr,totalCountIncludingTerminations,&h,
				 0 /* don't extract whole tree */,debug);

    int i;
    int error=0;
    for(i=0;i<CHARCOUNT;i++)
      {
	if (v->counts[i]!=n->counts[i]) {
	  if (!error) {
	    fprintf(stderr,"Verify error writing node for '%s'\n",s);
	    fprintf(stderr,"  n->count=%lld, totalCount=%lld\n",
		    n->count,totalCount);
	  }
	  fprintf(stderr,"  '%c' (%d) : %d versus %d written.\n",
		  chars[i],i,v->counts[i],n->counts[i]);
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

int rescaleCounts(struct node *n,double f)
{
  int i;
  n->count=0;
  if (n->count>=(0xffffff-CHARCOUNT)) { fprintf(stderr,"Rescaling failed (1).\n"); exit(-1); }
  for(i=0;i<CHARCOUNT;i++) {
    if (n->counts[i]) {
      n->counts[i]/=f;
      if (n->counts[i]==0) n->counts[i]=1;
    }
    n->count+=n->counts[i];
    if (n->counts[i]>=(0xffffff-CHARCOUNT))
      { fprintf(stderr,"Rescaling failed (2).\n"); exit(-1); }  
    if (n->children[i]) rescaleCounts(n->children[i],f);
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
  fprintf(out,"STA1XXXXYYYYZ");

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
     (2nd order) */
  for(i=0;i<80;i++)
    for(j=0;j<2;j++) {
      tally=caseposn2[j][i][0]+caseposn2[j][i][1];
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

  unsigned int totalCount=0;
  for(i=0;i<CHARCOUNT;i++) totalCount+=nodeTree->counts[i];

  /* Rewrite header bytes with final values */
  fseek(out,4,SEEK_SET);
  writeInt(out,topNodeAddress);
  writeInt(out,totalCount);
  fputc(maximumOrder+1,out);
  

  fclose(out);

  fprintf(stderr,"Wrote %d nodes\n",nodesWritten);

  stats_handle *h=stats_new_handle(filename);
  if (!h) {
    fprintf(stderr,"Failed to load stats file '%s'\n",filename);
    exit(-1);
  }

  /* some simple tests */
  struct probability_vector *v;
  v=extractVector("http",strlen("http"),h);
  // vectorReport("http",v,charIdx(':'));
  v=extractVector("",strlen(""),h);

  stats_load_tree(h);

  v=extractVector("http",strlen("http"),h);
  // vectorReport("http",v,charIdx(':'));
  v=extractVector("",strlen(""),h);
  
  stats_handle_free(h);

  return 0;
}

int sortWordList(int alphaP);

char **words;
int *wordCounts;
int wordCount=0;
int totalWords=0;


int distributeWordCounts(char *word_in,int count,int w)
{
  char word[1024];
  strcpy(word,word_in);

  int i;
  int bestWord,bestLen;
 tryagain:
  bestWord=-1;
  bestLen=0;

  int wordLen=strlen(word);

  /* Assumes word list is sorted with sortWordList(0) 
     (sorted by length, then reverse lexographical) so
     that we can short-cut this search. */
  for(i=w+1;i<wordCount;i++) 
    if (i!=w) {
      int len=strlen(words[i]);
      if (len>wordLen) continue;
      int d=strncmp(words[i],word,len);
      if (!d) {
	if (len>bestLen&&len>2) {
	  bestLen=len;
	  bestWord=i;
	  break;
	}
      } else 
	break;
    }

  if (bestLen>0) {
    if (0) fprintf(stderr,"Distributing counts for #%d/%d %s to %s\n",
		   w,wordCount,word,words[bestWord]);
    wordCounts[bestWord]+=count;
    strcpy(word,&word[bestLen]);
    goto tryagain;
  }
  
  return 0;
}

struct wordInstance {
  char *word;
  int count;
  struct wordInstance *left;
  struct wordInstance *right;
} wordInstance;

struct wordInstance *wordTree=NULL;

int unsortedFrom=0;
int countWord(char *word_in,int len)
{
  if (len<1) return 0;

  char word[1024];
  strcpy(word,word_in);
  word[len]=0;

  totalWords++;

  struct wordInstance **wi=&wordTree;
  while (*wi!=NULL) {
    int d=strcmp(word,(*wi)->word);
    if (d==0) {
      (*wi)->count++;
      return 0;
    }
    else if (d>0) wi=&(*wi)->left;
    else wi=&(*wi)->right;
  }

  if (!*wi) {
    *wi=calloc(sizeof(struct wordInstance),1);
    (*wi)->word=strdup(word);
    (*wi)->count=1;
    wordCount++;
  }
   
  return 0;
}

int wordNumber=0;

int listWords(struct wordInstance *n)
{
  while(n) {
    if (n->left) listWords(n->left);
    if (wordNumber>=wordCount) {
      fprintf(stderr,"word list seems to be longer than itself.\n");
    }
    words[wordNumber]=n->word;
    wordCounts[wordNumber++]=n->count;
    n=n->right;
  }
  return 0;
}

int listAllWords()
{
  fprintf(stderr,"Listing %d words.\n",wordCount);
  words=calloc(sizeof(char *),wordCount);
  wordCounts=calloc(sizeof(int),wordCount);
  listWords(wordTree);
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

double entropyOfWord3(char *word)
{
  double entropyWord=entropyOfSymbol3(counts1,charIdx(word[0]));
  if (word[1]) {
    entropyWord+=entropyOfSymbol3(counts2[charIdx(word[0])],charIdx(word[1]));
    int j;
    for(j=2;word[j];j++) {
      entropyWord
	+=entropyOfSymbol3(counts3[charIdx(word[j-2])][charIdx(word[j-1])],
			  charIdx(word[j]));
    }	
  }
  return entropyWord;
}

double entropyOfWord(char *word_in,stats_handle *h)
{
  /* Model words as if they followed a space, i.e., as though
     they are starting a word. */
  char word[1024];
  snprintf(word,1024," %s",word_in);

  double wordEntropy=0;
  int i;
  for(i=1;word[i];i++) {
    int t=word[i];
    word[i]=0;
    struct probability_vector *v=extractVector(word,i,h);
    int s=charIdx(t);
    wordEntropy+=entropyOfSymbol(v,s);
    word[i]=t;
  }
  return wordEntropy;
}

int cmp_words(const void *a,const void *b)
{
  int aa=*(int *)a;
  int bb=*(int *)b;

  /* order by length, and then by reverse lexical order */
  if (strlen(words[bb])>strlen(words[aa])) return 1;
  if (strlen(words[bb])<strlen(words[aa])) return -1;
  return strcmp(words[bb],words[aa]);
}

int cmp_words_alpha(const void *a,const void *b)
{
  int aa=*(int *)a;
  int bb=*(int *)b;

  /* order by length, and then by reverse lexical order */
  return strcmp(words[aa],words[bb]);
}

int sortWordList(int alphaP)
{
  int v[wordCount];
  int counts[wordCount];
  char *names[wordCount];
  int i;
  for(i=0;i<wordCount;i++) v[i]=i;

  /* work out correct order */
  if (alphaP)
    qsort(v,wordCount,sizeof(int),cmp_words_alpha); 
  else
    qsort(v,wordCount,sizeof(int),cmp_words); 
  
  /* now regenerate lists */
  for(i=0;i<wordCount;i++)
    {
      counts[i]=wordCounts[v[i]];
      names[i]=words[v[i]];
    }
  for(i=0;i<wordCount;i++)
    {
      wordCounts[i]=counts[i];
      words[i]=names[i];
    }

  return 0;
}

int filterWords(FILE *f,stats_handle *h,int wordModel)
{
  /* Remove words from list that occur too rarely compared with their
     length, such that it is unlikely that they will be useful for compression.
  */
  int i;

  fprintf(stderr,"Filtering word list [.=5k words]: ");

  /* Remove very rare words */
  int filtered=0;
  for(i=0;i<wordCount;i++) 
    if (wordCounts[i]<5) {
      distributeWordCounts(words[i],wordCounts[i],i); 
      if (i!=wordCount-1) {
	free(words[i]);
	words[i]=words[wordCount-1];
	wordCounts[i]=wordCounts[wordCount-1];
	i--;
      }
      wordCount--;
      filtered++;
      if (!(filtered%5000)) { fprintf(stderr,"."); fflush(stderr); }
    }

  fprintf(stderr,"\n");

  fprintf(stderr,"Culling words with low entropy that are better encoded directly: ");
  int culled=1;
  double totalSavings=0;
  while(culled>0) 
    {
      /* Sort word list by longest words first, so that we cull long words before 
	 their stems, so that the stemmed versions have a chance to be retained.
      */
      sortWordList(0);

      totalSavings=0;
      culled=0;
      /* How many word occurrences are left */
      int usefulOccurrences=0;
      for(i=0;i<wordCount;i++) usefulOccurrences+=wordCounts[i];
      double occurenceEntropy=-log(usefulOccurrences*1.0/wordBreaks)/log(2);
      double nonoccurrenceEntropy=-log((wordBreaks-usefulOccurrences)*1.0/wordBreaks)/log(2);
      nonoccurrenceEntropy*=(wordBreaks-usefulOccurrences);     
      if (0) {
	fprintf(stderr,"%d words left.\n",wordCount);
	fprintf(stderr,"  Total non-occurence penalty = %f bits.\n",
		nonoccurrenceEntropy);
	fprintf(stderr,"  Occurence penalty = %f bits (%lld word breaks, %d common word occurrences).\n",
		occurenceEntropy,
		wordBreaks,usefulOccurrences);
      }
      occurenceEntropy+=nonoccurrenceEntropy/usefulOccurrences;
      if (0)
	fprintf(stderr,"  Grossed up occurrence penalty (base + amortised non-occurrence penalty) = %f bits\n",occurenceEntropy);
      for(i=0;i<wordCount;i++) {

	double entropyOccurrence=-log(wordCounts[i]*1.0/totalWords)/log(2);
	
	/* Very crude: should use 3rd order stats we have gathered to calculate 
	   a more accurate estimate of entropy of the word. */
	double entropyWord=0;
	
	char *word=words[i];

	// Two options for modelling word entropy:  either use fixed 3rd order
	// statistics, or use variable-order model.
	if (wordModel==99) entropyWord=entropyOfWord(word,h);
	if (wordModel==3) entropyWord=entropyOfWord3(word);
	
	if ((entropyOccurrence+occurenceEntropy)<entropyWord) {
	  double savings=wordCounts[i]*(entropyWord-(entropyOccurrence+occurenceEntropy));
	  totalSavings+=savings;
	  if (0)
	    fprintf(stderr,"entropy of %s occurrences (x%d) = %f bits."
		    " Entropy of word is %f bits. Savings = %f\n",
		    words[i],wordCounts[i],entropyOccurrence,entropyWord,savings);
	} else {
	  /* Word doesn't occur often enough, or is too low entropy to encode 
	     directly. Either way, it doesn't make sense to introduce a symbol
	     for it.
	  */
	  distributeWordCounts(words[i],wordCounts[i],i); 
	  if (i!=wordCount-1) {
	    free(words[i]);
	    words[i]=words[wordCount-1];
	    wordCounts[i]=wordCounts[wordCount-1];
	    i--;
	  }
	  wordCount--;
	  culled++;
	}
      }
      if (!culled) {
	fprintf(f,"\n/* %d substitutable words in a total of %lld word breaks. */\n",
	       usefulOccurrences,wordBreaks);
	fprintf(f,"unsigned int wordSubstitutionFlag[1]={0x%x};\n",
	       (unsigned int)(usefulOccurrences*1.0/wordBreaks*0xffffff));
      } else {
	fprintf(stderr,"%d+",culled); fflush(stderr); 
      }      
    }
  fprintf(stderr,"0\nExpect to save %f bits by encoding %d common words\n",
	  totalSavings,wordCount);
  fprintf(stderr,"Word list extract:\n");
  for(i=0;i<16&&i<wordCount;i++)
    fprintf(stderr,"  %d %s\n",wordCounts[i],words[i]);
  return 0;
}

int writeWords(FILE *f)
{
  int i;
  unsigned int total=0;
  unsigned int tally=0;

  /* Sort word list alphabetically, so that it can be searched efficiently during compression */
  fprintf(stderr,"Sorting word list alphabetically for output.\n");
  sortWordList(1);
  
  for(i=0;i<10&&i<wordCount;i++) fprintf(stderr,"  %d %s\n",wordCounts[i],words[i]);


  fprintf(f,"\nint wordCount=%d;\n",wordCount);	 
  fprintf(f,"char *wordList[]={\n");
  for(i=0;i<wordCount;i++) { 
    fprintf(f,"\"%s\"",words[i]); 
    if (i<(wordCount-1)) fprintf(f,",");
    total+=wordCounts[i]; 
    if (!(i&7)) fprintf(f,"\n");
  }
  fprintf(f,"};\n\n");
  fprintf(f,"unsigned int wordFrequencies[]={\n");
  for(i=0;i<(wordCount-1);i++) {
    tally+=wordCounts[i];   
    fprintf(f,"0x%x",(unsigned int)(tally*1.0*0xffffff/total));
    if (i<(wordCount-2)) fprintf(f,",");
    if (!(i&7)) fprintf(f,"\n");
  }
  fprintf(f,"};\n");
  return 0;
}

int writeMessageStats(int wordModel,char *filename)
{
  FILE *f=fopen("message_stats.c","w");

  listAllWords();
  stats_handle *h=stats_new_handle(filename);
  filterWords(f,h,wordModel);
  writeWords(f);

return 0;
}

int main(int argc,char **argv)
{
  char line[8192];

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

  line[0]=0; fgets(line,8192,f);
  int lineCount=0;
  int wordPosn=-1;
  char word[1024];

  int wordCase[8]; for(i=0;i<8;i++) wordCase[i]=0;
  int wordCount=0;

  fprintf(stderr,"Reading corpus [.=5k lines]: ");
  while(line[0]) {    

    int som=1;
    lineCount++;
    int c1=charIdx(' ');
    int c2=charIdx(' ');
    int lc=0;

    if (!(lineCount%5000)) { fprintf(stderr,"."); fflush(stderr); }

    /* record occurrance of message of this length.
       (minus one for the LF at end of line that we chop) */
    messagelengths[strlen(line)-1]++;

    /* Chop CR/LF from end of line */
    line[strlen(line)-1]=0;

    /* Insert each string suffix into the tree.
       We provide full length to the counter, because we don't know
       it's maximum order/depth of recording. */
    for(i=strlen(line);i>0;i--) {
      countChars((unsigned char *)line,i,maximumOrder);
      // dumpTree(nodeTree,0);
    }

    for(i=0;i<strlen(line)-1;i++)
      {       

	if (line[i]=='\\') {
	  switch(line[i+1]) {
	    
	  case 'r': i++; line[i]='\r'; break;
	  case 'n': i++; line[i]='\n'; break;
	  case '\\': i++; line[i]='\\'; break;
	  case '\'': line[i]='\''; break;
	  case 0: /* \ at end of line -- nothing to do */
	    
	    /* ignore ones we don't de-escape */
	  case 'u':
	    break;
	  default:
	    fprintf(stderr,"Illegal \\-escape in line #%d:\n%s",lineCount,line);
	    exit(-1);
	    break;
	  }
	}

	int wc=charInWord(line[i]);
	if (!wc) {
	  if (wordPosn>0) {
	    if (wordModel) countWord(word,strlen(word));
	  }
	  wordBreaks++;
	  wordPosn=-1; lc=0;	 
	} else {
	  if (isalpha(line[i])) {
	    wordPosn++;
	    word[wordPosn]=tolower(line[i]);
	    word[wordPosn+1]=0;
	    int upper=0;
	    if (isupper(line[i])) upper=1;
	    if (wordPosn<80) caseposn1[wordPosn][upper]++;
	    if (wordPosn<80) caseposn2[lc][wordPosn][upper]++;
	    /* note if end of word (which includes end of message,
	       implicitly detected here by finding null at end of string */
	    if (!charInWord(line[i+1])) caseend[upper]++;
	    if (isupper(line[i])) lc=1; else lc=0;
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
	if (line[i]>='A'&&line[i]<='Z') line[i]|=0x20;

	/* process if it is a valid char */
	if (charIdx(line[i])>=0) {
	  int c3=charIdx(line[i]);
	  counts3[c1][c2][c3]++;
	  counts2[c2][c3]++;
	  counts1[c3]++;
	  c1=c2; c2=c3;
	}
      }

  trynextfile:    
    line[0]=0; fgets(line,8192,f);
    if (!line[0]) {
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
  dumpVariableOrderStats(maximumOrder,5);

  fprintf(stderr,"\nWriting letter frequency statistics.\n");

  char filename[1024];
  snprintf(filename,1024,"stats-o%d-t1000.dat",maximumOrder);
  writeMessageStats(wordModel,filename);

  return 0;
}
