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

long long counts3[72][72][72];
long long counts2[72][72];
long long counts1[72];

unsigned char chars[72]="abcdefghijklmnopqrstuvwxyz 0123456789!@#$%^&*()_+-=~`[{]}\\|;:'\"<,>.?/";
int charIdx(unsigned char c)
{
  int i;
  for(i=0;i<72;i++)
    if (c==chars[i]) return i;
       
  /* Not valid character -- must be encoded separately */
  return -1;
}

int main(int argc,char **argv)
{
  char line[8192];

  int i,j,k;
  for(i=0;i<72;i++) for(j=0;j<72;j++) for(k=0;k<72;k++) counts3[i][j][k]=0;
  for(j=0;j<72;j++) for(k=0;k<72;k++) counts2[j][k]=0;
  for(k=0;k<72;k++) counts1[k]=0;

  line[0]=0; fgets(line,8192,stdin);
  int lineCount=0;
  while(line[0]) {
    lineCount++;
    int c1=charIdx(' ');
    int c2=charIdx(' ');
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

  printf("float tweet_freqs3[72][72][72]={\n");
  for(i=0;i<72;i++) {
    printf("  {\n");
    for(j=0;j<72;j++) {
      int rowCount=0;
      for(k=0;k<72;k++) { 
	if (!counts3[i][j][k]) counts3[i][j][k]=1;
	rowCount+=counts3[i][j][k];
      }
      printf("      /* %c %c */ {",chars[i],chars[j]);
      for(k=0;k<72;k++) {
	printf("%.6f",counts3[i][j][k]*1.0/rowCount);
	if (k<71) printf(",");
      }
      printf("},\n");
    }
    printf("   },\n");
  }
  printf("};\n");
  
  printf("float tweet_freqs2[72][72]={\n");
  for(j=0;j<72;j++) {
    int rowCount=0;
    for(k=0;k<72;k++) { 
      if (!counts2[j][k]) counts2[j][k]=1;
      rowCount+=counts2[j][k];
    }
    printf("      /* %c */ {",chars[j]);
    for(k=0;k<72;k++) {
      printf("%.6f",counts2[j][k]*1.0/rowCount);
      if (k<71) printf(",");
    }
    printf("},\n");
  }
  printf("};\n");
  
  printf("float tweet_freqs1[72]={\n");
  {
    int rowCount=0;
    for(k=0;k<72;k++) { 
      if (!counts1[k]) counts1[k]=1;
      rowCount+=counts1[k];
    }
    for(k=0;k<72;k++) {
      printf("%.6f",counts1[k]*1.0/rowCount);
      if (k<71) printf(",");
    }
    printf("};\n");  
  }
  
  return 0;
}
