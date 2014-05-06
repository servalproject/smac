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
#include <ctype.h>

#include "charset.h"

/* 0 is a place holder for 0-9.
   U is a place holder for all Unicode characters.
*/
char chars[CHARCOUNT]="abcdefghijklmnopqrstuvwxyz !@#$%^&*()_+-=~`[{]}\\|;:'\"<,>.?/\r\n\t0U";
char printableChars[PRINTABLECHARCOUNT]="abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ !@#$%^&*()_+-=~`[{]}\\|;:'\"<,>.?/\r\n\t0123456789";
char wordChars[36]="abcdefghijklmnopqrstuvwxyz0123456789";
int charIdx(unsigned short c)
{
  if (c>0x7f) c='U';

  // Collapse digits onto a single position.
  if (c>='1'&&c<='9') c='0';

  int i;
  for(i=0;i<CHARCOUNT;i++)
    if (c==chars[i]) return i;
       
  /* Not valid character -- must be encoded separately */
  return -1;
}

int printableCharIdx(unsigned char c)
{
  int i;
  for(i=0;i<PRINTABLECHARCOUNT;i++)
    if (c==printableChars[i]) return i;

  /* Not valid character -- must be encoded separately */
  return -1;
}

int charInWord(unsigned short c)
{
  int i;
  int cc=c;
  if (cc<0x80) {
    cc=tolower(c);
    for(i=0;i<36;i++) if (cc==wordChars[i]) return 1;
  }
  // all unicode characters are for now treated as word breaking.
  return 0;
}
