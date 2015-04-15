#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <stdlib.h>

/*
  Re-constitute a form to XML (or other format) by reading a template of the output
  format and substituting the values in.
*/
int stripped2xml(char *stripped,int stripped_len,char *template,int template_len,char *xml,int xml_size)
{
  int xml_ofs=0;
  int state=0;
  int i,j,k;

  char *fieldnames[1024];
  char *values[1024];
  int field_count=0;
  
  char field[1024];
  int field_len=0;

  char value[1024];
  int value_len=0;
  
  // Read fields from stripped.
  for(i=0;i<stripped_len;i++) {
    if (stripped[i]=='='&&(state==0)) {
      state=1;
    } else if (stripped[i]<' ') {
      if (state==1) {
	// record field=value pair
	field[field_len]=0;
	value[value_len]=0;
	fieldnames[field_count]=strdup(field);
	values[field_count]=strdup(value);
	field_count++;
      }
      state=0;
      field_len=0;
      value_len=0;
    } else {
      if (field_len>1000||value_len>1000) return -1;
      if (state==0) field[field_len++]=stripped[i];
      else value[value_len++]=stripped[i];
    }
  }

  // Read template, substituting $FIELD$ with the value of the field.
  // $$ substitutes to a single $ character.
  state=0; field_len=0;
  for(i=0;i<template_len;i++) {
    if (template[i]=='$') {
      if (state==1) {
	// end of variable
	field[field_len]=0; field_len=0;
	for(j=0;j<field_count;j++)
	  if (!strcasecmp(field,fieldnames[j])) {
	    // write out field value
	    for(k=0;values[j][k];k++) {
	      xml[xml_ofs++]=values[j][k];
	      if (xml_ofs==xml_size) return -1;
	    }
	    break;
	  }
	state=0;
      } else {
	// start of variable stubstitution
	state=1;
      }
    } else {
      if (state==1) {
	// accumulate field name
	if (field_len<1023) {
	  field[field_len++]=template[i];
	  field[field_len]=0;
	}
      } else {
	// natural character
	xml[xml_ofs++]=template[i];
	if (xml_ofs==xml_size) return -1;
      }
    }
  }
  return xml_ofs;
}

int xml2stripped(const char *form_name, const char *xml,int xml_len,
		 char *stripped,int stripped_size)
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

  char exit_tag[1024]="";
  

  int c=xml[xmlofs++];
  while(c>=-1&&(xmlofs<xml_len)) {
    switch(c) {
    case '\n': case '\r': break;
    case '<': 
      state=1;
      if (interesting_tag&&val_len>0) {
	value[val_len]=0;
	// Magpi puts ~ in empty fields -- don't include these in the stripped output
	if ((value[0]=='~')&&(val_len==1)) {
	  // nothing to do
	} else {
	  int b=snprintf(&stripped[stripped_ofs],stripped_size-stripped_ofs,"%s=%s\n",tag,value);
	  if (b>0) stripped_ofs+=b;
	}
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
	if (!form_name) {
	  /*
	    Magpi forms don't include the form name in the xml.
	    We have to get the form name from the formid field.

	    ODK Collect on the other hand provides the form name as an
	    id attribute of a tag which follows an <instance> tag.
	  */
	  if (!strncasecmp("form",tag,strlen("form")))
	    {
	      //	    if (!in_instance) printf("Found start of instance\n");
	      in_instance++;
	    }
	  if ((!strncasecmp("form",&tag[1],strlen("form")))
	      &&tag[0]=='/')
	    {
	      in_instance--;
	      //	    if (!in_instance) printf("Found end of instance\n");
	    }
	  if (!in_instance) {
	    // ODK form name appears as attributes of a tag which has a named based
	    // on the name of the form.
	    char name_part[1024];
	    char version_part[1024];
	    int r=0;
	    if (strlen(tag)<1024) {
	      r=sscanf(tag,"%s id=\"%[^\"]\" version=\"%[^\"]\"",
		       exit_tag,name_part,version_part);
	    }
	    if (r==3) {
	      // Add implied formid tag for ODK forms so that we can more easily find
	      // the recipe that corresponds to a record.
	      fprintf(stderr,"ODK form name is %s.%s\n",
		      name_part,version_part);
	      int b=snprintf(&stripped[stripped_ofs],stripped_size-stripped_ofs,"formid=%s.%s\n",name_part,version_part);
	      if (b>0) stripped_ofs+=b;
	      in_instance++;
	    }

	  }
	  if (in_instance&&exit_tag[0]&&tag[0]=='/'&&!strcasecmp(&tag[1],exit_tag))
	    {
	      // Found matching tag for the ODK instance opening tag, so end
	      // form instance
	      in_instance--;
	    }
	} else {
	  if (!strncasecmp(form_name,tag,strlen(form_name)))
	    {
	      in_instance++;
	    }
	  if ((!strncasecmp(form_name,&tag[1],strlen(form_name)))
	      &&tag[0]=='/')
	    {
	      in_instance--;
	    }
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
  return stripped_ofs;  
}
