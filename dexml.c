#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <stdlib.h>

/*
  Re-constitute a form to XML (or other format) by reading a template of the output
  format and substituting the values in.

  Update 26.05.2016 : Stripped2xml supports forms with subforms (But only 1 level of subforms!)
*/
int stripped2xml(char *stripped,int stripped_len,char *template,int template_len,char *xml,int xml_size)
{
  int xml_ofs=0;
  int state=0;
  int i,j,k;

  int state_in_subform_record=0;

  int state_in_subform_instance=0;

  int record_count[1024];
  int record_number=0;

  int m;

  for(m=0;m<1024;m++){
	  record_count[m]=0;
  }

  char *fieldnames[1024];
  char *values[1024];
  int  in_subform_record[1024];
  int  record_number_field[1024];
  int field_count=0;
  
  char field[1024];
  int field_len=0;

  char value[1024];
  int value_len=0;
  
  char tag[1024];
  int tag_len=0;

  // Read fields from stripped.

  for(i=0;i<stripped_len;i++) {
    if (stripped[i]=='='&&(state==0)) {
      state=1;
    } else if(stripped[i]=='{') { //Found an opening bracket

    	printf("Found opening bracket ! \n");

    	//{Opening bracket} X {We are not already in a subform} => beginning of subform
    	if (state_in_subform_instance ==0) {
    		state_in_subform_instance=1;
    	}
    	//{Opening bracket} X {We are already in a subform} => beginning of subform record
    	else {
    		//Count the number of records in this form
    		record_count[record_number]++;
    		state_in_subform_record=1;
    	}
    } else if(stripped[i]=='}') { //Found a closing bracket

    printf("Found closing bracket ! \n");

        //{Closing bracket} X {We are already in a subform record} => end of subform record
		if (state_in_subform_record==1) {
			state_in_subform_record=0;
		}
		//{Closing bracket} X {We are not already in a subform record} => end of subform
		else {
			state_in_subform_instance=0;
			record_number++;
		}
    } else if (stripped[i]<' ') {
      if (state==1) {
	// record field=value pair
	field[field_len]=0;
	value[value_len]=0;
	fieldnames[field_count]=strdup(field);
	values[field_count]=strdup(value);

	//record if the value pair is a part of a subform record
	in_subform_record[field_count]=state_in_subform_record;
	//record the number of records in this subform record
	record_number_field[field_count]=record_number;

	printf("---\n@@@ fieldname = %s \n",fieldnames[field_count]);
	printf("@@@ value = %s \n",values[field_count]);
	printf("@@@ part of subform record ? = %d \n",in_subform_record[field_count]);
	printf("@@@ subform record temporary number = %d \n",record_number);
	printf("@@@ Record count = %d \n --- \n",record_count[record_number]);

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
  int state_field=0;
  int state_tag=0;
  field_len=0;
  state_in_subform_record=0;
  state_in_subform_instance=0;
  int beginning_index_of_subform_record = 0;
  int remaining_records = 0;
  int number_of_records_written = 0;

  printf(" About to read template ... \n");

  for(i=0;i<template_len;i++) {

	 if (template[i]=='<') {
		 //we are going into a tag
		state_tag=1;
		//Write the '<' symbol in the XML
		xml[xml_ofs++]=template[i];
	 }

	 else if (template[i]=='>') {
	  	//we are going out from a tag
		 tag[tag_len]=0; tag_len=0;
		 //Write the '>' symbol in the XML
		 xml[xml_ofs++]=template[i];

		 //The tag is a subform tag
		 if(!strncasecmp("dd:subform",tag,strlen("dd:subform"))){
			 printf("Found a subform tag ! \n");
			 state_in_subform_instance=1;
		 }

		 //The tag is a subform end tag
		 else if ((!strncasecmp("dd:subform",&tag[1],strlen("dd:subform")))
		  	      &&tag[0]=='/') {
			printf("Found a subform END tag ! \n");
		  	state_in_subform_instance=0;
		  	state_in_subform_record=0;
		  	number_of_records_written=0;
		  	beginning_index_of_subform_record=0;
		  }

		 //The tag is a data tag in a subform => Beginning of a record
		 else if (!strncasecmp("data",tag,strlen("data")) && state_in_subform_instance == 1){

			 printf("Found a data tag within a subform ! \n");
			 state_in_subform_record=1;
			 remaining_records = 0;
			 //Store the position of the record beginning for further records
			 beginning_index_of_subform_record=i-strlen("data")-3;

		 }

		 //The tag is a data end tag in a subform => End of a record
		 else if ((!strncasecmp("data",&tag[1],strlen("data")) && state_in_subform_instance == 1)
		  	      && tag[0]=='/') {
			 printf("Found a data END tag within a subform ! \n");
		  	 if(remaining_records == 1) {
				 number_of_records_written++;
				 //set remaining_records to 0, it will be set to 1 if we have another record
				 remaining_records = 0;
		  		 //Go back in the template file to the beginning of a record
		  		 i=beginning_index_of_subform_record;
		  	 } else {
			  	 state_in_subform_record=0;
		  	 }
		  }
		state_tag=0;
	 }

	 else if (template[i]=='$') {
      if (state_field==1) {
	// end of variable
	field[field_len]=0; field_len=0;
	for(j=0;j<field_count;j++)
	  if (!strcasecmp(field,fieldnames[j])) {
		printf(" Found matching field ! Fieldname = %s \n ",fieldnames[j]);
	    // write out field value
	    for(k=0;values[j][k];k++) {
	      xml[xml_ofs++]=values[j][k];
	      if (xml_ofs==xml_size) return -1;
	    }
	    //If we find a record, we check if we have more records
	    //Sometimes, there are even no records in the subform
	    //So in this case remaning records will stay 0 et we will go on.
		if(in_subform_record[j] == 1){
			//If we have written the total number of records (i.e. count - written == 1) we dont set remaining records to 0
			if((record_count[record_number_field[j]] - number_of_records_written) > 1) {
				remaining_records = 1;
			}
		}
		//Empty the fieldname because we already wrote it in the XML
		//Thus, the strcasecmp between field and fieldnames[j]
		//will find the other record fieldnames if the fields have the same name
		//for example "uuid" or fieldnames in the records of a same subform
		fieldnames[j]="";
	    break;
	  }
	state_field=0;
      } else {
	// start of variable stubstitution
	state_field=1;
      }
    }
	  else {

      if (state_field==1) {
    	  // accumulate field name
    	  if (field_len<1023) {
    		  field[field_len++]=template[i];
    		  field[field_len]=0;
    	  }
      } else {
    	  // natural character
    	  xml[xml_ofs++]=template[i];
    	  // accumulate tag name
    	  if(state_tag==1 && tag_len<1023) {
    		  tag[tag_len++]=template[i];
    		  tag[tag_len]=0;
    	  }
    	  if (xml_ofs==xml_size) return -1;
      }
    }
  }
  return xml_ofs;
}

int xml2stripped(const char *form_name, const char *xml,int xml_len,
		 char *stripped,int stripped_size)
{

  /* rbk - start */

  int in_subform=0;

  /* rbk - end */

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
	}

	/* Added code : April 2016 (handling subforms) */
	else if (!strncasecmp("dd:subform",tag,strlen("dd:subform"))) {
	  // nothing to do
	}
	/* End added code */

	else {
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


/* Added code : April 2016 (handling subforms) */
	  if(!strncasecmp("dd:subform",tag,strlen("dd:subform")))
	  {
		  //we are in the start of a subform
		  in_subform++;
		  printf("Test-Dialog / dexml.c : Found start of subform ! Tag = %s \n",tag);
			int b=snprintf(&stripped[stripped_ofs],stripped_size-stripped_ofs,"{\n");
			if (b>0) stripped_ofs+=b;
	  }

	  if ((!strncasecmp("dd:subform",&tag[1],strlen("dd:subform")))
	  	      &&tag[0]=='/')
	  {
		  //we reached the end of the subform
	  	  in_subform--;
	  	  printf("Test-Dialog / dexml.c : Reached end of subform ! Tag = %s \n",tag);
			int b=snprintf(&stripped[stripped_ofs],stripped_size-stripped_ofs,"}\n");
			if (b>0) stripped_ofs+=b;
	  }

	  if((!strncasecmp("data",tag,strlen("data")))&&in_subform)
	  {
		  int b=snprintf(&stripped[stripped_ofs],stripped_size-stripped_ofs,"{\n");
		  if (b>0) stripped_ofs+=b;
	  }

	  if ((!strncasecmp("data",&tag[1],strlen("data")))&&(tag[0]=='/')&&(in_subform))
	  {
		  int b=snprintf(&stripped[stripped_ofs],stripped_size-stripped_ofs,"}\n");
		  if (b>0) stripped_ofs+=b;
	  }

/* end added code */

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
