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
#include <stdlib.h>
#include <strings.h>
#include <ctype.h>
#include "arithmetic.h"
#include "charset.h"

/* 
   TODO: Doesn't handle UTF-8 Unicode yet.
*/
int encodePackedASCII(range_coder *c,unsigned char *m)
{
  /* we can't encode it more efficiently than char symbols */
  int i;
  for(i=0;m[i];i++) {
    int v=m[i];
    v=printableCharIdx(v);
    if (v<0) return -1;
    range_encode_equiprobable(c,PRINTABLECHARCOUNT,v);
  }
  // encodeCaseModel1(c,m);
  return 0;
}

int decodePackedASCII(range_coder *c, unsigned char *m,int encodedLength)
{
  int i;
  for(i=0;i<encodedLength;i++) {
    int symbol=range_decode_equiprobable(c,PRINTABLECHARCOUNT);
    int character=printableChars[symbol];
    m[i]=character;
  }
  m[i]=0;
  return 0;
}
