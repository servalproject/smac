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

int encodeLCAlphaSpace(range_coder *c,unsigned char *s)
{
  int c1=charIdx(' ');
  int c2=charIdx(' ');
  int o;
  for(o=0;s[o];o++) {
    int c3=charIdx(s[o]);
    
    if (!charInWord(s[o-1])) {
      /* We are at a word break, so see if we can do word substitution.
	 Either way, we must flag whether we performed word substitution */
      int w;
      int longestWord=-1;
      int longestLength=0;
      double longestSavings=0;
      if (charInWord(s[o])) {
#if 0
	{
	  /* See if it makes sense to encode part of the message from here
	     without using 3rd order model. */
	  range_coder *t=range_new_coder(2048);
	  range_coder *tf=range_new_coder(1024);
	  // approx cost of switching models twice
	  double entropyFlat=10+entropyOfInverse(69+1+1);
	  int i;
	  int cc2=c2;
	  int cc1=c1;
	  for(i=o;s[i]&&(isalnum(s[i])||s[i]==' ');i++) {
	    int c3=charIdx(s[i]);
	    range_encode_symbol(t,tweet_freqs3[cc1][cc2],69,c3);
	    range_encode_equiprobable(tf,69+1,c3);
	    if (!s[i]) {
	      /* encoding to the end of message saves us the 
		 stop symbol */
	      tf->entropy-=entropyOfInverse(69+1);
	    }
	    if ((t->entropy-tf->entropy-entropyFlat)>longestSavings) {
	      longestLength=i-o+1;
	      longestSavings=t->entropy-tf->entropy-entropyFlat;
	    }
	    cc1=cc2; cc2=c3;
	  }

	  if (longestLength>0)
	    printf("Could save %f bits by flat coding next %d chars.\n",
		   longestSavings,longestLength);
	  else
	    printf("No saving possible from flat coding.\n");
	  longestSavings=0; longestLength=0;
	  range_coder_free(t);
	  range_coder_free(tf);
	}
#endif

	for(w=0;w<wordCount;w++) {
	  if (!strncmp((char *)&s[o],wordList[w],strlen(wordList[w]))) {
	    if (0) printf("    word match: strlen=%d, longestLength=%d\n",
			  (int)strlen(wordList[w]),(int)longestLength
			  );
	    double entropy=entropy3(c1,c2,wordList[w]);
	    range_coder *t=range_new_coder(1024);
	    range_encode_symbol(t,wordSubstitutionFlag,2,0);
	    range_encode_symbol(t,wordFrequencies,wordCount,w);
	    double substEntropy=t->entropy;
	    range_coder_free(t);
	    double savings=entropy-substEntropy;
	    
	    if (strlen(wordList[w])>longestLength) {
	      longestLength=strlen(wordList[w]);
	      longestWord=w;	      
	      if (0)
		printf("spotted substitutable instance of '%s' -- save %f bits (%f vs %f)\n",
		     wordList[w],savings,substEntropy,entropy);
	    }
	  }
	}
      }
      if (longestWord>-1) {
	/* Encode "we are substituting a word here */
	double entropy=c->entropy;
	range_encode_symbol(c,wordSubstitutionFlag,2,0);

	/* Encode the word */
	range_encode_symbol(c,wordFrequencies,wordCount,longestWord);
	
	printf("substituted %s at a cost of %f bits.\n",
		 wordList[longestWord],c->entropy-entropy);

	/* skip rest of word, but make sure we stay on track for 3rd order model
	   state. */
	o+=longestLength-1;
	c3=charIdx(s[o-1]);
	c2=charIdx(s[o]);
	continue;
      } else {
	/* Encode "not substituting a word here" symbol */
	double entropy=c->entropy;
	range_encode_symbol(c,wordSubstitutionFlag,2,1);
	if (0)
	  printf("incurring non-substitution penalty = %f bits\n",
		 c->entropy-entropy);
      }
    }
    range_encode_symbol(c,tweet_freqs3[c1][c2],69,c3);    
    c1=c2; c2=c3;
  }
  return 0;
}
