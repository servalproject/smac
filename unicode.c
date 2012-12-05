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

int utf16toutf8(unsigned short *in,int in_len,unsigned char *out,int *out_len)
{
  int i; *out_len=0;
  for(i=0;i<in_len;i++) {
    int codepoint=in[i];
    if (codepoint<0x80) {
      if (*out_len>1023) return -1; // UTF8 string too long
      out[(*out_len)++]=codepoint;
    } else if (codepoint<0x0800) {
      if (*out_len>1022) return -1; // UTF8 string too long
      out[(*out_len)++]=0xc0+(codepoint>>6);
      out[(*out_len)++]=0x80+(codepoint&0x3f);
    } else {
      if (*out_len>1021) return -1; // UTF8 string too long
      out[(*out_len)++]=0xe0+(codepoint>>12);
      out[(*out_len)++]=0x80+((codepoint>>6)&0x3f);
      out[(*out_len)++]=0x80+(codepoint&0x3f);
    }
  }
  return 0;
}

int utf8toutf16(unsigned char *in,int in_len,unsigned short *out,int *out_len)
{
  int i; *out_len=0;
  for(i=0;i<in_len;i++)
    {
      if ((in[i]&0xc0)==0x80) {
	/* String begins with a UTF8 continuation character, or has a continuation
	   character out of place.
	   This is not allowed (not in the least because we use exactly this
	   construction to indicate a compressed message, and so never need to
	   have a compressed message be longer than uncompressed, because
	   uncompressed messages are valid in place of compressed messages). */
	return -1;
      } else if ((in[i]&0xc0)<0x80) {
	// natural character 
	out[(*out_len)++]=in[i];
      } else {
	// UTF character
	int unicode=0;
	if (in[i]<0xe0) {
	  if (in_len-i<1) return -1; // string ends mid-way through a UTF8 sequence
	  // 2 bytes
	  unicode=((in[i]&0x1f)<<6)|(in[i+1]&0x3f);
	  i++;
	  out[(*out_len)++]=unicode;	  
	} else if (in[i]<0xf8) {
	  if (in_len-i<2) return -1; // string ends mid-way through a UTF8 sequence
	  // 3 bytes
	  unicode=((in[i]&0x0f)<<12)|((in[i+1]&0x3f)<<6)|(in[i+2]&0x3f);
	  i+=2;
	  out[(*out_len)++]=unicode;	  
	} else {
	  // UTF8 no longer supports >3 byte sequences
	  return -1;
	}
      }
    }
  return 0;
}

unsigned short ret[1025];
unsigned short *ascii2utf16(char *in)
{
  int i;
  for(i=0;in[i]&&i<1024;i++) ret[i]=in[i];
  // null terminate utf16 string, as some functions require it
  ret[i]=0;
  return ret;
}

int unEscape(unsigned char *utf8line,int *utf8len)
{
  int outLen=0;
  int i;

  for(i=0;i<*utf8len;i++) {
    if (utf8line[i]=='\\') {
      switch(utf8line[i+1]) {
	
      case 'r': i++; utf8line[outLen++]='\r'; break;
      case 'n': i++; utf8line[outLen++]='\n'; break;
      case '\\': i++; utf8line[outLen++]='\\'; break;
      case '\'': utf8line[outLen++]='\''; break;
      case 0: utf8line[outLen++]='\\'; break;
      default:
	/* don't do anything to an unknown escape */
	utf8line[outLen++]='\\';
	break;
      }
    } else {
      utf8line[outLen++]=utf8line[i];
    }    
  }
  *utf8len=outLen;
  return 0;
}
