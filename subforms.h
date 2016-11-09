
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

extern char recipe_error[1024];

int record_free(struct record *r);
struct record *parse_stripped_with_subforms(char *in,int in_len);
int compress_record_with_subforms(struct recipe *recipe,struct record *r,
				  range_coder *c,stats_handle *h);
