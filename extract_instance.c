#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <stdlib.h>

int main(int argc,char **argv)
{
  if (argc<3) {
    fprintf(stderr,"You need to indicate the form name and form instance on the command line.\n");
    exit(-1);
  }

  char *form_name=argv[1];

  FILE *f=fopen(argv[2],"r");

  char tag[1024];
  int taglen=0;

  char value[1024];
  int val_len=0;

  int in_instance=0;
  
  int interesting_tag=0;

  int state=0;

  int c=fgetc(f);
  while(c>=-1&&(!feof(f))) {
    switch(c) {
    case '\n': case '\r': break;
    case '<': 
      state=1;
      if (interesting_tag&&val_len>0) {
	value[val_len]=0;
	printf("%s=%s\n",tag,value);
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
    c= fgetc(f);
  }
  return 0;  
}
