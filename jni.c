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

JNIEXPORT jbyteArray JNICALL Java_org_servalproject_succinctdata_jni_xml2succinct
(JNIEnv * env, jobject jobj,
 jstring xmlforminstance,
 jstring recipename,
 jstring succinctpath)
{
  const char *xmldata= (*env)->GetStringUTFChars(env,xmlforminstance,0);
  const char *recipefile= (*env)->GetStringUTFChars(env,recipename,0);
  const char *path= (*env)->GetStringUTFChars(env,succinctpath,0);
  
  char stripped[8192];
  unsigned char succinct[1024];
  int succinct_len=0;

  // Transform XML to stripped data first.

  int stripped_len=xml2stripped(recipefile,xmldata,strlen(xmldata),stripped,8192);

  if (stripped_len>0) {
    // Produce succinct data

    // Get stats handle
    char filename[1024];
    snprintf(filename,1024,"%s/smac.dat",path);
    stats_handle *h=stats_new_handle(filename);

    if (!h) {
      LOGI("Could not read SMAC stats file %s",filename);
      jbyteArray result=(*env)->NewByteArray(env, 0);
      return result;
    }

    // Read recipe file
    snprintf(filename,1024,"%s/%s.recipe",path,recipefile);
    struct recipe *recipe=recipe_read_from_file(filename);
    
    if (!recipe) {
      LOGI("Could not read recipe file %s",filename);
      jbyteArray result=(*env)->NewByteArray(env, 0);
      return result;
    }

    // Compress stripped data to form succinct data
    succinct_len=recipe_compress(h,recipe,stripped,stripped_len,succinct,sizeof(succinct));
    if (succinct_len<0) succinct_len=0;

    // Clean up after ourselves
    stats_handle_free(h);
    recipe_free(recipe);
  }
  
  jbyteArray result=(*env)->NewByteArray(env, succinct_len);
  (*env)->SetByteArrayRegion(env, result, 0, succinct_len, succinct);
  
  return result;  
}

