/*
  (C) Paul Gardner-Stephen 2012.

  Generate 3rd order statistics from a sample corpus.
  Designed to run over partly-escaped tweets as obtained from extract_tweets, 
  which should be run on the output from a run of:

  https://stream.twitter.com/1.1/statuses/sample.json

  Twitter don't like people redistributing piles of tweets, so we will just
  include our summary statistics.

*/

#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <math.h>

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

#define MAXWORDS 65536
char *words[MAXWORDS];
int wordCounts[MAXWORDS];
int wordCount=0;
int totalWords=0;

int countWord(char *word,int len)
{
  if (len<1) return 0;
  //  fprintf(stderr,"saw %s\n",word);

  totalWords++;

  int i;
  for(i=0;i<wordCount;i++) if (!strcmp(word,words[i])) { wordCounts[i]++; return 0; }
  if (i<MAXWORDS) {
    words[i]=strdup(word);
    wordCounts[i]=1;
    wordCount++;
    return 0;
  } 
  /* Too many words -- replace one that has only one count */
  int end=random()%MAXWORDS;
  i=end+1;
  while(i!=end) {
    i=i%MAXWORDS;
    if (wordCounts[i]==1) {
      fprintf(stderr,"replacing %s with %s (consider increasing MAXWORDS)\n",
	      words[i],word);
      free(words[i]);
      words[i]=strdup(word);
      wordCounts[i]=1;
      return 0;
    }   
    i++;
  }
  
  fprintf(stderr,"Ran out of word space. Definitely increase MAXWORDS.\n");
  exit(-1);

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

int filterWords()
{
  /* Remove words from list that occur too rarely compared with their
     length, such that it is unlikely that they will be useful for compression.
  */
  int i;
  
  fprintf(stderr,"Filtering word list.\n");


  /* Remove very rare words */
  for(i=0;i<wordCount;i++) 
    if (wordCounts[i]<5) {
      if (i!=wordCount-1) {
	free(words[i]);
	words[i]=words[wordCount-1];
	wordCounts[i]=wordCounts[wordCount-1];
	i--;
      }
      wordCount--;
    }

  int culled=1;
  double totalSavings=0;
  while(culled>0) 
    {
      totalSavings=0;
      culled=0;
      /* How many word occurrences are left */
      int usefulOccurrences=0;
      for(i=0;i<wordCount;i++) usefulOccurrences+=wordCounts[i];
      double occurenceEntropy=-log(usefulOccurrences*1.0/wordBreaks)/log(2);
      double nonoccurrenceEntropy=-log((wordBreaks-usefulOccurrences)*1.0/wordBreaks)/log(2);
      nonoccurrenceEntropy*=(wordBreaks-usefulOccurrences);     
      fprintf(stderr,"%d words left.\n",wordCount);
      fprintf(stderr,"  Total non-occurence penalty = %f bits.\n",
	      nonoccurrenceEntropy);
      fprintf(stderr,"  Occurence penalty = %f bits (%lld word breaks, %d common word occurrences).\n",
	      occurenceEntropy,
	      wordBreaks,usefulOccurrences);
      occurenceEntropy+=nonoccurrenceEntropy/usefulOccurrences;
      fprintf(stderr,"  Grossed up occurrence penalty (base + amortised non-occurrence penalty) = %f bits\n",occurenceEntropy);
      for(i=0;i<wordCount;i++) {

	double entropyOccurrence=-log(wordCounts[i]*1.0/totalWords)/log(2);
	
	/* Very crude: should use 3rd order stats we have gathered to calculate 
	   a more accurate estimate of entropy of the word. */
	double entropyWord=0;
	
	char *word=words[i];
	entropyWord+=entropyOfSymbol(counts1,charIdx(word[0]));
	if (word[1]) {
	  entropyWord+=entropyOfSymbol(counts2[charIdx(word[0])],charIdx(word[1]));
	  int j;
	  for(j=2;word[j];j++) {
	    entropyWord
	      +=entropyOfSymbol(counts3[charIdx(word[j-2])][charIdx(word[j-1])],
				charIdx(word[j]));
	  }	
	}
	
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
	printf("\nunsigned int wordSubstitutionFlag[1]={0x%x};\n",
	       (unsigned int)(wordBreaks*1.0/usefulOccurrences*0xffffffff));
      }
    }
  fprintf(stderr,"Expect to save %f bits by encoding %d common words\n",
	  totalSavings,wordCount);
  fprintf(stderr,"Word list:\n");
  for(i=0;i<wordCount;i++)
    fprintf(stderr,"  %d %s\n",wordCounts[i],words[i]);
  return 0;
}

int writeWords()
{
  int i;
  unsigned int total=0;
  unsigned int tally=0;
  printf("\nint wordCount=%d;\n",wordCount);	 
  printf("char *wordList[]={\n");
  for(i=0;i<wordCount;i++) { 
    printf("\"%s\"",words[i]); 
    if (i<(wordCount-1)) printf(",");
    total+=wordCounts[i]; 
    if (!(i&7)) printf("\n");
  }
  printf("};\n\n");
  printf("unsigned int wordFrequencies[]={\n");
  for(i=0;i<(wordCount-1);i++) {
    tally+=wordCounts[i];   
    printf("0x%x",(unsigned int)(tally*1.0*0xffffffff/total));
    if (i<(wordCount-2)) printf(",");
    if (!(i&7)) printf("\n");
  }
  printf("};\n");
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

  line[0]=0; fgets(line,8192,stdin);
  int lineCount=0;
  int wordPosn=-1;
  char word[1024];

  int wordCase[8]; for(i=0;i<8;i++) wordCase[i]=0;
  int wordCount=0;

  while(line[0]) {
    int som=1;
    lineCount++;
    int c1=charIdx(' ');
    int c2=charIdx(' ');
    int lc=0;

    /* record occurrance of message of this length.
       (minus one for the LF at end of line that we chop) */
    messagelengths[strlen(line)-1]++;

    for(i=0;i<strlen(line)-1;i++)
      {
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
	    countWord(word,strlen(word));
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
    
    line[0]=0; fgets(line,8192,stdin);
  }

  printf("unsigned int tweet_freqs3[69][69][69]={\n");
  for(i=0;i<69;i++) {
    printf("  {\n");
    for(j=0;j<69;j++) {
      int rowCount=0;
      double total=0;
      for(k=0;k<69;k++) { 
	if (!counts3[i][j][k]) counts3[i][j][k]=1;
	rowCount+=counts3[i][j][k];
      }
      printf("      /* %c %c */ {",chars[i],chars[j]);
      for(k=0;k<69;k++) {
	total+=counts3[i][j][k]*1.0*0xffffffff/rowCount;
	printf("0x%x",(unsigned int)total);
	if (k<(69-1)) printf(",");
      }
      printf("},\n");
    }
    printf("   },\n");
  }
  printf("};\n");
  
  printf("\nunsigned int tweet_freqs2[69][69]={\n");
  for(j=0;j<69;j++) {
    int rowCount=0;
    double total=0;
    for(k=0;k<69;k++) { 
      if (!counts2[j][k]) counts2[j][k]=1;
      rowCount+=counts2[j][k];
    }
    printf("      /* %c */ {",chars[j]);
    for(k=0;k<69;k++) {
      total+=counts2[j][k]*1.0*0xffffffff/rowCount;
      printf("0x%x",(unsigned int)total);
      if (k<(69-1)) printf(",");
    }
    printf("},\n");
  }
  printf("};\n");
  
  printf("\nunsigned int tweet_freqs1[69]={\n");
  {
    int rowCount=0;
    double total=0;
    for(k=0;k<69;k++) { 
      if (!counts1[k]) counts1[k]=1;
      rowCount+=counts1[k];
    }
    for(k=0;k<69;k++) {
      total+=counts1[k]*1.0/rowCount;
      printf("0x%x",(unsigned int)(total*0xffffffff));
      if (k<(69-1)) printf(",");
    }
    printf("};\n");  
  }

  printf("\nunsigned int casestartofmessage[1][1]={{");
  {
    int rowCount=0;
    for(k=0;k<2;k++) {
      if (!caseend[k]) casestartofmessage[k]=1;
      rowCount+=casestartofmessage[k];
    }
    for(k=0;k<(2-1);k++) {
      printf("%.6f",casestartofmessage[k]*1.0*0xffffffff/rowCount);
      if (k<(2-1)) printf(",");
    }
    printf("}};\n");  
  }

  printf("\nunsigned int casestartofword2[2][1]={");
  for(j=0;j<2;j++) {
    int rowCount=0;
    for(k=0;k<2;k++) {
      if (!casestartofword2[j][k]) casestartofword2[j][k]=1;
      rowCount+=casestartofword2[j][k];    
    }
    k=0;
    printf("{0x%x}",(unsigned int)(casestartofword2[j][k]*1.0*0xffffffff/rowCount));
    if (j<(2-1)) printf(",");
    
    printf("\n");
  }
  printf("};\n");

  printf("\nunsigned int casestartofword3[2][2][1]={");
  for(i=0;i<2;i++) {
    printf("  {\n");
    for(j=0;j<2;j++) {
      int rowCount=0;
      for(k=0;k<2;k++) {
	if (!casestartofword3[i][j][k]) casestartofword3[i][j][k]=1;
	rowCount+=casestartofword3[i][j][k];    
      }
      k=0;
      printf("    {0x%x}",(unsigned int)(casestartofword3[i][j][k]*1.0*0xffffffff/rowCount));
      if (j<(2-1)) printf(",");
      
      printf("\n");
    }
    printf("  },\n");
  }
  printf("};\n");

  printf("\nunsigned int caseend1[1][1]={{");
  {
    int rowCount=0;
    for(k=0;k<2;k++) {
      if (!caseend[k]) caseend[k]=1;
      rowCount+=caseend[k];
    }
    for(k=0;k<(2-1);k++) {
      printf("%.6f",caseend[k]*1.0*0xffffffff/rowCount);
      if (k<(2-1)) printf(",");
    }
    printf("}};\n");  
  }

  printf("\nunsigned int caseposn1[80][1]={\n");
  for(j=0;j<80;j++) {
    int rowCount=0;
    for(k=0;k<2;k++) {
      if (!caseposn1[j][k]) caseposn1[j][k]=1;
      rowCount+=caseposn1[j][k];    
    }
    printf("      /* %dth char of word */ {",j);
    k=0;
    printf("0x%x}",(unsigned int)(caseposn1[j][k]*1.0*0xffffffff/rowCount));
    if (j<(80-1)) printf(",");
    
    printf("\n");
  }
  printf("};\n");
  
  printf("\nunsigned int caseposn2[2][80][1]={\n");
  for(i=0;i<2;i++) {
    printf("  {\n");
    for(j=0;j<80;j++) {
      int rowCount=0;
      for(k=0;k<2;k++) {
	if (!caseposn2[i][j][k]) caseposn2[i][j][k]=1;
	rowCount+=caseposn2[i][j][k];    
      }
      printf("      /* %dth char of word */ {",j);
      k=0;
      printf("0x%x",(unsigned int)(caseposn2[i][j][k]*1.0*0xffffffff/rowCount));
      printf("}");
      if (j<(80-1)) printf(",");
      
      printf("\n");
    }
    printf("  },\n");
  }
  printf("};\n");

  printf("\nunsigned int messagelengths[1024]={\n");
  {
    int rowCount=0;
    double total=0;
    for(k=0;k<1024;k++) { 
      if (!messagelengths[k]) messagelengths[k]=1;
      rowCount+=messagelengths[k];
    }
    for(k=0;k<1024;k++) {
      total+=messagelengths[k]*1.0/rowCount;
      printf("   /* length = %d */ 0x%x",k,(unsigned int)(total*0xffffffff));
      if (k<(1024-1)) printf(",");
      printf("\n");
    }
    printf("};\n");  
  }

  filterWords();
  writeWords();

  return 0;
}
