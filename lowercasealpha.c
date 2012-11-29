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
#include <math.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>

#include "arithmetic.h"
#include "message_stats.h"
#include "charset.h"
#include "packed_stats.h"

#undef DEBUG
#define WORDSUBSTITUTION

/*
  TODO: Unicode not yet handled.
  TODO: Currently uses flat distribution for digit probabilities.  Should use "rule of 9" or similar.
*/
int decodeLCAlphaSpace(range_coder *c,unsigned short *s,int length,stats_handle *h)
{
  FILE *stats_file=fopen("stats.dat","r");
  int c1=charIdx(' ');
  int c2=charIdx(' ');
  int c3;
  int o;
  for(o=0;o<length;o++) {
#ifdef DEBUG
    printf("so far: '%s', c1=%d(%c), c2=%d(%c)\n",s,c1,chars[c1],c2,chars[c2]);
#endif

#ifdef WORDSUBSTITUTION
    int substituted=0;
    if (!charInWord(chars[c2])) {
      /* We are at a word break, so see if we can do word substitution.
	 Either way, we must flag whether we performed word substitution */
      substituted=1-range_decode_symbol(c,wordSubstitutionFlag,2);
#ifdef DEBUG
      printf("substitution flag = %d @ offset %d\n",substituted,o);
#endif
    }
    if (substituted)
      {
	int w=range_decode_symbol(c,wordFrequencies,wordCount);
	int wordLength=strlen(wordList[w]);
	
	/* place word into output stream */
	bcopy(wordList[w],&s[o],wordLength);
	
	/* skip rest of word, but make sure we stay on track for 3rd order model
	   state. */
	o+=wordLength-1;
	if (s[o]) {
	  c1=charIdx(s[o-1]);
	  if (c1<0) { exit(-1); }
	  c2=charIdx(s[o]);
	  if (c2<0) { exit(-1); }
	}
	continue;
      } else {
#endif
      s[o]=0;
      struct probability_vector *v=extractVector(s,o,h);
      c3=range_decode_symbol(c,v->v,CHARCOUNT);
      s[o]=chars[c3];
      if (s[o]=='0') s[o]='0'+range_decode_equiprobable(c,10);
#ifdef DEBUG
      printf("  decode alpha char %d = %c\n",c3,s[o]);
#endif
#ifdef WORDSUBSTITUTION
    }
#endif
    c1=c2; c2=c3;
  }
  fclose(stats_file);
  return 0;
}

int strncmp816(char *s1,unsigned short *s2,int len)
{
  int j;
  for(j=0;j<len;j++) {
    int d=(unsigned char)s1[j]-(unsigned short)s2[j];
    if (d) return d;
  }	  
  return 0;
}

int encodeLCAlphaSpace(range_coder *c,unsigned short *s,int len,stats_handle *h)
{
  int c1=charIdx(' ');
  int c2=charIdx(' ');
  int o;

  for(o=0;o<len;o++) {
    int c3=charIdx(s[o]);
    
#ifdef DEBUG
      printf("  encoding @ %d, c1=%d(%c) c2=%d(%c), c3=%d(%c)\n",
	     o,c1,chars[c1],c2,chars[c2],c3,chars[c3]);
#endif

#ifdef WORDSUBSTITUTION
    if (!charInWord(chars[c2])) {
      /* We are at a word break, so see if we can do word substitution.
	 Either way, we must flag whether we performed word substitution */
      int w;
      int longestWord=-1;
      int longestLength=0;

      if (charInWord(chars[c3])) {
	/* Find the first word that matches */
	w=0;
	int bit;
	for(bit=30;bit>=0;bit--) {
	  if ((w|(1<<bit))>=wordCount) continue;
       
	  int t=w|(1<<bit);

	  /* stop if we have found first match */
	  int d2=strncmp816(wordList[t],&s[o],strlen(wordList[t]));
	  /* if wordList[w-1] is lexographically earlier than the text,
	     and wordList[w] is not lexographically earlier than the next, then
	     we have found the point we are looking for, and can stop. */
	  if (d2>=0) {
	    int d1=t ? strncmp816(wordList[t-1],&s[o],strlen(wordList[t-1])) : -1;
	    if (d1<0)
	      {
		// printf("word '%s' comes before '%s'\n",wordList[t-1],&s[o]);
		// printf("but '%s' equals or comes after '%s'\n",wordList[t],&s[o]);
		w=t;
		break;
	      }
	  } else if (d2<0) {
	    /* if both are before where we are looking for, then set this bit in w. */
	    // printf("word '%s' comes before '%s'\n",wordList[t],&s[o]);
	    w=t;
	  } else
	    /* we have gone too far in the list, so don't set this bit.
	       Continue iterating through lower-order bits. */
	    continue;
	}
	// printf("starting to look from '%s' for '%s'\n",wordList[w],&s[o]);

	for(;w<wordCount;w++) {
	  int wordLen=strlen(wordList[w]);
	  int d=strncmp816(wordList[w],&s[o],wordLen);
	  if (d<0) {
	    /* skip words prior to the one we are looking for */
	    continue;
	  } else if (d==0) {
#ifdef DEBUG
	    printf("    word match: strlen=%d, longestLength=%d\n",
		   wordLen,(int)longestLength
		   );
#endif
	    range_coder *t=range_new_coder(1024);
	    range_encode_symbol(t,wordSubstitutionFlag,2,0);
	    range_encode_symbol(t,wordFrequencies,wordCount,w);	   
	    range_coder_free(t);
#ifdef DEBUG
	    double substEntropy=t->entropy;
	    // double entropy=entropy3(c1,c2,wordList[w]);
	    double savings=entropy-substEntropy;
#endif	    
	    if (strlen(wordList[w])>longestLength) {
	      longestLength=strlen(wordList[w]);
	      longestWord=w;	      
#ifdef DEBUG
	      printf("spotted substitutable instance of '%s' -- save %f bits (%f vs %f)\n",
		     wordList[w],savings,substEntropy,entropy);
#endif
	    }
	  } else
	    /* ran out of matching words, so stop search */
	    break;
	}
      }
      if (longestWord>-1) {
	/* Encode "we are substituting a word here */
#ifdef DEBUG
	double entropy=c->entropy;
#endif
	range_encode_symbol(c,wordSubstitutionFlag,2,0);

	/* Encode the word */
	range_encode_symbol(c,wordFrequencies,wordCount,longestWord);
	
#ifdef DEBUG
	  printf("substituted %s at a cost of %f bits.\n",
		 wordList[longestWord],c->entropy-entropy);
#endif

	/* skip rest of word, but make sure we stay on track for 3rd order model
	   state. */
	o+=longestLength-1;
	if (s[o]) {
	  c1=charIdx(s[o-1]);
	  if (c1<0) { exit(-1); }
	  c2=charIdx(s[o]);
	  if (c2<0) { exit(-1); }
	}
#ifdef DEBUG
	  printf("  post substitution @ %d, c1=%d(%c), c2=%d(%c)\n",
		 o,c1,chars[c1],c2,chars[c2]);
#endif
	continue;
      } else {
	/* Encode "not substituting a word here" symbol */
#ifdef DEBUG
	double entropy=c->entropy;
#endif
	range_encode_symbol(c,wordSubstitutionFlag,2,1);
#ifdef DEBUG
	printf("incurring non-substitution penalty = %f bits\n",
	       c->entropy-entropy);
#endif
      }
    } else {
#ifdef DEBUG
	printf("  not a wordbreak @ %d, c1=%d(%c), c2=%d(%c)\n",
	       o,c2,chars[c2],c3,chars[c3]);
#endif
    }
#endif // WORDSUBSTITUTIONFLAG
    if (0)
      {
	int i;
	fprintf(stderr,"After '");
	for(i=0;i<o;i++) fprintf(stderr,"%c",s[i]);
	fprintf(stderr,"'\n");
      }
    int t=s[o]; 
    s[o]=0;
    struct probability_vector *v=extractVector(s,o,h);
    s[o]=t;
    range_encode_symbol(c,v->v,CHARCOUNT,c3);
    if (chars[c3]=='0') range_encode_equiprobable(c,10,s[o]-'0');
    if (0) vectorReport("var",v,c3);    
    c1=c2; c2=c3;
  }
  return 0;
}
