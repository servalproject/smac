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

/* 3rd - 1st order frequency statistics for all characters */
long long counts3[69][69][69];
long long counts2[69][69];
long long counts1[69];

/* Frequency of letter case in each position of a word. */
long long caseposn3[2][2][80][2]; // position in word
long long caseposn2[2][80][2]; // position in word
long long caseposn1[80][2]; // position in word
long long caseend[2]; // end of word

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

int main(int argc,char **argv)
{
  char line[8192];

  int i,j,k;
  /* Zero statistics */
  for(i=0;i<69;i++) for(j=0;j<69;j++) for(k=0;k<69;k++) counts3[i][j][k]=0;
  for(j=0;j<69;j++) for(k=0;k<69;k++) counts2[j][k]=0;
  for(k=0;k<69;k++) counts1[k]=0;
  for(i=0;i<2;i++) { caseend[i]=0;
    for(j=0;j<80;j++) { caseposn1[j][i]=0; 
      for(k=0;k<2;k++) caseposn2[k][j][i]=0;
    }
  }

  line[0]=0; fgets(line,8192,stdin);
  int lineCount=0;
  int wordPosn=0;

  while(line[0]) {
    lineCount++;
    int c1=charIdx(' ');
    int c2=charIdx(' ');
    int lc=0;
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
	  wordPosn=-1; lc=0;
	} else {
	  if (isalpha(line[i])) {
	    wordPosn++;
	    int upper=0;
	    if (isupper(line[i])) upper=1; 
	    if (wordPosn<80) caseposn1[wordPosn][upper]++;
	    if (wordPosn<80) caseposn2[lc][wordPosn][upper]++;
	    /* note if end of word (which includes end of message,
	       implicitly detected here by finding null at end of string */
	    if (!charInWord(line[i+1])) caseend[upper]++;
	    if (isupper(line[i])) lc=1; else lc=0;
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

  printf("\nunsigned int caseend1[1][1]={{");
  {
    int rowCount=0;
    for(k=0;k<2;k++) {
      if (!caseend[k]) caseend[k]=1;
      rowCount+=caseend[k];
    }
    for(k=0;k<(2-1);k++) {
      printf("%.6f",caseend[k]*1.0/rowCount);
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

  return 0;
}
