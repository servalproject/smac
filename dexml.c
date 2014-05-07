#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <stdlib.h>

int xml2stripped(char *form_name, char *xml,int xml_len,char *stripped,int stripped_size)
{

  char tag[1024];
  int taglen=0;

  char value[1024];
  int val_len=0;

  int in_instance=0;
  
  int interesting_tag=0;

  int state=0;

  int xmlofs=0;
  int stripped_ofs=0;

  int c=xml[xmlofs++];
  while(c>=-1&&(xmlofs<xml_len)) {
    switch(c) {
    case '\n': case '\r': break;
    case '<': 
      state=1;
      if (interesting_tag&&val_len>0) {
	value[val_len]=0;
	int b=snprintf(&stripped[stripped_ofs],stripped_size-stripped_ofs,"%s=%s\n",tag,value);
	if (b>0) stripped_ofs+=b;
	val_len=0;
      }
      interesting_tag=0; 
      break;
    case '>': 
      if (taglen) {
	// got a tag name	
	tag[taglen]=0;
	interesting_tag=0;
	if (tag[0]!='/'&&in_instance&&tag[taglen-1]!='/') {
	  interesting_tag=1;
	}
	if (!strncasecmp(form_name,tag,strlen(form_name)))
	  {
	    //	    if (!in_instance) printf("Found start of instance\n");
	    in_instance++;
	  }
	if ((!strncasecmp(form_name,&tag[1],strlen(form_name)))
	    &&tag[0]=='/')
	  {
	    in_instance--;
	    //	    if (!in_instance) printf("Found end of instance\n");
	  }
	taglen=0;
      }
      state=0; break; // out of a tag
    default:
      if (state==1) {
	// in a tag specification, so accumulate name of tag
	if (taglen<1000) tag[taglen++]=c;
      }
      if (interesting_tag) {
	// exclude leading spaces from values
	if (val_len||(c!=' ')) {
	  if (val_len<1000) value[val_len++]=c;
	}
      }
    }
    c= xml[xmlofs++];
  }
  return 0;  
}
