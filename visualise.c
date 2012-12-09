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

char *contentXML1="<?xml version=\"1.0\" encoding=\"UTF-8\"?><office:document-content xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\" xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\" xmlns:number=\"urn:oasis:names:tc:opendocument:xmlns:datastyle:1.0\" xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\" xmlns:chart=\"urn:oasis:names:tc:opendocument:xmlns:chart:1.0\" xmlns:dr3d=\"urn:oasis:names:tc:opendocument:xmlns:dr3d:1.0\" xmlns:math=\"http://www.w3.org/1998/Math/MathML\" xmlns:form=\"urn:oasis:names:tc:opendocument:xmlns:form:1.0\" xmlns:script=\"urn:oasis:names:tc:opendocument:xmlns:script:1.0\" xmlns:ooo=\"http://openoffice.org/2004/office\" xmlns:ooow=\"http://openoffice.org/2004/writer\" xmlns:oooc=\"http://openoffice.org/2004/calc\" xmlns:dom=\"http://www.w3.org/2001/xml-events\" xmlns:xforms=\"http://www.w3.org/2002/xforms\" xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xmlns:rpt=\"http://openoffice.org/2005/report\" xmlns:of=\"urn:oasis:names:tc:opendocument:xmlns:of:1.2\" xmlns:xhtml=\"http://www.w3.org/1999/xhtml\" xmlns:grddl=\"http://www.w3.org/2003/g/data-view#\" xmlns:tableooo=\"http://openoffice.org/2009/table\" xmlns:field=\"urn:openoffice:names:experimental:ooo-ms-interop:xmlns:field:1.0\" xmlns:formx=\"urn:openoffice:names:experimental:ooxml-odf-interop:xmlns:form:1.0\" xmlns:css3t=\"http://www.w3.org/TR/css3-text/\" office:version=\"1.2\"><office:scripts/><office:font-face-decls><style:font-face style:name=\"Times New Roman1\" svg:font-family=\"&apos;Times New Roman&apos;\" style:font-family-generic=\"roman\"/><style:font-face style:name=\"Times New Roman\" svg:font-family=\"&apos;Times New Roman&apos;\" style:font-family-generic=\"roman\" style:font-pitch=\"variable\"/><style:font-face style:name=\"Arial Unicode MS\" svg:font-family=\"&apos;Arial Unicode MS&apos;\" style:font-family-generic=\"system\" style:font-pitch=\"variable\"/><style:font-face style:name=\"Tahoma\" svg:font-family=\"Tahoma\" style:font-family-generic=\"system\" style:font-pitch=\"variable\"/></office:font-face-decls><office:automatic-styles><style:style style:name=\"P1\" style:family=\"paragraph\" style:parent-style-name=\"Standard\"><style:text-properties style:font-name-complex=\"Times New Roman1\"/></style:style>\n";
char *contentXML3="</office:automatic-styles><office:body><office:text><text:sequence-decls><text:sequence-decl text:display-outline-level=\"0\" text:name=\"Illustration\"/><text:sequence-decl text:display-outline-level=\"0\" text:name=\"Table\"/><text:sequence-decl text:display-outline-level=\"0\" text:name=\"Text\"/><text:sequence-decl text:display-outline-level=\"0\" text:name=\"Drawing\"/></text:sequence-decls>\n";

int writePalette(FILE *out)
{
  /* Write out a color palette that works for both colour blind and non-colour blind
     people.  This basically leaves a blue->yellow gradiant, with yellow 
     representing higher entropy, and blue lower entropy. */
  int i;

  unsigned int colours[25]={
    0x00007f, // 0 bim blue
    0x0000ff, // 1 blue
    0x0040bf, // 2
    0x007f7f, // 3
    0x00bf3f, // 4
    0x00ff00, // 5 green
    0x3fff00, // 6
    0x7fff00, // 7
    0xbfff00, // 8
    0xffff00, // 9 yellow
    0xffbf00, // 10
    0xff7f00, // 11
    0xff3f00, // 12
    0xff0000, // 13 red
    0xff1f1f, // 14
    0xff3f3f, // 15
    0xff5f5f, // 16
    0xff7f7f, // 17 pink
    0xff9f9f, // 18
    0xffbfbf, // 19
    0xffdfdf, // 20
    0xffffff, // 21 white
    0xffffff, // 22
    0xffffff, // 23
    0xffffff // 24
  };

  for(i=0;i<256;i++) {
    int bits=i*24/256;

    fprintf(out,"<style:style style:name=\"T%d\" style:family=\"text\"><style:text-properties fo:background-color=\"#%06x\" fo:foreground-color=\"#000000\" style:font-name-complex=\"Times New Roman1\"/></style:style>\n",i,
	    colours[bits]
	    //i,i,255-i
	    );
  }
  return 0;
}

int beginContentXML(FILE *out)
{
  int i;
  fprintf(out,"%s",contentXML1);
  writePalette(out);
  fprintf(out,"%s",contentXML3);

  // Draw colour legend
  fprintf(out,"<text:p text:style-name=\"Standard\">Legend: <text:tab/>");
  for(i=0;i<=24;i++) {
    fprintf(out,"<text:span text:style-name=\"T%d\"> %d </text:span>",i*256/24,i);
  }
  fprintf(out,"bits</text:p>\n");
  fprintf(out,"<text:p/>\n");

  return 0;
}

int endContentXML(FILE *out)
{
  fprintf(out,"</office:text>\n</office:body>\n</office:document-content>\n");
  return 0;
}

int visualiseMessage(FILE *out,unsigned char *utf8,
		     double percent,double *entropyLog)
{
  fprintf(out,"<text:p text:style-name=\"Standard\">%.1f%%<text:tab/>",percent);

  int i,symbol=0;
  for(i=0;utf8[i];i++)
    {
      int c=entropyLog[symbol]*256/24;
      if (c>255) c=255;
      fprintf(out,"<text:span text:style-name=\"T%d\">",c);
      if (utf8[i]<0x80) {
	switch(utf8[i]) {
	case '<': fprintf(out,"&lt;"); break;
	case '>': fprintf(out,"&gt;"); break;
	case '&': fprintf(out,"&amp;"); break;
	default: fprintf(out,"%c",utf8[i]); break;
	}
      } else if (utf8[i]<0xc0) {
	// continuation character -- an error
      } else if (utf8[i]<0xe0) {
	// 0xc0 - 0xdf = 2 byte unicode character
	fprintf(out,"%c%c",utf8[i],utf8[i+1]);
	i+=1;
      } else if (utf8[i]<0xf0) {
	// 0xe0 - 0xef = 3 byte unicode character
	int codePoint=((utf8[i]&0x0f)<<12)|((utf8[i+1]&0x3f)<<6)|(utf8[i+2]&0x3f);
	if (codePoint>=0xd800&&codePoint<0xe000) {
	  // Code point low surrogate.  Must be followed by a high surrogate,
	  // and not split into separate spans.
	  // fprintf(out,"%c%c%c",utf8[i],utf8[i+1],utf8[i+2]);
	  i+=2;
	  // fprintf(out,"%c%c%c",utf8[i],utf8[i+1],utf8[i+2]);
	  // surrogates cause too much pain for now
	  fprintf(out,"?");
	} else {
	  fprintf(out,"%c%c%c",utf8[i],utf8[i+1],utf8[i+2]);
	  i+=2;
	}
      }
      fprintf(out,"</text:span>");
      symbol++;
    }
  fprintf(out,"</text:p>\n");
  return 0;
}
