#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

int extractTweet(char *s)
{
  while(*s) {
    switch (*s) {
    case '"': return 0;
    case '&': 
      if (!strncmp(s,"&amp;",5)) { printf("&"); s+=4; }
      else if (!strncmp(s,"&gt;",4)) { printf(">"); s+=3; }
      else if (!strncmp(s,"&lt;",4)) { printf("<"); s+=3; }
      else { 
	s[16]=0;
	fprintf(stderr,"Unrecognised & code: %s...\n",s);
	exit(-1);
      }
      break;
    case '\\':
      s++;
      switch(*s) {
      case '\'': case '"': case '/':
	/* remove escaping from these characters */
	printf("%c",*s);
	break;
      case 'n': case 'r': case '\\': 
      default:
	/* Keep escaped */
	printf("\\%c",*s);
	break;
      }
      break;
    default:
      printf("%c",*s);
      break;
    }
    s++;
  }
  return 0;
}

int main()
{
  int i;
  char line[8192];

  line[0]=0; fgets(line,8192,stdin);
  while(line[0]) {
    for(i=0;line[i]&&line[i+10];i++)
      if (!strncasecmp(&line[i],"\"text\":\"",7)) {
	// Line is for the creation of a tweet
	extractTweet(&line[8+i]); printf("\n");
	break;
      }    

    line[0]=0; fgets(line,8192,stdin);
  }
  return 0;
}
