/*
Copyright (C) 2012 Paul Gardner-Stephen
 
This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
 
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
 
You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>

#include "charset.h"
#include "visualise.h"
#include "arithmetic.h"
#include "packed_stats.h"
#include "smac.h"
#include "recipe.h"
#include "log.h"

int processFile(FILE *f,FILE *contentXML,stats_handle *h);

int lines=0;
double worstPercent=0,bestPercent=100;
long long total_compressed_bits=0;
long long total_uncompressed_bits=0;
long long total_length_millibits=0;

long long total_messages=0;

long long total_stats3_bytes=0;

long long stats3_compress_us=0;
long long stats3_decompress_us=0;

double comp_by_size_percent[104];
unsigned int comp_by_size_count[104];
unsigned int percent_count[104];

int outputHistograms()
{
  int i;
  FILE *f;

  f=fopen("compressed_size_hist.csv","w");
  fprintf(f,"compressed_size_in_percent;count;cumulative_fraction\n");
  long long total=0;
  long long running_total=0;
  for(i=0;i<=101;i++) total+=percent_count[i];
  for(i=0;i<=101;i++) {
    running_total+=percent_count[i];
    fprintf(f,"%d;%u;%f\n",i,percent_count[i],
	    running_total*100.0/total);
  }
  fclose(f);

  f=fopen("compressed_size_versus_uncompressed_length.csv","w");
  fprintf(f,"minlength;compressed_size_in_percent;count\n");
  for(i=0;i<=102;i++) {
    fprintf(f,"%d;%f;%d\n",i*10,
	    comp_by_size_percent[i]/comp_by_size_count[i],comp_by_size_count[i]);
  }
  fclose(f);
  return 0;
}

int usage()
{
  fprintf(stderr,
	  "smac usage:\n"
	  "  smac recipe <recipe sub-command>\n"
	  "  smac babble\n"
	  "  smac test <files>\n");
  exit(-1);
}

static int recipe_main(int argc, char *argv[]) {
  if (argc <= 2) {
    LOGE("'smac recipe' command requires further arguments.");
    return -1;
  }

  if (!strcasecmp(argv[2], "parse")) {
    if (argc <= 3) {
      LOGE("'smac recipe parse' requires name of recipe to load.");
      return (-1);
    }
    struct recipe *recipe = recipe_read_from_file(argv[3]);
    if (!recipe) {
      return (-1);
    }
    LOGI("recipe=%p", recipe);
    LOGI("recipe->field_count=%d", recipe->field_count);
  } else if (!strcasecmp(argv[2], "compress")) {
    if (argc <= 5) {
      LOGE("'smac recipe compress' requires recipe directory, input "
           "and output files.");
      return (-1);
    }
    stats_handle *h=stats_new_handle("stats.dat");
    if (!h) {
      char working_dir[1024];
      getcwd(working_dir,1024);
      LOGE("Could not read stats.dat (pwd='%s').",working_dir);
      return -1;
    }
    /* Preload tree for speed */
    stats_load_tree(h);

    LOGI(
        "Test-Dialog: About to compress the stripped data file: %s into: %s ",
        argv[4], argv[5]);

    int r = recipe_compress_file(h, argv[3], argv[4], argv[5]);
    stats_handle_free(h);
    return r;
  } else if (!strcasecmp(argv[2], "map")) {
    if (argc <= 4) {
      LOGE("usage: smac map <recipe directory> <output directory>");
      return (-1);
    }
    return generateMaps(argv[3], argv[4]);
  } else if (!strcasecmp(argv[2], "encrypt")) {
    if (argc <= 6) {
      LOGE("usage: smac encrypt <file> <MTU> <output directory> "
           "<public key hex>");
      return (-1);
    }
    return encryptAndFragment(argv[3], atoi(argv[4]), argv[5], argv[6]);
  } else if (!strcasecmp(argv[2], "decrypt")) {
    if (argc <= 5) {
      LOGE("usage: smac decrypt <input directory> <output "
           "directory> <pass phrase>");
      return (-1);
    }
    return defragmentAndDecrypt(argv[3], argv[4], argv[5]);
  } else if (!strcasecmp(argv[2], "create")) {
    if (argc <= 3) {
      LOGE("usage: smac recipe create <XML form> ");
      return (-1);
    }
    return recipe_create(argv[3]);
  } else if (!strcasecmp(argv[2], "xhcreate")) {
    if (argc <= 4) {
      LOGE("usage: smac recipe xhcreate <recipe directory> <XHTML form>");
      return (-1);
    }
    LOGI("Test-Dialog: About to XHCreate/Create recipe file(s) from the "
           "XHTML form %s ",
           argv[3]);
    return xhtml_recipe_create(argv[3], argv[4]);
  } else if (!strcasecmp(argv[2], "decompress")) {
    if (argc <= 5) {
      LOGE("usage: smac recipe decompress <recipe directory> "
           "<succinct data message> <output directory>");
      return (-1);
    }
    stats_handle *h=stats_new_handle("stats.dat");
    if (!h) {
      char working_dir[1024];
      getcwd(working_dir,1024);
      LOGE("Could not read stats.dat (pwd='%s').",working_dir);
      return -1;
    }
    /* Preload tree for speed */
    stats_load_tree(h);
    // If succinct data message is a directory, try decompressing all files in
    // it.
    struct stat st;
    if (stat(argv[4], &st)) {
      perror("Could not stat succinct data file/directory.");
      return (-1);
    }
    if (st.st_mode & S_IFDIR) {
      // Directory: so process each file inside
      DIR *dir = opendir(argv[4]);
      struct dirent *de = NULL;
      char filename[1024];
      int e = 0;
      while ((de = readdir(dir)) != NULL) {
        snprintf(filename, 1024, "%s/%s", argv[4], de->d_name);
        LOGI("Trying to decompress %s as succinct data message", filename);
        if (recipe_decompress_file(h, argv[3], filename, argv[5]) == -1) {
          LOGI("Failed to decompress %s as succinct data message", filename);
          e++;
        } else {
          LOGE("Decompressed %s", filename);
          LOGI("Decompressed succinct data message %s", filename);
        }
      }
      closedir(dir);
      LOGI("Finished extracting files.  %d failures.", e);
      stats_handle_free(h);
      if (e)
        return 1;
      else
        return 0;
    } else {
      LOGI("Test-Dialog: About to decompress the succinct data (SD) file: %s "
             "into: %s",
             argv[4], argv[5]);
      int r = recipe_decompress_file(h, argv[3], argv[4], argv[5]);
      stats_handle_free(h);
      return r;
    }
  } else if (!strcasecmp(argv[2], "strip")) {
    char stripped[65536];
    char xml_data[1048576];
    int xml_len = 0;
    if (argc < 4) {
      LOGE("usage: smac recipe strip <xml input> [stripped output].");
      return -1;
    }
    LOGI("Test-Dialog: About to strip the XML record: %s into the following "
           "file: %s",
           argv[3], argv[4]);
    xml_len = recipe_load_file(argv[3], xml_data, sizeof(xml_data));
    int stripped_len =
        xml2stripped(NULL, xml_data, xml_len, stripped, sizeof(stripped));
    if (stripped_len < 0) {
      LOGE("Failed to strip '%s'", argv[3]);
      return -1;
    }
    if (argv[4] == NULL)
      printf("%s", stripped);
    else {
      FILE *f = fopen(argv[4], "w");
      if (!f) {
        LOGE("Failed to write stripped output to '%s'", argv[4]);
        return -1;
      }
      fprintf(f, "%s", stripped);
      fclose(f);
      return 0;
    }
  } else if (!strcasecmp(argv[2], "rexml")) {
    char stripped[8192];
    int stripped_len = 0;
    char template[65536];
    int template_len = 0;
    char xml[65536];
    if (argc < 5) {
      LOGE("usage: smac recipe rexml <stripped> <template> [xml output].");
      return -1;
    }
    stripped_len = recipe_load_file(argv[3], stripped, sizeof(stripped));
    if (stripped_len < 0) {
      LOGE("Failed to read '%s'", argv[3]);
      return -1;
    }
    template_len = recipe_load_file(argv[4], template, sizeof(template));
    if (template_len < 0) {
      LOGE("Failed to read '%s'", argv[4]);
      return -1;
    }
    LOGI("Test-Dialog: About to rexml the stripped file: %s into the "
           "following file: %s ",
           argv[3], argv[5]);
    int xml_len = stripped2xml(stripped, stripped_len, template, template_len,
                               xml, sizeof(xml));
    if (xml_len < 0) {
      LOGE("Failed to rexml '%s'", argv[3]);
      return -1;
    }
    if (argv[5] == NULL)
      printf("%s", xml);
    else {
      FILE *f = fopen(argv[5], "w");
      if (!f) {
        LOGE("Failed to write rexml output to '%s'", argv[5]);
        return -1;
      }
      fprintf(f, "%s", xml);
      fclose(f);
      return 0;
    }

  } else {
    LOGE("unknown 'smac recipe' sub-command '%s'.", argv[2]);
    return -1;
  }

  return 0;
}

int main(int argc,char *argv[])
{
  int i;

  if (argc<2) usage();
  
  /* Clear statistics */
  for(i=0;i<104;i++) {
    comp_by_size_percent[i]=0;
    comp_by_size_count[i]=0;
    percent_count[i]=0;
  }

  if (argc>1) {
    if (!strcasecmp(argv[1],"recipe")) return recipe_main(argc,argv);
  }
  
  // XXX - Evil, evil hack.  Should pass in the path to the stats file for
  // all invocations.
  stats_handle *h=stats_new_handle("stats.dat");
  if (!h) {
    char working_dir[1024];
    getcwd(working_dir,1024);
    fprintf(stderr,"Could not read stats.dat (pwd='%s').\n",working_dir);
    exit(-1);
  }
  /* Preload tree for speed */
  stats_load_tree(h);

  if (!strcmp("babble",argv[1])) {
    fprintf(stderr,"You didn't provide me any messages to test, so I'll make some up.\n");
#ifndef ANDROID
    range_coder *c=range_new_coder(2048);
    while(1)
      {
	int i;
	for(i=0;i<2048;i++) c->bit_stream[i]=random()&0xff;
	// make sure it gets treated as a compressed message.
	c->bit_stream[0]&=0x3f; 
	c->bit_stream[0]|=0x80; 
	c->low=0; c->high=0xffffffff;
	c->bit_stream_length=8192;
	c->bits_used=0;
	range_decode_prefetch(c);
	char out[2048];
	int lenout;
	stats3_decompress_bits(c,out,&lenout,h,NULL);
	printf("%s\n",out);
      }
#endif
    exit(-1);
  }

  if (!strcmp(argv[1],"test")) {  
    FILE *f;
    
    FILE *contentXML=fopen("content.xml","w+");
    beginContentXML(contentXML);
    
    int argn;
    
    for(argn=2;argn<argc;argn++) {
      if (strcmp(argv[argn],"-")) f=fopen(argv[argn],"r"); else f=stdin;
      if (!f) {
	fprintf(stderr,"Failed to open `%s' for input.\n",argv[1]);
	exit(-1);
      } else {
	processFile(f,contentXML,h);
	fclose(f);
      }
    }
    
    endContentXML(contentXML);
    fclose(contentXML);
    
    printf("Summary:\n");
    printf("         compressed size: %f%% (bit oriented)\n",
	   total_compressed_bits*100.0/total_uncompressed_bits);
    printf("         compressed size: %f%% (byte oriented)\n",
	   total_stats3_bytes*100.0/(total_uncompressed_bits/8.0));
    printf("       uncompressed bits: %lld\n",total_uncompressed_bits);
    printf("        compressed bytes: %lld\n",total_stats3_bytes);
    printf("         compressed bits: %lld\n",total_compressed_bits);
    printf("    length-encoding bits: %lld\n",h->total_length_bits);
    printf("     model-encoding bits: %lld\n",h->total_model_bits);
    printf("      case-encoding bits: %lld\n",h->total_case_bits);
    printf("     alpha-encoding bits: %lld\n",h->total_alpha_bits);
    if (h->total_unicode_chars)
      printf("   avg unicode bits/char: %.2f\n",
	     h->total_unicode_millibits/h->total_unicode_chars/1000.0);
    printf("  nonalpha-encoding bits: %lld\n",h->total_nonalpha_bits);
    printf("\n");
    printf("stats3 compression time: %lld usecs (%.1f messages/sec, %f MB/sec)\n",
	   stats3_compress_us,1000000.0/(stats3_compress_us*1.0/total_messages),total_uncompressed_bits*0.125/stats3_compress_us);
    printf("stats3 decompression time: %lld usecs (%.1f messages/sec, %f MB/sec)\n",
	   stats3_decompress_us,1000000.0/(stats3_decompress_us*1.0/total_messages),total_uncompressed_bits*0.125/stats3_decompress_us);
    
    outputHistograms();
  } else {
    usage();
  }
  
  return 0;
}

long long current_time_us()
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_usec+tv.tv_sec*1000000LL;
}

int processFile(FILE *f,FILE *contentXML,stats_handle *h)
{
  char m[1024]; // raw message, no pre-processing
  long long now;

  time_t lastReport=time(0);

  m[0]=0; fgets(m,1024,f);
  
  while(m[0]) {    
    if (time(0)>lastReport+4) {
      fprintf(stderr,"Processed %d lines.\n",lines);
      lastReport=time(0);
    }

    /* chop newline */
    m[strlen(m)-1]=0;

    total_messages++;

    double entropyLog[1025];
    range_coder *c=range_new_coder(2048);
    now = current_time_us();
    stats3_compress_bits(c,m,strlen(m),h,entropyLog);
    stats3_compress_us+=current_time_us()-now;
    
    if (total_messages<1000)
      visualiseMessage(contentXML,(unsigned char *)m,
		       c->bits_used*100.0/(strlen(m)*8),entropyLog);

    total_compressed_bits+=c->bits_used;
    total_uncompressed_bits+=strlen(m)*8;

    /* Also count whole bytes for comparison with SMAZ etc */
    total_stats3_bytes+=c->bits_used>>3;
    if (c->bits_used&7) total_stats3_bytes++;

    double percent=c->bits_used*100.0/(strlen(m)*8);   
    if (percent<bestPercent) bestPercent=percent;
    if (percent>worstPercent) worstPercent=percent;

    {
      int bytes_used=(c->bits_used>>3)+((c->bits_used&7)?1:0);
      double percent=bytes_used*100.0/(strlen(m));   
      /* Calculate histograms of compression performance */
      if (strlen(m)<=1024) {
	comp_by_size_percent[strlen(m)/10]+=percent;
	comp_by_size_count[strlen(m)/10]++;
      }
      if (percent>=0&&percent<=100)
	percent_count[(int)percent]++;
      if (percent>100) percent_count[101]++;
      // if ((int)percent==66) fprintf(stderr,"%s\n",m);
    }

    /* Verify that compression worked */
    {
      int lenout=0;
      char mout[1025];
      range_coder *d=range_coder_dup(c);
      d->bit_stream_length=d->bits_used;
      d->bits_used=0;
      d->low=0; d->high=0xffffffff;
      
      now=current_time_us();
      range_decode_prefetch(d);
      stats3_decompress_bits(d,mout,&lenout,h,NULL);
      stats3_decompress_us+=current_time_us()-now;

      if (lenout!=strlen(m)) {	
	printf("Verify error: length mismatch: decoded = %d, original = %d\n",lenout,(int)strlen(m));
	printf("   Input: [%s]\n  Output: [%s]\n",m,mout);
      	exit(-1);
      } else if (strcasecmp(m,mout)) {
	printf("Verify error: even ignoring case, the messages do not match.\n");
	printf("   Input: [%s]\n  Output: [%s]\n",m,mout);
	int i;
	printf(" input as utf8: ");
	for(i=0;i<strlen(m);i++)
	  printf("<%02x>",(unsigned char)m[i]);
	printf("\n");
	printf("output as utf8: ");
	for(i=0;i<strlen(mout);i++)
	  printf("<%02x>",(unsigned char)mout[i]);
	printf("\n");
	exit(-1);
      } else if (strcmp(m,mout)) {
	printf("Verify error: messages differ in case only.\n");
	printf("   Input: [%s]\n  Output: [%s]\n",m,mout);
	exit(-1);
      }

      range_coder_free(d);
    }

    range_coder_free(c);

    lines++;
    m[0]=0; fgets(m,1024,f);
  }
  return 0;
}
