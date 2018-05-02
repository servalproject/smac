#ifndef SUCCINCT_RECIPE_H
#define SUCCINCT_RECIPE_H

#include <stdint.h>

#include "packed_stats.h"
#include "arithmetic.h"

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
// For Magpi sub-forms
#define FIELDTYPE_SUBFORM 14

#define MAX_ENUM_VALUES 32

struct enum_value{
  struct enum_value *next;
  char value[];
};

struct field {
  struct field *next;
  int type;
  int minimum;
  int maximum;
  int precision; // meaning differs based on field type
  struct enum_value *enum_value;
  int enum_count;
  char name[];
};

struct recipe {
  struct recipe *next;
  unsigned char formhash[6];
  struct field *field_list;
  int field_count;
  char formname[];
};

int recipe_main(int argc,char *argv[]);
struct recipe *recipe_read_from_file(char *filename);
struct recipe *recipe_read(char *formname,char *buffer,int buffer_size);
void recipe_free(struct recipe *recipe);
int stripped2xml(char *stripped,int stripped_len,char *template,int template_len,char *xml,int xml_size);
int xml2stripped(const char *form_name, const char *xml,int xml_len,char *stripped,int stripped_size);

int generateMaps(char *recipeDir, char *outputDir);

int xhtmlToRecipe(const char *xmltext, char **template_text, char *recipe_text[1024], char *form_versions[1024]);

int xmlToRecipe(char *xmltext,int size,char *formname,char *formversion,
		char *recipetext,int *recipeLen,
		char *templatetext,int *templateLen);

int recipe_encode_field(struct field *field, stats_handle *stats, range_coder *c, char *value);
int recipe_parse_fieldtype(const char *name);
const char *recipe_field_type_name(int f);

#endif
