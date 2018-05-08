#ifndef SUCCINCT_RECIPE_H
#define SUCCINCT_RECIPE_H

#include <stdint.h>
#include <stdbool.h>

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
  unsigned char formhash[6];
  struct field *field_list;
  int field_count;
  char formname[];
};


struct record_field {
  char *key;
  char *value;
  struct record *subrecord;
};

#define MAX_FIELDS 1024
struct record {
  int field_count;
  struct record_field fields[MAX_FIELDS];
  struct record *parent;
};

typedef struct recipe *(*find_recipe) (const char *formid, void *data);

int record_free(struct record *r);
struct record *parse_stripped_with_subforms(char *in,int in_len);

int recipe_create(char *input);
int xhtml_recipe_create(char *recipe_dir, char *input);
struct recipe *recipe_read_from_file(char *filename);
struct recipe *recipe_read(char *formname,char *buffer,int buffer_size);
void recipe_free(struct recipe *recipe);
int recipe_load_file(char *filename, char *out, int out_size);
struct recipe *recipe_read_from_specification(char *xmlform_c);
struct recipe *recipe_find_recipe(char *recipe_dir, unsigned char *formhash);

int recipe_parse_fieldtype(const char *name);
const char *recipe_field_type_name(int f);

int recipe_compress_file(stats_handle *h, char *recipe_dir, char *input_file, char *output_file);
int encryptAndFragment(char *filename, int mtu, char *outputdir,
                       char *publickeyhex);
int recipe_encode_field(struct field *field, stats_handle *stats, range_coder *c, char *value);
int compress_record_with_subforms(find_recipe find_recipe, void *context, struct recipe *recipe,
				  struct record *r, range_coder *c, stats_handle *h);
int recipe_compress(stats_handle *h, find_recipe find_recipe, void *context, struct recipe *recipe,
                    char *in, int in_len, unsigned char *out, int out_size);

int recipe_decompress_file(stats_handle *h, char *recipe_dir, char *input_file,
                           char *output_directory);
int defragmentAndDecrypt(char *inputdir, char *outputdir, char *passphrase);
int recipe_decompress(stats_handle *h, struct recipe *recipe, char *recipe_dir,
                      char *out, int out_size, char *recipe_name,
                      range_coder *c, bool is_subform, bool is_record);

int stripped2xml(char *stripped,int stripped_len,char *template,int template_len,char *xml,int xml_size);
int xml2stripped(const char *form_name, const char *xml,int xml_len,char *stripped,int stripped_size);

int generateMaps(char *recipeDir, char *outputDir);

int xhtmlToRecipe(const char *xmltext, char **template_text, char *recipe_text[1024], char *form_versions[1024]);

int xmlToRecipe(char *xmltext,int size,char *formname,char *formversion,
		char *recipetext,int *recipeLen,
		char *templatetext,int *templateLen);


#endif
