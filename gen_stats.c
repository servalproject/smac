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
#include "packed_stats.h"

/* Maximum order of statistics to calculate.
   Order 5 means we model each character on the previous 4.
*/
#define MAXIMUMORDER 5

#define COUNTWORDS

struct node *nodeTree=NULL;
long long nodeCount=0;

/* 3rd - 1st order frequency statistics for all characters */
long long counts3[69][69][69];
long long counts2[69][69];
long long counts1[69];

/* Frequency of letter case in each position of a word. */
long long caseposn3[2][2][80][2]; // position in word
long long caseposn2[2][80][2]; // position in word
long long caseposn1[80][2]; // position in word
long long caseend[2]; // end of word
long long casestartofmessage[2]; // start of message
long long casestartofword2[2][2]; // case of start of word based on case of start of previous word
long long casestartofword3[2][2][2]; // case of start of word based on case of start of previous word
long long messagelengths[1024];

long long wordBreaks=0;

unsigned char chars[69]="abcdefghijklmnopqrstuvwxyz 0123456789!@#$%^&*()_+-=~`[{]}\\|;:'\"<,>.?/";
unsigned char wordChars[36]="abcdefghijklmnopqrstuvwxyz0123456789";

int charIdx(unsigned char c)
{
  int i;
  for(i=0;i<69;i++)
    if (c==chars[i]) return i;
       
  /* Not valid character -- must be encoded separately */
  return -1;
}

int charInWord(unsigned c)
{
  int i;
  int cc=tolower(c);
  for(i=0;wordChars[i];i++) if (cc==wordChars[i]) return 1;
  return 0;
}

int countChars(unsigned char *s,int len)
{
  int j=0;
  if (len>MAXIMUMORDER) len=MAXIMUMORDER;
 
  struct node **n=&nodeTree;
  while(j<len) {
    int c=charIdx(s[j]);
    if (c<0) break;
    if (!(*n)) {
      *n=calloc(sizeof(struct node),1);
      nodeCount++;
    }
    (*n)->count++;
    (*n)->counts[c]++;
    n=&(*n)->children[c];
    j++;
  }
  return 0;
}

int nodesWritten=0;
unsigned int writeNode(FILE *out,struct node *n,char *s,int threshold)
{
  nodesWritten++;
  char schild[128];
  int i;

  long long totalCount=0;

  for(i=0;i<69;i++) totalCount+=n->counts[i];
  if (totalCount!=n->count) {
    fprintf(stderr,"Sequence '%s' counts don't add up: %lld vs %lld\n",
	    s,totalCount,n->count);
  }

  if (0) fprintf(stderr,"sequence '%s' occurs %lld times.\n",s,totalCount);
  /* Don't go any deeper if the sequence is too rare */
  if (totalCount<threshold) return 0;

  int children=0;
  for(i=0;i<69;i++) 
    if (n->children[i]) children++;
      
  range_coder *c=range_new_coder(1024);

  int lastChild=-1;

  int childAddresses[69];
  int childCount=0;
  int storedChildren=0;

  /* Encode children so that we know where they live */
  int lowChild=68;
  int highChild=-1;
  for(i=0;i<69;i++) {
    childAddresses[i]=0;
    if (n->counts[i]) {
      if (n->counts[i]>=threshold) {
	snprintf(schild,128,"%s%c",s,chars[i]);
	if (n->children[i]) {
	  childAddresses[i]=writeNode(out,n->children[i],schild,threshold);
	  storedChildren++;
	}
      }
      if (i<lowChild) lowChild=i;
      if (i>highChild) highChild=i;
      childCount++;
    }
  }
  
  /* Write number of children with counts */
  range_encode_equiprobable(c,69+1,childCount);
  /* Now number of children that we are storing sub-nodes for */
  range_encode_equiprobable(c,childCount+1,storedChildren);
  /* If we have more than one child, then write the maximum numbered
     child number first, so that we can constrain the range and reduce
     entropy.  Probably interpolative coding would be better here. */
  if (childCount) {
    range_encode_equiprobable(c,69+1,highChild);
  }

  unsigned int highAddr=ftell(out);
  unsigned int lowAddr=0;

  unsigned int remainingCount=totalCount;
  int childrenRemaining=childCount;
  for(i=0;i<69;i++) {
    if (n->counts[i]) {
      snprintf(schild,128,"%s%c",s,chars[i]);
      if (0) 
	fprintf(stderr, "writing: '%s' x %d @ 0x%x\n",
		schild,n->counts[i],childAddresses[i]);
      if (childrenRemaining>1) {
	if (0)
	  fprintf(stderr,"  encoding child #%d as %d-%d of %d\n",
		  i,i,lastChild,highChild+1-lastChild);
	range_encode_equiprobable(c,highChild+1-(childrenRemaining-1)-(lastChild+1),
				  i-(lastChild+1));
      }
      childrenRemaining--;
      lastChild=i;
      if (childrenRemaining) {
	if (0) fprintf(stderr,":  writing %d of %d count\n",
		       n->counts[i],remainingCount+1);
	range_encode_equiprobable(c,remainingCount+1,n->counts[i]);
      }
      remainingCount-=n->counts[i];
      
      if (childAddresses[i]) {
	range_encode_equiprobable(c,2,1);
	
	/* Encode address of child node compactly.
	   For starters, we know that it must preceed us in the bit stream.
	   We also know that we write them in order, so once we know the address
	   of a previous one, we can narrow the range further. */
	range_encode_equiprobable(c,highAddr-lowAddr+1,childAddresses[i]-lowAddr);
	lowAddr=childAddresses[i];
      } else 
	range_encode_equiprobable(c,2,0);
    }
  }
  range_conclude(c);

  if (remainingCount) {
    fprintf(stderr,"'%s' Count incomplete: %d of %lld not accounted for.\n",
	    s,remainingCount,totalCount);
  }
  
  unsigned int addr = ftello(out);
  int bytes=c->bits_used>>3;
  if (c->bits_used&7) bytes++;
  fwrite(c->bit_stream,bytes,1,out);

  if (0)
    fprintf(stderr,"write: childCount=%d, storedChildren=%d, highChild=%d\n",
	    childCount,storedChildren,highChild);

  /* Verify */
  {
    off_t fileOffset=ftello(out);
    unsigned char data[8192];
    fseek(out,addr,SEEK_SET);
    int bytes=fread(data,1,8192,out);
    struct node *v=extractNodeAt("",addr,n->count,out);
    int i;
    int error=0;
    for(i=0;i<69;i++)
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
      for(i=0;i<bytes;i++) fprintf(stderr," %02x",data[i]);
      fprintf(stderr,"\n");
      exit(-1);
    }
    if (!strcmp(s,"h")) {
      fprintf(stderr,"%s 0x%x (%f bits)\n",s,addr,c->entropy);
      dumpNode(v);
    }
    free(v);
    fseek(out,fileOffset,SEEK_SET);
  }
  range_coder_free(c);

  return addr;
}

int rescaleCounts(struct node *n,double f)
{
  int i;
  n->count/=f;
  if (n->count>0xffffff) { fprintf(stderr,"Rescaling failed (1).\n"); exit(-1); }
  for(i=0;i<69;i++) {
    n->counts[i]/=f;
    if (n->counts[i]>0xffffff)
      { fprintf(stderr,"Rescaling failed (2).\n"); exit(-1); }  
    if (n->children[i]) rescaleCounts(n->children[i],f);
  }
  return 0;
}

int dumpVariableOrderStats()
{
  FILE *out=fopen("stats.dat","w+");
  if (!out) {
    fprintf(stderr,"Could not write to stats.dat");
    return -1;
  }

  /* Normalise counts if required */
  if (nodeTree->count>0xffffff) {
    double factor=nodeTree->count*1.0/0xffffff;
    fprintf(stderr,"Dividing all counts by %.1f\n",factor);
    rescaleCounts(nodeTree,factor);
  }

  /* Keep space for our header */
  fprintf(out,"STA1XXXXYYYY");

  unsigned int topNodeAddress=writeNode(out,nodeTree,"",
					100 /* minimum frequency to count */);

  fseek(out,4,SEEK_SET);
  fputc((topNodeAddress>>24)&0xff,out);
  fputc((topNodeAddress>>16)&0xff,out);
  fputc((topNodeAddress>> 8)&0xff,out);
  fputc((topNodeAddress>> 0)&0xff,out);

  unsigned int totalCount=0;
  int i;
  for(i=0;i<69;i++) totalCount+=nodeTree->counts[i];
  fputc((totalCount>>24)&0xff,out);
  fputc((totalCount>>16)&0xff,out);
  fputc((totalCount>> 8)&0xff,out);
  fputc((totalCount>> 0)&0xff,out);

  fprintf(stderr,"Wrote %d nodes\n",nodesWritten);

  unsigned int v[69];
  extractVector("http",strlen("http"),out,v);
  extractVector("kljadfasdf",strlen("kljadfasdf"),out,v);
  extractVector("/",strlen("/"),out,v);
  extractVector("",strlen(""),out,v);

  fclose(out);

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

double entropyOfSymbol(long long v[69],int symbol)
{
  int i;
  long long total=0;
  for(i=0;i<69;i++) total+=v[i]?v[i]:1;
  double entropy=-log((v[symbol]?v[symbol]:1)*1.0/total)/log(2);
  // fprintf(stderr,"Entropy of %c = %f\n",chars[symbol],entropy);
  return entropy;
}

double entropyOfWord(char *word)
{
  double entropyWord=entropyOfSymbol(counts1,charIdx(word[0]));
if (word[1]) {
  entropyWord+=entropyOfSymbol(counts2[charIdx(word[0])],charIdx(word[1]));
  int j;
  for(j=2;word[j];j++) {
    entropyWord
      +=entropyOfSymbol(counts3[charIdx(word[j-2])][charIdx(word[j-1])],
			charIdx(word[j]));
  }	
 }
 return entropyWord;
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

int filterWords(FILE *f)
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
	entropyWord=entropyOfWord(word);
	
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

int writeMessageStats()
{
  FILE *f=fopen("message_stats.c","w");

  int i,j,k;

  fprintf(f,"unsigned int char_freqs3[69][69][69]={\n");
  for(i=0;i<69;i++) {
    fprintf(f,"  {\n");
    for(j=0;j<69;j++) {
      int rowCount=0;
      double total=0;
      for(k=0;k<69;k++) { 
	if (!counts3[i][j][k]) counts3[i][j][k]=1;
	rowCount+=counts3[i][j][k];
      }
      fprintf(f,"      /* %c %c */ {",chars[i],chars[j]);
      for(k=0;k<69;k++) {
	total+=counts3[i][j][k]*1.0*0xffffff/rowCount;
	fprintf(f,"0x%x",(unsigned int)total);
	if (k<(69-1)) fprintf(f,",");
      }
      fprintf(f,"},\n");
    }
    fprintf(f,"   },\n");
  }
  fprintf(f,"};\n");
  
  fprintf(f,"\nunsigned int char_freqs2[69][69]={\n");
  for(j=0;j<69;j++) {
    int rowCount=0;
    double total=0;
    for(k=0;k<69;k++) { 
      if (!counts2[j][k]) counts2[j][k]=1;
      rowCount+=counts2[j][k];
    }
    fprintf(f,"      /* %c */ {",chars[j]);
    for(k=0;k<69;k++) {
      total+=counts2[j][k]*1.0*0xffffff/rowCount;
      fprintf(f,"0x%x",(unsigned int)total);
      if (k<(69-1)) fprintf(f,",");
    }
    fprintf(f,"},\n");
  }
  fprintf(f,"};\n");
  
  fprintf(f,"\nunsigned int char_freqs1[69]={\n");
  {
    int rowCount=0;
    double total=0;
    for(k=0;k<69;k++) { 
      if (!counts1[k]) counts1[k]=1;
      rowCount+=counts1[k];
    }
    for(k=0;k<69;k++) {
      total+=counts1[k]*1.0/rowCount;
      fprintf(f,"0x%x",(unsigned int)(total*0xffffff));
      if (k<(69-1)) fprintf(f,",");
    }
    fprintf(f,"};\n");  
  }

  fprintf(f,"\nunsigned int casestartofmessage[1][1]={{");
  {
    int rowCount=0;
    for(k=0;k<2;k++) {
      if (!caseend[k]) casestartofmessage[k]=1;
      rowCount+=casestartofmessage[k];
    }
    for(k=0;k<(2-1);k++) {
      fprintf(f,"%.6f",casestartofmessage[k]*1.0*0xffffff/rowCount);
      if (k<(2-1)) fprintf(f,",");
    }
    fprintf(f,"}};\n");  
  }

  fprintf(f,"\nunsigned int casestartofword2[2][1]={");
  for(j=0;j<2;j++) {
    int rowCount=0;
    for(k=0;k<2;k++) {
      if (!casestartofword2[j][k]) casestartofword2[j][k]=1;
      rowCount+=casestartofword2[j][k];    
    }
    k=0;
    fprintf(f,"{0x%x}",(unsigned int)(casestartofword2[j][k]*1.0*0xffffff/rowCount));
    if (j<(2-1)) fprintf(f,",");
    
    fprintf(f,"\n");
  }
  fprintf(f,"};\n");

  fprintf(f,"\nunsigned int casestartofword3[2][2][1]={");
  for(i=0;i<2;i++) {
    fprintf(f,"  {\n");
    for(j=0;j<2;j++) {
      int rowCount=0;
      for(k=0;k<2;k++) {
	if (!casestartofword3[i][j][k]) casestartofword3[i][j][k]=1;
	rowCount+=casestartofword3[i][j][k];    
      }
      k=0;
      fprintf(f,"    {0x%x}",(unsigned int)(casestartofword3[i][j][k]*1.0*0xffffff/rowCount));
      if (j<(2-1)) fprintf(f,",");
      
      fprintf(f,"\n");
    }
    fprintf(f,"  },\n");
  }
  fprintf(f,"};\n");

  fprintf(f,"\nunsigned int caseend1[1][1]={{");
  {
    int rowCount=0;
    for(k=0;k<2;k++) {
      if (!caseend[k]) caseend[k]=1;
      rowCount+=caseend[k];
    }
    for(k=0;k<(2-1);k++) {
      fprintf(f,"%.6f",caseend[k]*1.0*0xffffff/rowCount);
      if (k<(2-1)) fprintf(f,",");
    }
    fprintf(f,"}};\n");  
  }

  fprintf(f,"\nunsigned int caseposn1[80][1]={\n");
  for(j=0;j<80;j++) {
    int rowCount=0;
    for(k=0;k<2;k++) {
      if (!caseposn1[j][k]) caseposn1[j][k]=1;
      rowCount+=caseposn1[j][k];    
    }
    fprintf(f,"      /* %dth char of word */ {",j);
    k=0;
    fprintf(f,"0x%x}",(unsigned int)(caseposn1[j][k]*1.0*0xffffff/rowCount));
    if (j<(80-1)) fprintf(f,",");
    
    fprintf(f,"\n");
  }
  fprintf(f,"};\n");
  
  fprintf(f,"\nunsigned int caseposn2[2][80][1]={\n");
  for(i=0;i<2;i++) {
    fprintf(f,"  {\n");
    for(j=0;j<80;j++) {
      int rowCount=0;
      for(k=0;k<2;k++) {
	if (!caseposn2[i][j][k]) caseposn2[i][j][k]=1;
	rowCount+=caseposn2[i][j][k];    
      }
      fprintf(f,"      /* %dth char of word */ {",j);
      k=0;
      fprintf(f,"0x%x",(unsigned int)(caseposn2[i][j][k]*1.0*0xffffff/rowCount));
      fprintf(f,"}");
      if (j<(80-1)) fprintf(f,",");
      
      fprintf(f,"\n");
    }
    fprintf(f,"  },\n");
  }
  fprintf(f,"};\n");

  fprintf(f,"\nunsigned int messagelengths[1024]={\n");
  {
    int rowCount=0;
    double total=0;
    for(k=0;k<1024;k++) { 
      if (!messagelengths[k]) messagelengths[k]=1;
      rowCount+=messagelengths[k];
    }
    for(k=0;k<1024;k++) {
      total+=messagelengths[k]*1.0/rowCount;
      fprintf(f,"   /* length = %d */ 0x%x",k,(unsigned int)(total*0xffffff));
      if (k<(1024-1)) fprintf(f,",");
      fprintf(f,"\n");
    }
    fprintf(f,"};\n");  
  }

  listAllWords();
  filterWords(f);
  writeWords(f);

return 0;
}

int main(int argc,char **argv)
{
  char line[8192];

  int i,j,k;
  /* Zero statistics */
  for(i=0;i<69;i++) for(j=0;j<69;j++) for(k=0;k<69;k++) counts3[i][j][k]=0;
  for(j=0;j<69;j++) for(k=0;k<69;k++) counts2[j][k]=0;
  for(k=0;k<69;k++) counts1[k]=0;
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

  int argn=1;
  FILE *f=stdin;

  if (argc>1) {
    f=fopen(argv[argn++],"r");
    fprintf(stderr,"Reading corpora from command line options.\n");
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

    for(i=0;i<strlen(line)-1;i++)
      {       
	countChars((unsigned char *)&line[i],strlen(&line[i]));

	if (line[i]=='\\') {
	  switch(line[i+1]) {
	    
	  case 'r': i++; line[i]='\r'; break;
	  case 'n': i++; line[i]='\n'; break;
	  case '\\': i++; line[i]='\\'; break;

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
#ifdef COUNTWORDS
	    countWord(word,strlen(word));
#endif
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

  dumpVariableOrderStats();

  fprintf(stderr,"\nWriting letter frequency statistics.\n");

  writeMessageStats();

  return 0;
}
