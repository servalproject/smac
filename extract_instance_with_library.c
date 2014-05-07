#include <expat.h>
#include <stdio.h>
#include <string.h>

/* Keep track of the current level in the XML tree */
int Depth;
int in_instance = 0;
//char *Cdata;
#define MAXCHARS 1000000

void
start(void *data, const char *el, const char **attr)
{
    char *form_name = (char *)data;
	if (!strncasecmp(form_name,el,strlen(form_name))) {
		printf("##Found start of instance\n");
		in_instance++;
	}
	
	if (in_instance){  
		Depth++;
	}
    
}   			/* End of start handler */

void
characterdata(void *data, const char *el, int len)
{
	if (in_instance){  
		//Warning, can be dangerous with buffer overflow attack
		int i;
		for(i=0;i<len;i++){
			if ((el[i] != ' ')&&(el[i] != '\n')) printf("%c",el[i]);
			//Cdata = el;
		}
		
		
		//printf("%.*s", len, el);
		//printf("\n");
	}
}
void

end(void *data, const char *el)
{
    
    
    if (in_instance){  
		//Depth
		int i;
		for (i = 0; i < Depth; i++)
			printf("  ");

		//Element name
		printf("%s", el);

		//Attributes
		/*for (i = 0; attr[i]; i += 2) {
			printf(" %s='%s'", attr[i], attr[i + 1]);
		}*/

		printf("\n");
	}
	
	if (in_instance) Depth--;
    
    char *form_name = (char *)data;
    if (!strncasecmp(form_name,el,strlen(form_name))) {
		printf("##Found End of instance\n");
		in_instance--;
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
    XML_SetCharacterDataHandler(parser, characterdata);
    XML_SetUserData(parser, form_name);
    //Open XML File
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

 

