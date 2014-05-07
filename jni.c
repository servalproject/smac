#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <stdlib.h>
#include <jni.h>

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
  }
  
  // jbyteArray result=(*env)->NewByteArray(env, succinct_len);
  // (*env)->SetByteArrayRegion(env, result, 0, succinct_len, succinct);

  jbyteArray result=(*env)->NewByteArray(env, stripped_len);

  (*env)->SetByteArrayRegion(env, result, 0, stripped_len, stripped);

  
  return result;  
}

