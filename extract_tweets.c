/*
(C) Paul Gardner-Stephen 2012-2013

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

int extractTweet(char *s,int allowUnicode)
{
  int len=0;
  char out[8192];
  int unicode=0;

  while(*s) {
    switch (*s) {
    case '"': goto done;
    case '&': 
      if (!strncmp(s,"&amp;",5)) { out[len++]='&'; s+=4; }
      else if (!strncmp(s,"&gt;",4)) { out[len++]='>'; s+=3; }
      else if (!strncmp(s,"&lt;",4)) { out[len++]='<'; s+=3; }
      else { 
	out[len++]=*s;
      }
      break;
    case '\\':
      s++;
      switch(*s) {
      case 'u':
	{
	  /* unicode character */
	  char hex[5];
	  hex[0]=*(++s);
	  hex[1]=*(++s);
	  hex[2]=*(++s);
	  hex[3]=*(++s);
	  hex[4]=0;
	  unsigned int codepoint=strtol(hex,NULL,16);
	  if (codepoint<0x80) {
	    out[len++]=codepoint;
	  } else if (codepoint<0x0800) {
	    out[len++]=0xc0+(codepoint>>6);
	    out[len++]=0x80+(codepoint&0x3f);
	    unicode++;
	  } else {
	    out[len++]=0xe0+(codepoint>>12);
	    out[len++]=0x80+((codepoint>>6)&0x3f);
	    out[len++]=0x80+(codepoint&0x3f);
	    unicode++;
	  }
	}
	break;
      case '\'': case '"': case '/':
	/* remove escaping from these characters */
	out[len++]=*s;
	break;
      case 'n': case 'r': case '\\': 
      default:
	/* Keep escaped */
	out[len++]='\\';
	out[len++]=*s;
	break;
      }
      break;
    default:
      out[len++]=*s;
      break;
    }
    s++;
  }
 done:
  out[len]=0;
  if (!unicode||allowUnicode) printf("%s\n",out);
  return 0;
}

int main(int argc,char **argv)
{
  int i;
  char line[8192];

  int allowUnicode=atoi(argv[1]?argv[1]:"0");

  line[0]=0; fgets(line,8192,stdin);
  while(line[0]) {
    for(i=0;line[i]&&line[i+10];i++)
      if (!strncasecmp(&line[i],"\"text\":\"",7)) {
	// Line is for the creation of a tweet
	extractTweet(&line[8+i],allowUnicode);
	break;
      }    

    line[0]=0; fgets(line,8192,stdin);
  }
  return 0;
/}
