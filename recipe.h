
struct field {
  char *name;

  int type;
  int minimum;
  int maximum;
  int precision; // meaning differs based on field type
  char *enum_values[32];
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
int stripped2xml(char *stripped,int stripped_len,char *template,int template_len,char *xml,int xml_size);
int xml2stripped(const char *form_name, const char *xml,int xml_len,char *stripped,int stripped_size);

