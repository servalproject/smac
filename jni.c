#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <stdlib.h>
#include <jni.h>
#include <android/log.h>
 
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "libsmac", __VA_ARGS__))

#include "charset.h"
#include "visualise.h"
#include "arithmetic.h"
#include "packed_stats.h"
#include "smac.h"
#include "recipe.h"
#define MAX_FRAGMENTS 64
int encryptAndFragmentBuffer(unsigned char *in_buffer,int in_len,
			     char *fragments[MAX_FRAGMENTS],int *fragment_count,
			     int mtu,char *publickeyhex,int debug);
void recipe_free(struct recipe *recipe);
int recipe_compress(stats_handle *h,struct recipe *recipe,
		    char *in,int in_len, unsigned char *out, int out_size);

jobjectArray error_message(JNIEnv * env, char *message)
{
  LOGI("%s",message);
      
  jobjectArray result=
    (jobjectArray)(*env)->NewObjectArray(env,2,
					 (*env)->FindClass(env,"java/lang/String"),
					 (*env)->NewStringUTF(env,""));
  (*env)->SetObjectArrayElement(env,result,0,(*env)->NewStringUTF(env,"ERROR"));
  (*env)->SetObjectArrayElement(env,result,1,(*env)->NewStringUTF(env,message));

  return result;
}

char form_name[1024],form_version[1024];

JNIEXPORT jobjectArray JNICALL Java_org_servalproject_succinctdata_jni_xml2succinctfragments
(JNIEnv * env, jobject jobj,
 jstring xmlforminstance,
 jstring xmlformspecification,
 jstring formname,
 jstring formversion,
 jstring succinctpath,
 jstring smacdat,
 jint mtu,
 jint debug)
{
  LOGI("  xml2succinctfragments ENTRY");
  
  const char *xmldata= (*env)->GetStringUTFChars(env,xmlforminstance,0);
  LOGI("  xml2succinctfragments E2");
  const char *formname_c= NULL; // (*env)->GetStringUTFChars(env,formname,0);
  LOGI("  xml2succinctfragments E3");
  const char *formversion_c=  NULL; // (*env)->GetStringUTFChars(env,formversion,0);
  LOGI("  xml2succinctfragments E4");
  const char *path= NULL; // (*env)->GetStringUTFChars(env,succinctpath,0);
  LOGI("  xml2succinctfragments E5");
  const char *xmlform_c= (*env)->GetStringUTFChars(env,xmlformspecification,0);
  LOGI("  xml2succinctfragments E6");
  const char *smacdat_c= (*env)->GetStringUTFChars(env,smacdat,0);
  LOGI("  xml2succinctfragments E7");
  
  char stripped[65536];
  unsigned char succinct[1024];
  int succinct_len=0;
  char filename[1024];

  int magpi_mode=0;

  LOGI("  xml2succinctfragments checkpoing 1");
  
  // Automatically detect Magpi forms versus ODK ones.
  // Magpi forms are HTML documents, where as ODK uses XML ones.
  if (xmlform_c&&(!strncasecmp("<html",xmlform_c,5))) magpi_mode=1;

  // Read public key hex

  LOGI("  xml2succinctfragments checkpoing 2: magpi_mode = %d",magpi_mode);
  
  // Default to public key of Serval succinct data server
  char publickeyhex[1024]="74f3a36029b0e60084d42bd9cafa3f2b26fe802b0a6f024ff00451481c9bba4a";

  if (path&&formname_c&&formversion_c)
  {
    LOGI("recipe: Expecting public key file");
    snprintf(filename,1024,"%s/%s.%s.publickey",path,formname_c,formversion_c);
    LOGI("recipe: Opening recipient public key file %s",filename);

    FILE *f=fopen(filename,"r");
    if (f) {
      int r=fread(publickeyhex,1,1023,f);
      if (r<64) {
	char message[1024];
	snprintf(message,1024,"Failed to read from public key file");
	return error_message(env,message);
	LOGI("recipe: failed to load publickeyhex from file (using default value)");
      }
      publickeyhex[r]=0;
    }

    // Trim CR/LF from end
    if (publickeyhex[0]) {
      while(publickeyhex[strlen(publickeyhex)-1]<' ')
	publickeyhex[strlen(publickeyhex)-1]=0;
    }
    
    fclose(f);
  }
  
  LOGI("recipe: have publixkeyhex = '%s'",publickeyhex);

  struct recipe *recipe=NULL;
  
  if (xmlformspecification==NULL||xmlform_c==NULL||!strlen(xmlform_c)) {
    // Read recipe file
    snprintf(filename,1024,"%s/%s.%s.recipe",path,formname_c,formversion_c);
    LOGI("Opening recipe file %s",filename);
    recipe=recipe_read_from_file(filename);
    if (!recipe) {
      char message[1024];
      snprintf(message,1024,"Could not read recipe file %s",filename);
      return error_message(env,message);
    }    
  } else {
    // Create recipe from form specification
    char recipetext[65536];
    int recipetextLen=65536;
    char templatetext[65536];
    int templatetextLen=65536;

    char the_form_name[1024];
    char the_form_version[1024];

    LOGI("Using form specification to create recipe on the fly (magpi_mode=%d, form spec = %d bytes)",
	 magpi_mode,strlen(xmlform_c));

    LOGI("Form specification is: %s",xmlform_c);
    
    int r;
    if (magpi_mode) {
      r=xhtmlToRecipe(xmlform_c,strlen(xmlform_c),
		      the_form_name,the_form_version,
		      recipetext,&recipetextLen,
		      templatetext,&templatetextLen);
      // Magpi forms are identified solely by the numeric formid
      strcpy(the_form_name,the_form_version);
      strcpy(the_form_version,"this should not be used");
    }
    else
      r=xmlToRecipe(xmlform_c,strlen(xmlform_c),
		      the_form_name,the_form_version,
		      recipetext,&recipetextLen,
		      templatetext,&templatetextLen);
    if (r) {
      return error_message(env,"Could not create recipe from form specification");
    }

    LOGI("Have %d bytes of recipe text (name='%s', version='%s')",
	 recipetextLen,the_form_name,the_form_version);
    if (recipetextLen<10) return error_message(env,"Could not convert form specification to recipe");

    
    formname_c=form_name; formversion_c=form_version;
    recipe = recipe_read(the_form_name,recipetext,recipetextLen);    
    if (!recipe) {
      char message[1024];
      snprintf(message,1024,"Could not set recipe");
      LOGI("Failed to read recipe from buffer");
      LOGI("Recipe is:\n%s",recipetext);
      return error_message(env,message);
    }

    LOGI("Read recipe from buffer (%d bytes). Hash = %02x%02x%02x%02x%02x%02x",
	 recipetextLen,
	 recipe->formhash[0],recipe->formhash[1],recipe->formhash[2],
	 recipe->formhash[3],recipe->formhash[4],recipe->formhash[5]
	 );
    LOGI("Recipe is:\n%s\n",recipetext);
  }

  LOGI("About to run xml2stripped");
  
  // Transform XML to stripped data first.
  int stripped_len;

  stripped_len = xml2stripped(formname_c,xmldata,strlen(xmldata),stripped,sizeof(stripped));

  LOGI("Stripped data is %d bytes long",stripped_len);
  
  if (stripped_len>0) {
    // Produce succinct data

    LOGI("About to read stats file %s",smacdat_c);

    // Get stats handle
    stats_handle *h=stats_new_handle(smacdat_c);

    stats_load_tree(h);
    LOGI("Loaded entire stats tree");
    
    if (!h) {
      recipe_free(recipe);
      char message[1024];
      snprintf(message,1024,"Could not read SMAC stats file %s",smacdat_c);
      return error_message(env,message);
    }

    LOGI("Read stats, now about to call recipe_compresS()");

    // Compress stripped data to form succinct data
    succinct_len=recipe_compress(h,recipe,stripped,stripped_len,succinct,sizeof(succinct));

    LOGI("Binary succinct data is %d bytes long",succinct_len);

    // Clean up after ourselves
    stats_handle_free(h);
    recipe_free(recipe);
    char filename[1024];
    snprintf(filename,1024,"%s/%s.%s.recipe",path,formname_c,formversion_c);

    LOGI("Cleaned up after ourselves");

    if (succinct_len<1) {
      LOGI("Failed to compress XML - reporting error and exiting");
      char message[1024];
      snprintf(message,1024,"recipe_compess failed with recipe file %s. stripped_len=%d",filename,stripped_len);
      LOGI("Exiting due to failure to produce valid Succinct Data output.");
      return error_message(env,message);
    }
  } else {
    LOGI("Failed to strop XML using recipe file -- exiting.");
    recipe_free(recipe);
    char message[1024];
    snprintf(message,1024,"Failed to strip XML using recipe file %s.",filename);
    return error_message(env,message);
  }

  LOGI("Fragmenting succinct data record");
  char *fragments[MAX_FRAGMENTS];
  int fragment_count=0;
  encryptAndFragmentBuffer(succinct,succinct_len,fragments,&fragment_count,mtu,
			   publickeyhex,debug);

  LOGI("Succinct data formed into %d fragments",fragment_count);
  
  jobjectArray result=
    (jobjectArray)(*env)->NewObjectArray(env,fragment_count,
				      (*env)->FindClass(env,"java/lang/String"),
         (*env)->NewStringUTF(env,""));
  for(int i=0;i<fragment_count;i++) {
    (*env)->SetObjectArrayElement(env,result,i,(*env)->NewStringUTF(env,fragments[i]));
    free(fragments[i]);
  }
    
  return result;  
}

