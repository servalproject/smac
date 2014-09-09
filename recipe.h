
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
  struct field fields[1024];
  int field_count;
};

int recipe_main(int argc,char *argv[],stats_handle *h);
struct recipe *recipe_read_from_file(char *filename);

extern char recipe_error[1024];
