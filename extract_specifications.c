#include <expat.h>
#include <stdio.h>
#include <string.h>

//Creation specification stripped file from ODK XML
//FieldName,Type,Minimum,Maximum,Precision

/* Keep track of the current level in the XML tree */
int in_bind = 0;

#define MAXCHARS 1000000

void
start(void *data, const char *el, const char **attr)
{
    char *form_name = (char *)data;
    
    //Looking for bind elements
	if ((!strncasecmp("bind",el,strlen("bind")))||(!strncasecmp("xf:bind",el,strlen("xf:bind")))) {
		in_bind++;

		//printf("%s", el); //Element name

		//Attributes
		char *node_name = "", *node_type = "", *node_constraint = "";
		
		
		int i ;
		for (i = 0; attr[i]; i += 2) {
			//printf(" %s='%s'", attr[i], attr[i + 1]); //Display attributes
			
			//Looking for attribute nodeset
			if (!strncasecmp("nodeset",attr[i],strlen("nodeset"))) {
					char *last_slash = strrchr(attr[i+1], '/');
					node_name  = calloc (strlen(last_slash), sizeof(char*));
					memcpy (node_name, last_slash+1, strlen(last_slash));
					//printf("%s||", attr[i + 1]);
					//printf("%s", node_name);

			}
			//Looking for attribute type
			if (!strncasecmp("type",attr[i],strlen("type"))) {
					node_type  = calloc (strlen(attr[i + 1]), sizeof(char*));
					memcpy (node_type, attr[i + 1], strlen(attr[i + 1]));
					//printf(":%s", node_type);
			}
			
			//Looking for attribute constraint
			if (!strncasecmp("constraint",attr[i],strlen("constraint"))) {
					node_constraint  = calloc (strlen(attr[i + 1]), sizeof(char*));
					memcpy (node_constraint, attr[i + 1], strlen(attr[i + 1]));
					//printf(":%s", node_constraint);
			}

		}
		printf("%s:%s", node_name,node_type);
		if (strlen(node_constraint)) {
			printf(":%s",node_constraint);
		} else {
			printf(":0");
		}
		printf("\n");
	}
    
}   			/* End of start handler */

void
characterdata(void *data, const char *el, int len)
{
	if (in_bind){  
		
		//Warning, can be dangerous with buffer overflow attack
		/*int i;
		for(i=0;i<len;i++){
			if ((el[i] != ' ')&&(el[i] != '\n')) printf("%c",el[i]);
		}*/
		
		//printf("%.*s", len, el);
		//printf("\n");
	}
}

void
end(void *data, const char *el)
{
    //Not in a bind element anymore    
	if ((!strncasecmp("bind",el,strlen("bind")))||(!strncasecmp("xf:bind",el,strlen("xf:bind")))) {
		in_bind--;
	}
}   			/* End of end handler */

int
main(int argc, char **argv)
{
	if (argc != 3) {
    	fprintf(stderr, "Usage: %s formname,filename\n", argv[0]);
    	return (1);
    }
	
    char *form_name=argv[1];
    FILE *f=fopen(argv[2],"r");
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
    // Tell expat to use function characterdata() each times it encounters the element data
    XML_SetCharacterDataHandler(parser, characterdata);
    // Set UserData to the form_name
    XML_SetUserData(parser, form_name);
    
    
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
    fprintf(stdout, "Successfully parsed %i characters !\n", size);
    return (0);
}

 

