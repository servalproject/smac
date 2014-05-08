#include <expat.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

//Creation specification stripped file from ODK XML
//FieldName,Type,Minimum,Maximum,Precision

#define MAXCHARS 1000000

void
start(void *data, const char *el, const char **attr)
{   
    char *node_name = "", *node_type = "", *node_constraint = "";
	int i ;
    
    //Looking for bind elements
	if ((!strncasecmp("bind",el,strlen("bind")))||(!strncasecmp("xf:bind",el,strlen("xf:bind")))) {
		
		//We found a bind element, look for attributes
		for (i = 0; attr[i]; i += 2) {
			
			//Display attributes
			/*printf(" %s='%s'", attr[i], attr[i + 1]); */
			
			//Looking for attribute nodeset
			if (!strncasecmp("nodeset",attr[i],strlen("nodeset"))) {
					char *last_slash = strrchr(attr[i+1], '/');
					node_name  = calloc (strlen(last_slash), sizeof(char*));
					memcpy (node_name, last_slash+1, strlen(last_slash));
			}
			
			//Looking for attribute type
			if (!strncasecmp("type",attr[i],strlen("type"))) {
					node_type  = calloc (strlen(attr[i + 1]), sizeof(char*));
					memcpy (node_type, attr[i + 1], strlen(attr[i + 1]));
			}
			
			//Looking for attribute constraint
			if (!strncasecmp("constraint",attr[i],strlen("constraint"))) {
					node_constraint  = calloc (strlen(attr[i + 1]), sizeof(char*));
					memcpy (node_constraint, attr[i + 1], strlen(attr[i + 1]));
			}

		}
		
		// Ignore binary fields in succinct data
		if (strcasecmp(node_type,"binary")){
		  //Now write output Name and Type
		  printf("%s:%s", node_name,node_type);
		  
		  //Now write output Constraint depends on the type (decimal & int) and if constraint exists
		  if (strlen(node_constraint)&&((!strncasecmp("decimal",node_type,strlen("decimal")))||(!strncasecmp("int",node_type,strlen("int"))))) {
		    char *ptr = node_constraint;
		    int a, b;
		    
		    //We look for 0 to 2 digits
		    while( ! isdigit(*ptr) && (ptr<node_constraint+strlen(node_constraint))) ptr++;
		    a = atoi(ptr);
		    while( isdigit(*ptr) && (ptr<node_constraint+strlen(node_constraint))) ptr++;
		    while( ! isdigit(*ptr) && (ptr<node_constraint+strlen(node_constraint))) ptr++;
		    b = atoi(ptr);
		    
		    printf(":%d:%d:0", MIN(a, b), MAX(a, b));
		  } else {
		    printf(":0:0:0");
		  }
		  printf("\n");
		}
	}
    
}

/*void characterdata(void *data, const char *el, int len)
{
			
	//Warning, can be dangerous with buffer overflow attack
	int i;
	for(i=0;i<len;i++){
		if ((el[i] != ' ')&&(el[i] != '\n')) printf("%c",el[i]);
	}
	//printf("%.*s", len, el);

}*/

void end(void *data, const char *el)
{
    //Nothing to do anymore
}  

int main(int argc, char **argv)
{
	if (argc != 2) {
    	fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
    	return (1);
    }
	
    FILE *f=fopen(argv[1],"r");
    XML_Parser parser;
    size_t size;
    char *xmltext;
    
    
    //ParserCreation
    parser = XML_ParserCreate(NULL);
    if (parser == NULL) {
    	fprintf(stderr, "Parser not created\n");
    	return (1);
    }
    
    // Tell expat to use functions start() and end() each times it encounters the start or end of an element.
    XML_SetElementHandler(parser, start, end);    
    
    //Open Xml File
    xmltext = malloc(MAXCHARS);
    size = fread(xmltext, sizeof(char), MAXCHARS, f);
    
    //Parse Xml Text
    if (XML_Parse(parser, xmltext, strlen(xmltext), XML_TRUE) ==
        XML_STATUS_ERROR) {
    	fprintf(stderr,
    		"Cannot parse , file may be too large or not well-formed XML\n");
    	return (1);
    }
    
    //Close
    fclose(f);
    XML_ParserFree(parser);
    fprintf(stderr, "Successfully parsed %i characters !\n", (int)size);
    return (0);
}

 

