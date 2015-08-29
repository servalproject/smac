// min,max set inclusive bound
#define FIELDTYPE_INTEGER 0
// precision specifies bits of precision. currently only 32 is supported.
#define FIELDTYPE_FLOAT 1
// precision sets number of decimal places.
// gets encoded by multiplying value,min and max by 10^precision, and then encoding as an integer.
#define FIELDTYPE_FIXEDPOINT 2
#define FIELDTYPE_BOOLEAN 3
// precision is bits of time of day encoded.  17 = 1 sec granularity
#define FIELDTYPE_TIMEOFDAY 4
// precision is bits of date encoded.  32 gets full UNIX Julian seconds.
// 16 gets resolution of ~ 1 day.
// with min set appropriately, 25 gets 1 second granularity within a year.
#define FIELDTYPE_DATE 5
// precision is bits of precision in coordinates
#define FIELDTYPE_LATLONG 6
// min,max refer to size limits of text field.
// precision refers to minimum number of characters to encode if we run short of space.
#define FIELDTYPE_TEXT 7
// precision is the number of bits of the UUID
// we just pull bits from the left of the UUID
#define FIELDTYPE_UUID 8
// Like time of day, but takes a particular string format of date
#define FIELDTYPE_TIMEDATE 9
#define FIELDTYPE_ENUM 10
// Magpi UUID is 64-bit value followed by time since UNIX epoch
#define FIELDTYPE_MAGPIUUID 11
// Like time of day, but takes a particular string format of date
// (a slightly different format for magpi)
#define FIELDTYPE_MAGPITIMEDATE 12
// Like _ENUM, but allows multiple choices to be selected
#define FIELDTYPE_MULTISELECT 13

#define MAX_ENUM_VALUES 1024
struct field {
  char *name;

  int type;
  int minimum;
  int maximum;
  int precision; // meaning differs based on field type
  char *enum_values[MAX_ENUM_VALUES];
  int enum_count;
};

struct recipe {
  char formname[1024];
  unsigned char formhash[6];

  struct field fields[1024];
  int field_count;
};

int recipe_main(int argc,char *argv[],stats_handle *h);
struct recipe *recipe_read_from_file(char *filename);
struct recipe *recipe_read(char *formname,char *buffer,int buffer_size);
int stripped2xml(char *stripped,int stripped_len,char *template,int template_len,char *xml,int xml_size);
int xml2stripped(const char *form_name, const char *xml,int xml_len,char *stripped,int stripped_size);

int generateMaps(char *recipeDir, char *outputDir);

int xhtmlToRecipe(char *xmltext,int size,char *formname,char *formversion,
		  char *recipetext,int *recipeLen,
		  char *templatetext,int *templateLen);
int xmlToRecipe(char *xmltext,int size,char *formname,char *formversion,
		char *recipetext,int *recipeLen,
		char *templatetext,int *templateLen);
