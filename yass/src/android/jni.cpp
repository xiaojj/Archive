// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023-2024 Chilledheart  */

#ifdef __ANDROID__

#include "android/jni.hpp"

#include "cli/cli_worker.hpp"
#include "config/config.hpp"
#include "crypto/crypter_export.hpp"

#include <pthread.h>
#include <signal.h>
#include <string>
#include <vector>

JavaVM* g_jvm = nullptr;
jobject g_activity_obj = nullptr;

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
  g_jvm = vm;

  // setup signal handler
  signal(SIGPIPE, SIG_IGN);

  /* Block SIGPIPE in all threads, this can happen if a thread calls write on
     a closed pipe. */
  sigset_t sigpipe_mask;
  sigemptyset(&sigpipe_mask);
  sigaddset(&sigpipe_mask, SIGPIPE);
  sigset_t saved_mask;
  if (pthread_sigmask(SIG_BLOCK, &sigpipe_mask, &saved_mask) == -1) {
    perror("pthread_sigmask failed");
    return -1;
  }

  return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* vm, void* reserved) {
  g_jvm = nullptr;
}

JNIEXPORT jobject JNICALL Java_it_gui_yass_YassUtils_getServerHost(JNIEnv* env, jobject obj) {
  return env->NewStringUTF(absl::GetFlag(FLAGS_server_host).c_str());
}

JNIEXPORT jobject JNICALL Java_it_gui_yass_YassUtils_getServerSNI(JNIEnv* env, jobject obj) {
  return env->NewStringUTF(absl::GetFlag(FLAGS_server_sni).c_str());
}

JNIEXPORT jint JNICALL Java_it_gui_yass_YassUtils_getServerPort(JNIEnv* env, jobject obj) {
  return absl::GetFlag(FLAGS_server_port);
}

JNIEXPORT jobject JNICALL Java_it_gui_yass_YassUtils_getUsername(JNIEnv* env, jobject obj) {
  return env->NewStringUTF(absl::GetFlag(FLAGS_username).c_str());
}

JNIEXPORT jobject JNICALL Java_it_gui_yass_YassUtils_getPassword(JNIEnv* env, jobject obj) {
  return env->NewStringUTF(absl::GetFlag(FLAGS_password).c_str());
}

JNIEXPORT jint JNICALL Java_it_gui_yass_YassUtils_getCipher(JNIEnv* env, jobject obj) {
  const auto method = absl::GetFlag(FLAGS_method).method;
  unsigned int i;
  for (unsigned int i = 0; i < std::size(kCipherMethods); ++i) {
    if (kCipherMethods[i] == method) {
      return i;
    }
  }
  // not found
  return 0;
}

JNIEXPORT jobjectArray JNICALL Java_it_gui_yass_YassUtils_getCipherStrings(JNIEnv* env, jobject obj) {
  jobjectArray jarray =
      env->NewObjectArray(std::size(kCipherMethodCStrs), env->FindClass("java/lang/String"), env->NewStringUTF(""));
  for (unsigned int i = 0; i < std::size(kCipherMethodCStrs); ++i) {
    env->SetObjectArrayElement(jarray, i, env->NewStringUTF(kCipherMethodCStrs[i]));
  }
  return jarray;
}

JNIEXPORT jobject JNICALL Java_it_gui_yass_YassUtils_getDoHUrl(JNIEnv* env, jobject obj) {
  return env->NewStringUTF(absl::GetFlag(FLAGS_doh_url).c_str());
}

JNIEXPORT jobject JNICALL Java_it_gui_yass_YassUtils_getDoTHost(JNIEnv* env, jobject obj) {
  return env->NewStringUTF(absl::GetFlag(FLAGS_dot_host).c_str());
}

JNIEXPORT jobject JNICALL Java_it_gui_yass_YassUtils_getLimitRate(JNIEnv* env, jobject obj) {
  return env->NewStringUTF(std::string(absl::GetFlag(FLAGS_limit_rate)).c_str());
}

JNIEXPORT jint JNICALL Java_it_gui_yass_YassUtils_getTimeout(JNIEnv* env, jobject obj) {
  return absl::GetFlag(FLAGS_connect_timeout);
}

JNIEXPORT jobject JNICALL Java_it_gui_yass_YassUtils_saveConfig(JNIEnv* env,
                                                                jobject obj,
                                                                jobject _server_host,
                                                                jobject _server_sni,
                                                                jobject _server_port,
                                                                jobject _username,
                                                                jobject _password,
                                                                jint _method_idx,
                                                                jobject _doh_url,
                                                                jobject _dot_host,
                                                                jobject _limit_rate,
                                                                jobject _timeout) {
  const char* server_host_str = env->GetStringUTFChars((jstring)_server_host, nullptr);
  std::string server_host = server_host_str != nullptr ? server_host_str : std::string();
  env->ReleaseStringUTFChars((jstring)_server_host, server_host_str);

  const char* server_sni_str = env->GetStringUTFChars((jstring)_server_sni, nullptr);
  std::string server_sni = server_sni_str != nullptr ? server_sni_str : std::string();
  env->ReleaseStringUTFChars((jstring)_server_sni, server_sni_str);

  const char* server_port_str = env->GetStringUTFChars((jstring)_server_port, nullptr);
  std::string server_port = server_port_str != nullptr ? server_port_str : std::string();
  env->ReleaseStringUTFChars((jstring)_server_port, server_port_str);

  const char* username_str = env->GetStringUTFChars((jstring)_username, nullptr);
  std::string username = username_str != nullptr ? username_str : std::string();
  env->ReleaseStringUTFChars((jstring)_username, username_str);

  const char* password_str = env->GetStringUTFChars((jstring)_password, nullptr);
  std::string password = password_str != nullptr ? password_str : std::string();
  env->ReleaseStringUTFChars((jstring)_password, password_str);

  DCHECK_GE(_method_idx, 0);
  DCHECK_LT(static_cast<uint32_t>(_method_idx), std::size(kCipherMethods));
  auto method = kCipherMethods[_method_idx];

  constexpr std::string_view local_host = "0.0.0.0";
  constexpr std::string_view local_port = "0";

  const char* doh_url_str = env->GetStringUTFChars((jstring)_doh_url, nullptr);
  std::string doh_url = doh_url_str != nullptr ? doh_url_str : std::string();
  env->ReleaseStringUTFChars((jstring)_doh_url, doh_url_str);

  const char* dot_host_str = env->GetStringUTFChars((jstring)_dot_host, nullptr);
  std::string dot_host = dot_host_str != nullptr ? dot_host_str : std::string();
  env->ReleaseStringUTFChars((jstring)_dot_host, dot_host_str);

  const char* limit_rate_str = env->GetStringUTFChars((jstring)_limit_rate, nullptr);
  std::string limit_rate = limit_rate_str != nullptr ? limit_rate_str : std::string();
  env->ReleaseStringUTFChars((jstring)_limit_rate, limit_rate_str);

  const char* timeout_str = env->GetStringUTFChars((jstring)_timeout, nullptr);
  std::string timeout = timeout_str != nullptr ? timeout_str : std::string();
  env->ReleaseStringUTFChars((jstring)_timeout, timeout_str);

  std::string err_msg = config::ReadConfigFromArgument(server_host, server_sni, server_port, username, password, method,
                                                       local_host, local_port, doh_url, dot_host, limit_rate, timeout);

  if (err_msg.empty()) {
    return nullptr;
  }
  return env->NewStringUTF(err_msg.c_str());
}

JNIEXPORT void JNICALL Java_it_gui_yass_YassUtils_setEnablePostQuantumKyber(JNIEnv* env,
                                                                            jobject obj,
                                                                            jboolean enable_post_quantum_kyber) {
  absl::SetFlag(&FLAGS_enable_post_quantum_kyber, enable_post_quantum_kyber);
}

#endif  // __ANDROID__
