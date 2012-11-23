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

#ifdef UNCOMPRESSEDMESSAGESTATS
/* Calculate entropy of string using 3rd-order
   message statistics. */
double entropy3(int c1,int c2, char *string)
{
  int i;
  range_coder *t=range_new_coder(1024);
  for(i=0;i<strlen(string);i++)
    {
      int c3=charIdx(string[i]);
      range_encode_symbol(t,char_freqs3[c1][c2],69,c3);    
      c1=c2; c2=c3;
    }
  double entropy=t->entropy;
  range_coder_free(t);
  return entropy;
}
#endif

double entropyOfInverse(int n)
{
  return -log(1.0/n)/log(2);
}
