#include "ajni_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <math.h>

#if defined(__has_include)
# if __has_include(<jni.h>)
#  define AJNI_HAS_JNI 1
# endif
#endif
#ifndef AJNI_HAS_JNI
# define AJNI_HAS_JNI 0
#endif

static _Thread_local int rt_status;
static _Thread_local char rt_message[512];
static int fail(int status, const char *message) {
  rt_status = status;
  snprintf(rt_message, sizeof(rt_message), "%s", message);
  return status;
}
static moonbit_string_t ascii_string(const char *text) {
  size_t n = strlen(text);
  moonbit_string_t out = moonbit_make_string_raw((int32_t)n);
  for (size_t i = 0; i < n; ++i) out[i] = (unsigned char)text[i];
  return out;
}
MOONBIT_FFI_EXPORT int ajni_rt_last_status(void) { return rt_status; }
MOONBIT_FFI_EXPORT moonbit_string_t ajni_rt_error_message(void) { return ascii_string(rt_message); }

#if AJNI_HAS_JNI
#include <jni.h>
#include <pthread.h>

typedef struct Resource Resource;
typedef struct Callback Callback;
typedef struct Type { int tag; char *descriptor; jclass klass; } Type;
typedef struct Signature { int count; Type *parameters; Type result; } Signature;
struct Resource {
  Resource *next;
  unsigned refs;
  uint64_t generation;
  int kind, closed, class_flag, array_tag;
  jobject global;
  uint64_t token;
  Resource *declaring;
  int member_kind;
  jmethodID method;
  jfieldID field;
  Signature signature;
};
struct Callback {
  Callback *next;
  uint64_t token, generation;
  int closed, active;
  jweak companion;
  void *closure;
  AjniTrampoline trampoline;
  Signature signature;
};
struct AjniSession { uint64_t generation; };
struct AjniObject { Resource *resource; int null_closed; };
struct AjniMember { Resource *resource; };
struct AjniValue {
  int tag, error;
  jvalue value;
  Resource *object;
  char *message;
};
struct AjniArgs { int length, capacity; AjniValue **values; };
static pthread_mutex_t registry_lock = PTHREAD_MUTEX_INITIALIZER;
static struct {
  JavaVM *vm;
  pthread_t owner;
  int initialized, live, active, leases, closing;
  uint64_t generation, next_token;
  jobject loader;
  Resource *resources;
  Callback *callbacks;
} runtime;
static _Thread_local Resource *rt_exception;
static _Thread_local jthrowable raw_exception;
static _Thread_local JavaVM *raw_exception_vm;

static JNIEnv *owner_env(void) {
  JNIEnv *env = NULL;
  if (!runtime.live || !runtime.vm || !pthread_equal(runtime.owner, pthread_self())) return NULL;
  if ((*runtime.vm)->GetEnv(runtime.vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) return NULL;
  return env;
}
static void resource_release(Resource *r);
static void signature_free(Signature *s, JNIEnv *env) {
  for (int i = 0; i < s->count; ++i) {
    if (env && s->parameters[i].klass) (*env)->DeleteGlobalRef(env, s->parameters[i].klass);
    free(s->parameters[i].descriptor);
  }
  if (env && s->result.klass) (*env)->DeleteGlobalRef(env, s->result.klass);
  free(s->result.descriptor);
  free(s->parameters);
  memset(s, 0, sizeof(*s));
}
static void resource_retain(Resource *r) { if (r) ++r->refs; }
static void resource_close(Resource *r, JNIEnv *env) {
  if (!r || r->closed) return;
  r->closed = 1;
  if (env && r->global) (*env)->DeleteGlobalRef(env, r->global);
  r->global = NULL;
  if (r->kind == 1) signature_free(&r->signature, env);
}
static void resource_release(Resource *r) {
  if (!r || --r->refs) return;
  Resource **slot = &runtime.resources;
  while (*slot && *slot != r) slot = &(*slot)->next;
  if (*slot) *slot = r->next;
  resource_close(r, owner_env());
  Resource *declaring = r->declaring;
  r->declaring = NULL;
  resource_release(declaring);
  free(r);
}
static void clear_error(void) {
  Resource *old = rt_exception;
  rt_exception = NULL;
  resource_release(old);
  JNIEnv *env = owner_env();
  if (raw_exception && env && raw_exception_vm == runtime.vm) {
    (*env)->DeleteGlobalRef(env, raw_exception);
    raw_exception = NULL; raw_exception_vm = NULL;
  }
  rt_status = 0;
  rt_message[0] = 0;
}
static Resource *resource_new(int kind) {
  Resource *r = calloc(1, sizeof(*r));
  if (!r) { fail(AJNI_RT_NATIVE_FAILURE, "Native resource allocation failed"); return NULL; }
  r->refs = 1;
  r->kind = kind;
  r->generation = runtime.generation;
  r->next = runtime.resources;
  runtime.resources = r;
  return r;
}
static void session_finalizer(void *p) { (void)p; }
static void object_finalizer(void *p) { resource_release(((AjniObject *)p)->resource); }
static void member_finalizer(void *p) { resource_release(((AjniMember *)p)->resource); }
static void value_finalizer(void *p) {
  AjniValue *v = p;
  resource_release(v->object);
  free(v->message);
}
static void args_finalizer(void *p) {
  AjniArgs *a = p;
  for (int i = 0; i < a->length; ++i) moonbit_decref(a->values[i]);
  free(a->values);
}
static AjniSession *session_wrap(uint64_t generation) {
  AjniSession *s = moonbit_make_external_object(session_finalizer, sizeof(*s));
  s->generation = generation;
  return s;
}
static AjniObject *object_wrap(Resource *r, int retain) {
  AjniObject *o = moonbit_make_external_object(object_finalizer, sizeof(*o));
  o->resource = r;
  o->null_closed = 0;
  if (retain) resource_retain(r);
  return o;
}
static AjniMember *member_wrap(Resource *r) {
  AjniMember *m = moonbit_make_external_object(member_finalizer, sizeof(*m));
  m->resource = r;
  return m;
}
static AjniValue *value_new(int tag) {
  AjniValue *v = moonbit_make_external_object(value_finalizer, sizeof(*v));
  memset(v, 0, sizeof(*v));
  v->tag = tag;
  return v;
}
static AjniArgs *args_new(void) {
  AjniArgs *a = moonbit_make_external_object(args_finalizer, sizeof(*a));
  memset(a, 0, sizeof(*a));
  return a;
}
static int args_append(AjniArgs *a, AjniValue *v) {
  if (a->length == INT_MAX) return fail(AJNI_RT_INVALID_ARGUMENT, "Too many arguments");
  if (a->length == a->capacity) {
    int capacity = a->capacity ? (a->capacity > INT_MAX / 2 ? INT_MAX : a->capacity * 2) : 8;
    AjniValue **values = realloc(a->values, (size_t)capacity * sizeof(*values));
    if (!values) return fail(AJNI_RT_NATIVE_FAILURE, "Argument allocation failed");
    a->values = values;
    a->capacity = capacity;
  }
  moonbit_incref(v);
  a->values[a->length++] = v;
  return 0;
}
static int capture_exception(JNIEnv *env, const char *operation) {
  if (!(*env)->ExceptionCheck(env)) {
    /* A completed JNI call owns its result; Java may have handled reentrant errors. */
    clear_error();
    return 0;
  }
  jthrowable original = (*env)->ExceptionOccurred(env);
  (*env)->ExceptionClear(env);
  jobject global = original ? (*env)->NewGlobalRef(env, original) : NULL;
  int secondary = (*env)->ExceptionCheck(env);
  if (secondary) (*env)->ExceptionClear(env);
  if (!global) {
    if (original) (*env)->Throw(env, original);
    if (original) (*env)->DeleteLocalRef(env, original);
    fail(AJNI_RT_NATIVE_FAILURE, "Cannot preserve Java exception");
    return 1;
  }
  if (original) (*env)->DeleteLocalRef(env, original);
  Resource *r = resource_new(0);
  if (!r) {
    (*env)->Throw(env, (jthrowable)global);
    (*env)->DeleteGlobalRef(env, global);
    return 1;
  }
  r->global = global;
  resource_release(rt_exception);
  rt_exception = r;
  fail(AJNI_RT_JAVA_THROWN, operation);
  return 1;
}
static Resource *global_resource(JNIEnv *env, jobject object, int class_flag) {
  if (!object) return NULL;
  jobject global = (*env)->NewGlobalRef(env, object);
  int thrown = capture_exception(env, "NewGlobalRef");
  if (thrown || !global) {
    if (global) (*env)->DeleteGlobalRef(env, global);
    if (!thrown) fail(AJNI_RT_NATIVE_FAILURE, "NewGlobalRef returned null");
    return NULL;
  }
  Resource *r = resource_new(0);
  if (!r) { (*env)->DeleteGlobalRef(env, global); return NULL; }
  r->global = global;
  r->class_flag = class_flag;
  return r;
}
static void drain_callbacks(void) {
  JNIEnv *env = owner_env();
  if (!env) return;
  for (;;) {
    void *closure = NULL;
    Callback *retired = NULL;
    pthread_mutex_lock(&registry_lock);
    Callback **slot = &runtime.callbacks;
    while (*slot) {
      Callback *c = *slot;
      if (!c->closed && c->companion && (*env)->IsSameObject(env, c->companion, NULL)) c->closed = 1;
      if (c->closed && c->closure) { closure = c->closure; c->closure = NULL; }
      if (c->closed && !c->active) { *slot = c->next; retired = c; }
      if (closure || retired) break;
      slot = &c->next;
    }
    pthread_mutex_unlock(&registry_lock);
    if (closure) moonbit_decref(closure);
    if (retired) {
      if (retired->companion) (*env)->DeleteWeakGlobalRef(env, retired->companion);
      signature_free(&retired->signature, env);
      free(retired);
    }
    if (!closure && !retired) return;
  }
}
static int gate(JNIEnv **out) {
  pthread_mutex_lock(&registry_lock);
  int initialized = runtime.initialized, live = runtime.live;
  int owner = initialized && pthread_equal(runtime.owner, pthread_self());
  pthread_mutex_unlock(&registry_lock);
  if (!initialized) return fail(AJNI_RT_NOT_INITIALIZED, "JNI session is not initialized");
  if (!owner) return fail(AJNI_RT_WRONG_THREAD, "JNI session belongs to another OS thread");
  if (!live) return fail(AJNI_RT_CLOSED, "JNI session is closed");
  JNIEnv *env = owner_env();
  if (!env) return fail(AJNI_RT_NATIVE_FAILURE, "Owner thread is not attached to the VM");
  if ((*env)->ExceptionCheck(env)) return fail(AJNI_RT_JAVA_THROWN, "Entry has a pending Java exception");
  drain_callbacks();
  clear_error();
  *out = env;
  return 0;
}
static int begin(JNIEnv **out) {
  if (gate(out)) return rt_status;
  int result = (**out)->PushLocalFrame(*out, 32);
  int thrown = capture_exception(*out, "PushLocalFrame");
  if (thrown || result < 0) {
    if (!thrown) fail(AJNI_RT_NATIVE_FAILURE, "PushLocalFrame failed");
    return rt_status;
  }
  ++runtime.active;
  return 0;
}
static void end(JNIEnv *env) { (*env)->PopLocalFrame(env, NULL); --runtime.active; }
static int check_session(AjniSession *s) {
  if (!s || !s->generation || s->generation != runtime.generation)
    return fail(AJNI_RT_CLOSED, "Session is closed or belongs to an old generation");
  return 0;
}
static int check_resource(Resource *r) {
  if (!r || r->closed || r->generation != runtime.generation)
    return fail(AJNI_RT_CLOSED, "JNI handle is closed");
  return 0;
}
static int check_object(AjniObject *o, int nullable) {
  if (!o || o->null_closed) return fail(AJNI_RT_CLOSED, "Object handle is closed");
  if (!o->resource) return nullable ? 0 : fail(AJNI_RT_INVALID_ARGUMENT, "Java null is not allowed here");
  return check_resource(o->resource);
}
static char *ascii_input(moonbit_string_t text) {
  int n = Moonbit_array_length(text);
  if ((size_t)n > (SIZE_MAX - 1) / 3) { fail(AJNI_RT_NATIVE_FAILURE, "JNI text length overflow"); return NULL; }
  char *out = malloc((size_t)n * 3 + 1);
  if (!out) { fail(AJNI_RT_NATIVE_FAILURE, "Text allocation failed"); return NULL; }
  size_t cursor = 0;
  for (int i = 0; i < n; ++i) {
    uint16_t c = text[i];
    if (!c) { free(out); fail(AJNI_RT_INVALID_ARGUMENT, "JNI names cannot contain NUL"); return NULL; }
    if (c < 0x80) out[cursor++] = (char)c;
    else if (c < 0x800) { out[cursor++] = (char)(0xC0 | (c >> 6)); out[cursor++] = (char)(0x80 | (c & 63)); }
    else { out[cursor++] = (char)(0xE0 | (c >> 12)); out[cursor++] = (char)(0x80 | ((c >> 6) & 63)); out[cursor++] = (char)(0x80 | (c & 63)); }
  }
  out[cursor] = 0;
  return out;
}
static jclass resolve_class(JNIEnv *env, const char *name) {
  if (!runtime.loader) {
    jclass klass = (*env)->FindClass(env, name);
    int thrown = capture_exception(env, "FindClass");
    if (!thrown && !klass) fail(AJNI_RT_NATIVE_FAILURE, "FindClass returned null");
    return thrown ? NULL : klass;
  }
  jclass loader_class = NULL, class_class = NULL, klass = NULL;
  jstring binary = NULL;
  jmethodID method = NULL;
  loader_class = (*env)->GetObjectClass(env, runtime.loader);
  if (capture_exception(env, "GetObjectClass(ClassLoader)")) goto done;
  if (!loader_class) { fail(AJNI_RT_NATIVE_FAILURE, "ClassLoader class is null"); goto done; }
  if (name[0] == '[') {
    class_class = (*env)->FindClass(env, "java/lang/Class");
    if (capture_exception(env, "FindClass(Class)")) goto done;
    if (!class_class) { fail(AJNI_RT_NATIVE_FAILURE, "Class class is null"); goto done; }
    method = (*env)->GetStaticMethodID(env, class_class, "forName", "(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;");
    if (capture_exception(env, "GetStaticMethodID(Class.forName)")) goto done;
  } else {
    method = (*env)->GetMethodID(env, loader_class, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    if (capture_exception(env, "GetMethodID(ClassLoader.loadClass)")) goto done;
  }
  if (!method) { fail(AJNI_RT_NATIVE_FAILURE, "Class lookup method is null"); goto done; }
  size_t n = strlen(name);
  if (n > INT_MAX || n > SIZE_MAX / sizeof(jchar)) { fail(AJNI_RT_INVALID_ARGUMENT, "Class name is too long"); goto done; }
  jchar *characters = malloc((n ? n : 1) * sizeof(*characters));
  if (!characters) { fail(AJNI_RT_NATIVE_FAILURE, "Class name allocation failed"); goto done; }
  size_t units = 0;
  for (size_t i = 0; i < n;) {
    unsigned char c = (unsigned char)name[i++];
    uint16_t unit = c;
    if ((c & 0xE0) == 0xC0) { unit = (uint16_t)(((c & 31) << 6) | ((unsigned char)name[i] & 63)); ++i; }
    else if ((c & 0xF0) == 0xE0) { unit = (uint16_t)(((c & 15) << 12) | (((unsigned char)name[i] & 63) << 6) | ((unsigned char)name[i+1] & 63)); i += 2; }
    characters[units++] = unit == '/' ? '.' : unit;
  }
  binary = (*env)->NewString(env, characters, (jsize)units);
  free(characters);
  if (capture_exception(env, "NewString(class name)")) goto done;
  if (!binary) { fail(AJNI_RT_NATIVE_FAILURE, "Class name string is null"); goto done; }
  jvalue args[3]; memset(args, 0, sizeof(args)); args[0].l = binary; args[2].l = runtime.loader;
  klass = name[0] == '[' ? (jclass)(*env)->CallStaticObjectMethodA(env, class_class, method, args)
    : (jclass)(*env)->CallObjectMethodA(env, runtime.loader, method, args);
  if (capture_exception(env, "ClassLoader class lookup")) klass = NULL;
  else if (!klass) fail(AJNI_RT_NATIVE_FAILURE, "Class lookup returned null");
done:
  if (binary) (*env)->DeleteLocalRef(env, binary);
  if (loader_class) (*env)->DeleteLocalRef(env, loader_class);
  if (class_class) (*env)->DeleteLocalRef(env, class_class);
  return klass;
}
static int parse_type(const char **cursor, Type *type, int allow_void) {
  const char *start = *cursor, *p = start;
  memset(type, 0, sizeof(*type));
  switch (*p++) {
    case 'V': if (!allow_void) goto invalid; type->tag = 0; break;
    case 'Z': type->tag = 1; break; case 'B': type->tag = 2; break;
    case 'C': type->tag = 3; break; case 'S': type->tag = 4; break;
    case 'I': type->tag = 5; break; case 'J': type->tag = 6; break;
    case 'F': type->tag = 7; break; case 'D': type->tag = 8; break;
    case 'L': {
      const char *name = p;
      while (*p && *p != ';') { if (*p == '.' || *p == '[' || *p == '(' || *p == ')') goto invalid; ++p; }
      if (p == name || *p != ';') goto invalid;
      ++p; type->tag = 9; break;
    }
    case '[': {
      int depth = 1; while (*p == '[') { ++p; if (++depth > 255) goto invalid; }
      Type component;
      if (parse_type(&p, &component, 0)) return rt_status;
      free(component.descriptor);
      type->tag = 9; break;
    }
    default: goto invalid;
  }
  if (type->tag == 9) {
    size_t n = (size_t)(p - start);
    type->descriptor = malloc(n + 1);
    if (!type->descriptor) return fail(AJNI_RT_NATIVE_FAILURE, "Descriptor allocation failed");
    memcpy(type->descriptor, start, n); type->descriptor[n] = 0;
  }
  *cursor = p;
  return 0;
invalid:
  return fail(AJNI_RT_INVALID_ARGUMENT, "Invalid JNI descriptor");
}
static int parse_signature(const char *descriptor, Signature *s, int method, int is_static) {
  memset(s, 0, sizeof(*s));
  const char *p = descriptor;
  Type parameters[255];
  int count = 0;
  if (method) {
    if (*p != '(') return fail(AJNI_RT_INVALID_ARGUMENT, "Expected method descriptor");
    ++p;
    int slots = is_static ? 0 : 1;
    while (*p && *p != ')') {
      if (count == 255) goto invalid;
      Type type;
      if (parse_type(&p, &type, 0)) goto error;
      slots += type.tag == 6 || type.tag == 8 ? 2 : 1;
      if (slots > 255) { free(type.descriptor); goto invalid; }
      parameters[count++] = type;
    }
    if (*p != ')') goto invalid;
    ++p;
  }
  if (parse_type(&p, &s->result, method)) goto error;
  if (*p) goto invalid;
  if (count) {
    s->parameters = malloc((size_t)count * sizeof(*s->parameters));
    if (!s->parameters) { fail(AJNI_RT_NATIVE_FAILURE, "Descriptor allocation failed"); goto error; }
    memcpy(s->parameters, parameters, (size_t)count * sizeof(*parameters));
  }
  s->count = count;
  return 0;
invalid:
  fail(AJNI_RT_INVALID_ARGUMENT, "Invalid JNI descriptor or argument slot limit");
error:
  for (int i = 0; i < count; ++i) free(parameters[i].descriptor);
  signature_free(s, NULL);
  return rt_status;
}
static jclass descriptor_class(JNIEnv *env, const char *name, jobject loader) {
  size_t length = strlen(name);
  char *binary_name = malloc(length + 1);
  if (!binary_name) { fail(AJNI_RT_NATIVE_FAILURE, "Class name allocation failed"); return NULL; }
  for (size_t i = 0; i <= length; ++i) binary_name[i] = name[i] == '/' ? '.' : name[i];
  jstring binary = (*env)->NewStringUTF(env, binary_name);
  free(binary_name);
  if (capture_exception(env, "NewStringUTF(descriptor class)")) return NULL;
  if (!binary) { fail(AJNI_RT_NATIVE_FAILURE, "Descriptor class name is null"); return NULL; }
  jclass class_class = (*env)->FindClass(env, "java/lang/Class");
  if (capture_exception(env, "FindClass(Class)")) return NULL;
  if (!class_class) { fail(AJNI_RT_NATIVE_FAILURE, "Class class is null"); return NULL; }
  jmethodID method = (*env)->GetStaticMethodID(env, class_class, "forName", "(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;");
  if (capture_exception(env, "GetStaticMethodID(Class.forName)")) return NULL;
  if (!method) { fail(AJNI_RT_NATIVE_FAILURE, "Class.forName method is null"); return NULL; }
  jvalue arguments[3]; memset(arguments, 0, sizeof(arguments));
  arguments[0].l = binary; arguments[2].l = loader;
  jclass klass = (jclass)(*env)->CallStaticObjectMethodA(env, class_class, method, arguments);
  int thrown = capture_exception(env, "Class.forName(descriptor)");
  (*env)->DeleteLocalRef(env, binary);
  (*env)->DeleteLocalRef(env, class_class);
  if (!thrown && !klass) fail(AJNI_RT_NATIVE_FAILURE, "Descriptor class lookup returned null");
  return thrown ? NULL : klass;
}
static int resolve_type(JNIEnv *env, Type *t, jobject loader, int defining_loader) {
  if (t->tag != 9) return 0;
  const char *name = t->descriptor;
  char *copy = NULL;
  if (name[0] == 'L') {
    size_t n = strlen(name);
    copy = malloc(n - 1);
    if (!copy) return fail(AJNI_RT_NATIVE_FAILURE, "Class name allocation failed");
    memcpy(copy, name + 1, n - 2); copy[n - 2] = 0; name = copy;
  }
  jclass local = defining_loader ? descriptor_class(env, name, loader) : resolve_class(env, name);
  free(copy);
  if (!local) return rt_status;
  t->klass = (*env)->NewGlobalRef(env, local);
  int thrown = capture_exception(env, "NewGlobalRef(descriptor class)");
  (*env)->DeleteLocalRef(env, local);
  if (!thrown && !t->klass) fail(AJNI_RT_NATIVE_FAILURE, "Descriptor class global reference is null");
  return rt_status;
}
static int resolve_signature(JNIEnv *env, Signature *s, jclass declaring) {
  jobject loader = NULL;
  if (declaring) {
    jclass class_class = (*env)->FindClass(env, "java/lang/Class");
    if (capture_exception(env, "FindClass(Class)")) return rt_status;
    if (!class_class) return fail(AJNI_RT_NATIVE_FAILURE, "Class class is null");
    jmethodID method = (*env)->GetMethodID(env, class_class, "getClassLoader", "()Ljava/lang/ClassLoader;");
    if (capture_exception(env, "GetMethodID(Class.getClassLoader)")) return rt_status;
    if (!method) return fail(AJNI_RT_NATIVE_FAILURE, "Class.getClassLoader method is null");
    loader = (*env)->CallObjectMethodA(env, declaring, method, NULL);
    int thrown = capture_exception(env, "Class.getClassLoader");
    (*env)->DeleteLocalRef(env, class_class);
    if (thrown) return rt_status;
  }
  int status = 0;
  for (int i = 0; i < s->count; ++i) if ((status = resolve_type(env, &s->parameters[i], loader, declaring != NULL))) break;
  if (!status) status = resolve_type(env, &s->result, loader, declaring != NULL);
  if (loader) (*env)->DeleteLocalRef(env, loader);
  return status;
}
static int value_matches(JNIEnv *env, AjniValue *v, Type *type) {
  if (!v || v->error || v->tag != type->tag) return fail(AJNI_RT_INVALID_ARGUMENT, "Value does not match JNI descriptor");
  if (v->tag == 9 && v->object) {
    if (check_resource(v->object)) return rt_status;
    jboolean valid = (*env)->IsInstanceOf(env, v->object->global, type->klass);
    if (capture_exception(env, "IsInstanceOf(argument)")) return rt_status;
    if (!valid) return fail(AJNI_RT_INVALID_ARGUMENT, "Object is not assignable to descriptor class");
  }
  return 0;
}
static int receiver_check(JNIEnv *env, Resource *m, AjniObject *receiver) {
  int is_static = m->member_kind == 1 || m->member_kind == 4 || m->member_kind == 2;
  if (check_object(receiver, is_static)) return rt_status;
  if (is_static) return receiver->resource ? fail(AJNI_RT_INVALID_ARGUMENT, "Static member must not have a receiver") : 0;
  jboolean valid = (*env)->IsInstanceOf(env, receiver->resource->global, (jclass)m->declaring->global);
  if (capture_exception(env, "IsInstanceOf(receiver)")) return rt_status;
  return valid ? 0 : fail(AJNI_RT_INVALID_ARGUMENT, "Receiver is not an instance of declaring class");
}
static jobject resource_local(JNIEnv *env, Resource *resource) {
  if (!resource) return NULL;
  if (check_resource(resource)) return NULL;
  jobject local = (*env)->NewLocalRef(env, resource->global);
  int thrown = capture_exception(env, "NewLocalRef(operation snapshot)");
  if (!thrown && !local) fail(AJNI_RT_NATIVE_FAILURE, "Operation reference snapshot is null");
  return thrown ? NULL : local;
}
static AjniValue *result_value(JNIEnv *env, int tag, jvalue result) {
  AjniValue *value = value_new(tag);
  value->value = result;
  if (tag == 9 && result.l) value->object = global_resource(env, result.l, 0);
  return value;
}

int ajni_rt_host_open(void *raw_env, void *class_loader) {
  JNIEnv *env = raw_env;
  if (!env) return fail(AJNI_RT_INVALID_ARGUMENT, "JNIEnv is null");
  if ((*env)->ExceptionCheck(env)) return fail(AJNI_RT_JAVA_THROWN, "Host open has a pending exception");
  pthread_mutex_lock(&registry_lock);
  int was_initialized = runtime.initialized;
  int wrong = was_initialized && !pthread_equal(runtime.owner, pthread_self());
  int live = runtime.live;
  pthread_mutex_unlock(&registry_lock);
  if (wrong) return fail(AJNI_RT_WRONG_THREAD, "MoonBit owner thread cannot change");
  if (live) {
    if (owner_env() != env) return fail(AJNI_RT_INVALID_ARGUMENT, "JNIEnv does not belong to the established owner");
    clear_error(); return 0;
  }
  JavaVM *vm = NULL;
  if ((*env)->GetJavaVM(env, &vm) != JNI_OK || !vm) return fail(AJNI_RT_NATIVE_FAILURE, "GetJavaVM failed");
  if (was_initialized && runtime.vm != vm) return fail(AJNI_RT_INVALID_ARGUMENT, "A different JVM cannot reuse this runtime");
  if (runtime.generation == UINT64_MAX) return fail(AJNI_RT_NATIVE_FAILURE, "Session generation exhausted");
  jobject loader = NULL;
  if (class_loader) {
    jclass klass = (*env)->FindClass(env, "java/lang/ClassLoader");
    if ((*env)->ExceptionCheck(env)) return fail(AJNI_RT_JAVA_THROWN, "FindClass(ClassLoader) failed");
    if (!klass) return fail(AJNI_RT_NATIVE_FAILURE, "ClassLoader class is null");
    jboolean valid = (*env)->IsInstanceOf(env, class_loader, klass);
    (*env)->DeleteLocalRef(env, klass);
    if ((*env)->ExceptionCheck(env)) return fail(AJNI_RT_JAVA_THROWN, "ClassLoader validation failed");
    if (!valid) return fail(AJNI_RT_INVALID_ARGUMENT, "Host loader is not a ClassLoader");
    loader = (*env)->NewGlobalRef(env, class_loader);
    if ((*env)->ExceptionCheck(env)) return fail(AJNI_RT_JAVA_THROWN, "ClassLoader global reference failed");
    if (!loader) return fail(AJNI_RT_NATIVE_FAILURE, "ClassLoader global reference is null");
  }
  pthread_mutex_lock(&registry_lock);
  runtime.vm = vm; runtime.owner = pthread_self(); runtime.initialized = 1;
  runtime.live = 1; ++runtime.generation; runtime.loader = loader;
  pthread_mutex_unlock(&registry_lock);
  clear_error();
  return 0;
}
int ajni_rt_host_enter(void *raw_env) {
  JNIEnv *env;
  if (gate(&env)) return rt_status;
  if (env != (JNIEnv *)raw_env) return fail(AJNI_RT_INVALID_ARGUMENT, "JNIEnv does not belong to the owner thread");
  ++runtime.active;
  return 0;
}
void ajni_rt_host_leave(void) {
  if (runtime.initialized && pthread_equal(runtime.owner, pthread_self()) && runtime.active > 0) --runtime.active;
}
int ajni_rt_host_close(void *raw_env) {
  JNIEnv *env;
  if (gate(&env)) {
    if (rt_status == AJNI_RT_CLOSED && runtime.initialized && pthread_equal(runtime.owner, pthread_self())) { rt_status = 0; rt_message[0] = 0; return 0; }
    return rt_status;
  }
  if (env != (JNIEnv *)raw_env) return fail(AJNI_RT_INVALID_ARGUMENT, "JNIEnv does not belong to owner");
  pthread_mutex_lock(&registry_lock);
  if (runtime.active || runtime.leases) {
    pthread_mutex_unlock(&registry_lock);
    return fail(AJNI_RT_BUSY, "Cannot close a session during an active call or native lease");
  }
  runtime.closing = 1;
  for (Callback *c = runtime.callbacks; c; c = c->next) c->closed = 1;
  pthread_mutex_unlock(&registry_lock);
  drain_callbacks();
  resource_release(rt_exception); rt_exception = NULL;
  for (Resource *r = runtime.resources; r;) {
    Resource *next = r->next;
    resource_retain(r);
    resource_close(r, env);
    resource_release(r);
    r = next;
  }
  clear_error();
  if (runtime.loader) (*env)->DeleteGlobalRef(env, runtime.loader);
  runtime.loader = NULL;
  pthread_mutex_lock(&registry_lock); runtime.live = 0; runtime.closing = 0; pthread_mutex_unlock(&registry_lock);
  rt_status = 0; rt_message[0] = 0;
  return 0;
}
static void throw_text(JNIEnv *env, int status, const char *message) {
  if ((*env)->ExceptionCheck(env)) return;
  const char *name = status == AJNI_RT_INVALID_ARGUMENT ? "java/lang/IllegalArgumentException"
    : status == AJNI_RT_NATIVE_FAILURE ? "dev/nanaloveyuki/ajni/AjniException" : "java/lang/IllegalStateException";
  jclass klass = (*env)->FindClass(env, name);
  if ((*env)->ExceptionCheck(env)) return;
  if (!klass) return;
  (*env)->ThrowNew(env, klass, message);
  (*env)->DeleteLocalRef(env, klass);
}
static int validate_throwable(JNIEnv *env, Resource *resource) {
  if (!resource) return fail(AJNI_RT_INVALID_ARGUMENT, "JavaThrown requires a nonnull Throwable");
  if (check_resource(resource)) return rt_status;
  jclass klass = (*env)->FindClass(env, "java/lang/Throwable");
  if (capture_exception(env, "FindClass(Throwable)")) return rt_status;
  if (!klass) return fail(AJNI_RT_NATIVE_FAILURE, "Throwable class is null");
  jboolean valid = (*env)->IsInstanceOf(env, resource->global, klass);
  int thrown = capture_exception(env, "IsInstanceOf(Throwable)");
  (*env)->DeleteLocalRef(env, klass);
  if (thrown) return rt_status;
  return valid ? 0 : fail(AJNI_RT_INVALID_ARGUMENT, "JavaThrown payload is not a Throwable");
}
void ajni_rt_host_throw(void *raw_env) {
  JNIEnv *env = raw_env;
  if (!env || (*env)->ExceptionCheck(env)) return;
  if (rt_status == AJNI_RT_JAVA_THROWN && raw_exception) {
    (*env)->Throw(env, raw_exception);
    (*env)->DeleteGlobalRef(env, raw_exception);
    raw_exception = NULL; raw_exception_vm = NULL;
  } else if (rt_status == AJNI_RT_JAVA_THROWN && runtime.live && pthread_equal(runtime.owner, pthread_self()) && rt_exception && !rt_exception->closed) {
    Resource *throwable = rt_exception; resource_retain(throwable);
    if (!validate_throwable(env, throwable)) (*env)->Throw(env, (jthrowable)throwable->global);
    else if (!(*env)->ExceptionCheck(env)) throw_text(env, rt_status, rt_message);
    resource_release(throwable);
  } else throw_text(env, rt_status, rt_message[0] ? rt_message : "JNI runtime operation failed");
}
MOONBIT_FFI_EXPORT AjniSession *ajni_rt_current(void) {
  JNIEnv *env;
  if (gate(&env)) return session_wrap(0);
  return session_wrap(runtime.generation);
}
MOONBIT_FFI_EXPORT int ajni_rt_session_close(AjniSession *s) {
  if (runtime.initialized && pthread_equal(runtime.owner, pthread_self()) && s && s->generation == runtime.generation && !runtime.live) {
    rt_status = 0; rt_message[0] = 0; return 0;
  }
  JNIEnv *env;
  if (gate(&env) || check_session(s)) return rt_status;
  return ajni_rt_host_close(env);
}
MOONBIT_FFI_EXPORT AjniObject *ajni_rt_take_exception(void) {
  Resource *r = rt_exception; rt_exception = NULL;
  if (!r && raw_exception && runtime.live && pthread_equal(runtime.owner, pthread_self())) {
    r = resource_new(0);
    if (r) { r->global = raw_exception; raw_exception = NULL; raw_exception_vm = NULL; }
  }
  return object_wrap(r, 0);
}
MOONBIT_FFI_EXPORT AjniObject *ajni_rt_null_object(void) { clear_error(); return object_wrap(NULL, 0); }
MOONBIT_FFI_EXPORT int ajni_rt_object_close(AjniObject *o) {
  JNIEnv *env;
  if (gate(&env)) return rt_status;
  if (!o) return fail(AJNI_RT_INVALID_ARGUMENT, "Missing object wrapper");
  if (o->resource) {
    if (o->resource->generation != runtime.generation) return fail(AJNI_RT_CLOSED, "Object belongs to an old session");
    resource_close(o->resource, env);
  } else o->null_closed = 1;
  return 0;
}
MOONBIT_FFI_EXPORT int ajni_rt_object_same(AjniObject *a, AjniObject *b) {
  JNIEnv *env;
  if (begin(&env)) return 0;
  int same = 0;
  if (!check_object(a, 1) && !check_object(b, 1)) {
    same = (*env)->IsSameObject(env, a->resource ? a->resource->global : NULL, b->resource ? b->resource->global : NULL);
    capture_exception(env, "IsSameObject");
  }
  end(env); return same;
}
MOONBIT_FFI_EXPORT int ajni_rt_object_is_null(AjniObject *o) {
  JNIEnv *env;
  if (gate(&env) || check_object(o, 1)) return 0;
  return !o->resource;
}
MOONBIT_FFI_EXPORT AjniSession *ajni_rt_object_session(AjniObject *o) {
  JNIEnv *env;
  if (gate(&env) || check_object(o, 0)) return session_wrap(0);
  return session_wrap(o->resource->generation);
}
MOONBIT_FFI_EXPORT AjniObject *ajni_rt_find_class(AjniSession *s, moonbit_string_t name) {
  JNIEnv *env;
  Resource *r = NULL;
  if (begin(&env)) return object_wrap(NULL, 0);
  if (!check_session(s)) {
    char *text = ascii_input(name);
    if (text) {
      if (!*text || strchr(text, '.') || strchr(text, ';')) fail(AJNI_RT_INVALID_ARGUMENT, "Expected an internal class name");
      else { jclass klass = resolve_class(env, text); if (klass) r = global_resource(env, klass, 1); }
      free(text);
    }
  }
  end(env); return object_wrap(r, 0);
}
MOONBIT_FFI_EXPORT AjniObject *ajni_rt_new_string(AjniSession *s, moonbit_string_t text) {
  JNIEnv *env; Resource *r = NULL;
  if (begin(&env)) return object_wrap(NULL, 0);
  if (!check_session(s)) {
    jstring string = (*env)->NewString(env, (const jchar *)text, Moonbit_array_length(text));
    int thrown = capture_exception(env, "NewString");
    if (!thrown && string) r = global_resource(env, string, 0);
    else if (!thrown) fail(AJNI_RT_NATIVE_FAILURE, "NewString returned null");
  }
  end(env); return object_wrap(r, 0);
}
MOONBIT_FFI_EXPORT moonbit_string_t ajni_rt_read_string(AjniObject *o) {
  JNIEnv *env; moonbit_string_t out = NULL;
  if (begin(&env)) return moonbit_make_string_raw(0);
  if (!check_object(o, 0)) {
    jclass klass = (*env)->FindClass(env, "java/lang/String");
    if (!capture_exception(env, "FindClass(String)") && klass) {
      jboolean valid = (*env)->IsInstanceOf(env, o->resource->global, klass);
      if (!capture_exception(env, "IsInstanceOf(String)")) {
        if (!valid) fail(AJNI_RT_INVALID_ARGUMENT, "Object is not a String");
        else {
          jsize n = (*env)->GetStringLength(env, (jstring)o->resource->global);
          if (!capture_exception(env, "GetStringLength")) {
            out = moonbit_make_string_raw(n);
            (*env)->GetStringRegion(env, (jstring)o->resource->global, 0, n, (jchar *)out);
            capture_exception(env, "GetStringRegion");
          }
        }
      }
    } else if (!rt_status) fail(AJNI_RT_NATIVE_FAILURE, "String class lookup returned null");
  }
  end(env); return out ? out : moonbit_make_string_raw(0);
}
MOONBIT_FFI_EXPORT AjniMember *ajni_rt_lookup(AjniObject *klass, moonbit_string_t name, moonbit_string_t descriptor, int kind) {
  JNIEnv *env; Resource *r = NULL, *declaring = NULL;
  if (begin(&env)) return member_wrap(NULL);
  if (check_object(klass, 0)) goto done;
  if (!klass->resource->class_flag || kind < 0 || kind > 4) { fail(AJNI_RT_INVALID_ARGUMENT, "Expected class and valid member kind"); goto done; }
  declaring = global_resource(env, klass->resource->global, 1);
  if (!declaring) goto done;
  char *n = ascii_input(name), *d = NULL;
  if (!n) goto done;
  d = ascii_input(descriptor);
  if (!d) { free(n); goto done; }
  Signature signature;
  if (!*n || (kind == 2 ? strcmp(n, "<init>") != 0 : n[0] == '<')) fail(AJNI_RT_INVALID_ARGUMENT, "Invalid member name");
  else if (!parse_signature(d, &signature, kind < 3, kind == 1 || kind == 4)) {
    if (kind == 2 && signature.result.tag != 0) fail(AJNI_RT_INVALID_ARGUMENT, "Constructor descriptor must return void");
    else if (!resolve_signature(env, &signature, (jclass)declaring->global)) {
      jmethodID method = NULL; jfieldID field = NULL;
      if (kind == 1) method = (*env)->GetStaticMethodID(env, (jclass)declaring->global, n, d);
      else if (kind < 3) method = (*env)->GetMethodID(env, (jclass)declaring->global, n, d);
      else if (kind == 3) field = (*env)->GetFieldID(env, (jclass)declaring->global, n, d);
      else field = (*env)->GetStaticFieldID(env, (jclass)declaring->global, n, d);
      if (!capture_exception(env, "Member lookup")) {
        if (!method && !field) fail(AJNI_RT_NATIVE_FAILURE, "Member lookup returned null");
        else {
          r = resource_new(1);
          if (r) {
            r->declaring = declaring; declaring = NULL;
          }
          if (r) {
            r->member_kind = kind; r->method = method; r->field = field;
            r->signature = signature; memset(&signature, 0, sizeof(signature));
          }
        }
      }
    }
    signature_free(&signature, env);
  }
  free(n); free(d);
done:
  resource_release(declaring);
  end(env); return member_wrap(r);
}
MOONBIT_FFI_EXPORT AjniValue *ajni_rt_member_call(AjniMember *member, AjniObject *receiver, AjniArgs *args) {
  JNIEnv *env; AjniValue *out = NULL;
  if (begin(&env)) return value_new(0);
  Resource *m = member ? member->resource : NULL;
  if (check_resource(m)) goto done;
  if (m->member_kind > 2 || check_resource(m->declaring)) { if (!rt_status) fail(AJNI_RT_INVALID_ARGUMENT, "Member is not a method"); goto done; }
  if (receiver_check(env, m, receiver)) goto done;
  if (!args || args->length != m->signature.count) { fail(AJNI_RT_INVALID_ARGUMENT, "Argument count does not match descriptor"); goto done; }
  jvalue stack[16];
  jvalue *values = args->length > 16 ? malloc((size_t)args->length * sizeof(*values)) : stack;
  if (!values) { fail(AJNI_RT_NATIVE_FAILURE, "Call argument allocation failed"); goto done; }
  int valid = 1;
  for (int i = 0; i < args->length; ++i) {
    if (value_matches(env, args->values[i], &m->signature.parameters[i])) { valid = 0; break; }
    values[i] = args->values[i]->value;
    if (args->values[i]->tag == 9) {
      values[i].l = resource_local(env, args->values[i]->object);
      if (rt_status) { valid = 0; break; }
    }
  }
  if (valid) {
    jvalue result; memset(&result, 0, sizeof(result));
    int tag = m->member_kind == 2 ? 9 : m->signature.result.tag;
    jclass klass = (jclass)resource_local(env, m->declaring);
    if (rt_status) { if (values != stack) free(values); goto done; }
    jobject object = resource_local(env, receiver->resource);
    if (rt_status) { if (values != stack) free(values); goto done; }
    if (m->member_kind == 2) result.l = (*env)->NewObjectA(env, klass, m->method, values);
    else if (m->member_kind == 1) {
      switch (tag) {
        case 0: (*env)->CallStaticVoidMethodA(env, klass, m->method, values); break;
        case 1: result.z = (*env)->CallStaticBooleanMethodA(env, klass, m->method, values); break;
        case 2: result.b = (*env)->CallStaticByteMethodA(env, klass, m->method, values); break;
        case 3: result.c = (*env)->CallStaticCharMethodA(env, klass, m->method, values); break;
        case 4: result.s = (*env)->CallStaticShortMethodA(env, klass, m->method, values); break;
        case 5: result.i = (*env)->CallStaticIntMethodA(env, klass, m->method, values); break;
        case 6: result.j = (*env)->CallStaticLongMethodA(env, klass, m->method, values); break;
        case 7: result.f = (*env)->CallStaticFloatMethodA(env, klass, m->method, values); break;
        case 8: result.d = (*env)->CallStaticDoubleMethodA(env, klass, m->method, values); break;
        case 9: result.l = (*env)->CallStaticObjectMethodA(env, klass, m->method, values); break;
      }
    } else {
      switch (tag) {
        case 0: (*env)->CallVoidMethodA(env, object, m->method, values); break;
        case 1: result.z = (*env)->CallBooleanMethodA(env, object, m->method, values); break;
        case 2: result.b = (*env)->CallByteMethodA(env, object, m->method, values); break;
        case 3: result.c = (*env)->CallCharMethodA(env, object, m->method, values); break;
        case 4: result.s = (*env)->CallShortMethodA(env, object, m->method, values); break;
        case 5: result.i = (*env)->CallIntMethodA(env, object, m->method, values); break;
        case 6: result.j = (*env)->CallLongMethodA(env, object, m->method, values); break;
        case 7: result.f = (*env)->CallFloatMethodA(env, object, m->method, values); break;
        case 8: result.d = (*env)->CallDoubleMethodA(env, object, m->method, values); break;
        case 9: result.l = (*env)->CallObjectMethodA(env, object, m->method, values); break;
      }
    }
    if (!capture_exception(env, "Java method call")) {
      if (m->member_kind == 2 && !result.l) fail(AJNI_RT_NATIVE_FAILURE, "Constructor returned null");
      else out = result_value(env, tag, result);
    }
  }
  if (values != stack) free(values);
done:
  end(env); return out ? out : value_new(0);
}
MOONBIT_FFI_EXPORT AjniValue *ajni_rt_field_get(AjniMember *member, AjniObject *receiver) {
  JNIEnv *env; AjniValue *out = NULL;
  if (begin(&env)) return value_new(0);
  Resource *m = member ? member->resource : NULL;
  if (check_resource(m)) goto done;
  if (m->member_kind < 3 || check_resource(m->declaring)) { if (!rt_status) fail(AJNI_RT_INVALID_ARGUMENT, "Member is not a field"); goto done; }
  if (receiver_check(env, m, receiver)) goto done;
  jvalue result; memset(&result, 0, sizeof(result));
  jclass klass = (jclass)resource_local(env, m->declaring);
  if (rt_status) goto done;
  jobject object = resource_local(env, receiver->resource);
  if (rt_status) goto done;
  int tag = m->signature.result.tag;
#define AJNI_GET_FIELD(TAG, SLOT, NAME) case TAG: result.SLOT = m->member_kind == 4 ? (*env)->GetStatic##NAME##Field(env, klass, m->field) : (*env)->Get##NAME##Field(env, object, m->field); break
  switch (tag) {
    AJNI_GET_FIELD(1, z, Boolean); AJNI_GET_FIELD(2, b, Byte); AJNI_GET_FIELD(3, c, Char);
    AJNI_GET_FIELD(4, s, Short); AJNI_GET_FIELD(5, i, Int); AJNI_GET_FIELD(6, j, Long);
    AJNI_GET_FIELD(7, f, Float); AJNI_GET_FIELD(8, d, Double); AJNI_GET_FIELD(9, l, Object);
  }
#undef AJNI_GET_FIELD
  if (!capture_exception(env, "Java field get")) out = result_value(env, tag, result);
done:
  end(env); return out ? out : value_new(0);
}
MOONBIT_FFI_EXPORT int ajni_rt_field_set(AjniMember *member, AjniObject *receiver, AjniValue *value) {
  JNIEnv *env;
  if (begin(&env)) return rt_status;
  Resource *m = member ? member->resource : NULL;
  if (check_resource(m)) goto done;
  if (m->member_kind < 3 || check_resource(m->declaring)) { if (!rt_status) fail(AJNI_RT_INVALID_ARGUMENT, "Member is not a field"); goto done; }
  if (receiver_check(env, m, receiver) || value_matches(env, value, &m->signature.result)) goto done;
  jclass klass = (jclass)resource_local(env, m->declaring);
  if (rt_status) goto done;
  jobject object = resource_local(env, receiver->resource);
  if (rt_status) goto done;
  jvalue v = value->value;
  if (value->tag == 9) {
    v.l = resource_local(env, value->object);
    if (rt_status) goto done;
  }
#define AJNI_SET_FIELD(TAG, SLOT, NAME) case TAG: if (m->member_kind == 4) (*env)->SetStatic##NAME##Field(env, klass, m->field, v.SLOT); else (*env)->Set##NAME##Field(env, object, m->field, v.SLOT); break
  switch (value->tag) {
    AJNI_SET_FIELD(1, z, Boolean); AJNI_SET_FIELD(2, b, Byte); AJNI_SET_FIELD(3, c, Char);
    AJNI_SET_FIELD(4, s, Short); AJNI_SET_FIELD(5, i, Int); AJNI_SET_FIELD(6, j, Long);
    AJNI_SET_FIELD(7, f, Float); AJNI_SET_FIELD(8, d, Double); AJNI_SET_FIELD(9, l, Object);
  }
#undef AJNI_SET_FIELD
  capture_exception(env, "Java field set");
done:
  end(env); return rt_status;
}
MOONBIT_FFI_EXPORT AjniValue *ajni_rt_value_int(int tag, int64_t integer) {
  clear_error(); AjniValue *v = value_new(tag);
  switch (tag) {
    case 1: if (integer < 0 || integer > 1) goto invalid; v->value.z = (jboolean)integer; break;
    case 2: if (integer < -128 || integer > 127) goto invalid; v->value.b = (jbyte)integer; break;
    case 3: if (integer < 0 || integer > 65535) goto invalid; v->value.c = (jchar)integer; break;
    case 4: if (integer < -32768 || integer > 32767) goto invalid; v->value.s = (jshort)integer; break;
    case 5: if (integer < INT32_MIN || integer > INT32_MAX) goto invalid; v->value.i = (jint)integer; break;
    case 6: v->value.j = (jlong)integer; break;
    default: goto invalid;
  }
  return v;
invalid:
  fail(AJNI_RT_INVALID_ARGUMENT, "Integer tag or range is invalid"); return v;
}
MOONBIT_FFI_EXPORT AjniValue *ajni_rt_value_float(int tag, double number) {
  clear_error(); AjniValue *v = value_new(tag);
  if (tag == 7) v->value.f = (jfloat)number;
  else if (tag == 8) v->value.d = number;
  else fail(AJNI_RT_INVALID_ARGUMENT, "Expected Float or Double tag");
  return v;
}
MOONBIT_FFI_EXPORT AjniValue *ajni_rt_value_object(AjniObject *object) {
  clear_error(); AjniValue *v = value_new(9);
  if (!check_object(object, 1)) { v->object = object->resource; resource_retain(v->object); }
  return v;
}
MOONBIT_FFI_EXPORT AjniValue *ajni_rt_value_void(void) { clear_error(); return value_new(0); }
MOONBIT_FFI_EXPORT int ajni_rt_value_tag(AjniValue *v) { clear_error(); return v->tag; }
MOONBIT_FFI_EXPORT int64_t ajni_rt_value_integer(AjniValue *v) {
  clear_error();
  switch (v->tag) {
    case 1: return v->value.z; case 2: return v->value.b; case 3: return v->value.c;
    case 4: return v->value.s; case 5: return v->value.i; case 6: return v->value.j;
    default: fail(AJNI_RT_INVALID_ARGUMENT, "Value is not an integer"); return 0;
  }
}
MOONBIT_FFI_EXPORT double ajni_rt_value_number(AjniValue *v) {
  clear_error();
  if (v->tag == 7) return v->value.f;
  if (v->tag == 8) return v->value.d;
  fail(AJNI_RT_INVALID_ARGUMENT, "Value is not floating point"); return 0;
}
MOONBIT_FFI_EXPORT AjniObject *ajni_rt_value_get_object(AjniValue *v) {
  clear_error();
  if (v->tag != 9) { fail(AJNI_RT_INVALID_ARGUMENT, "Value is not an object"); return object_wrap(NULL, 0); }
  if (v->object && check_resource(v->object)) return object_wrap(NULL, 0);
  return object_wrap(v->object, 1);
}
MOONBIT_FFI_EXPORT AjniArgs *ajni_rt_args_new(void) { clear_error(); return args_new(); }
MOONBIT_FFI_EXPORT int ajni_rt_args_push(AjniArgs *a, AjniValue *v) {
  clear_error();
  if (!a || !v || v->tag == 0 || v->error) return fail(AJNI_RT_INVALID_ARGUMENT, "Invalid argument value");
  return args_append(a, v);
}
MOONBIT_FFI_EXPORT int ajni_rt_args_length(AjniArgs *a) { clear_error(); return a->length; }
MOONBIT_FFI_EXPORT AjniValue *ajni_rt_args_get(AjniArgs *a, int index) {
  clear_error();
  if (index < 0 || index >= a->length) { fail(AJNI_RT_INVALID_ARGUMENT, "Argument index is out of bounds"); return value_new(0); }
  moonbit_incref(a->values[index]); return a->values[index];
}
static int array_type(JNIEnv *env, Resource *r) {
  if (r->array_tag) return r->array_tag;
  static const char *names[] = {NULL,"[Z","[B","[C","[S","[I","[J","[F","[D","[Ljava/lang/Object;"};
  for (int tag = 1; tag <= 9; ++tag) {
    jclass klass = (*env)->FindClass(env, names[tag]);
    if (capture_exception(env, "FindClass(array type)")) return 0;
    if (!klass) { fail(AJNI_RT_NATIVE_FAILURE, "Array type class is null"); return 0; }
    jboolean valid = (*env)->IsInstanceOf(env, r->global, klass);
    int thrown = capture_exception(env, "IsInstanceOf(array)");
    (*env)->DeleteLocalRef(env, klass);
    if (thrown) return 0;
    if (valid) { r->array_tag = tag; return tag; }
  }
  fail(AJNI_RT_INVALID_ARGUMENT, "Object is not an array"); return 0;
}
static int array_bounds(JNIEnv *env, Resource *r, int start, int length) {
  jsize n = (*env)->GetArrayLength(env, (jarray)r->global);
  if (capture_exception(env, "GetArrayLength")) return rt_status;
  if (start < 0 || length < 0 || start > n || length > n - start) return fail(AJNI_RT_INVALID_ARGUMENT, "Array region is out of bounds");
  return 0;
}
MOONBIT_FFI_EXPORT AjniObject *ajni_rt_array_new(AjniSession *s, int tag, int length) {
  JNIEnv *env; Resource *r = NULL;
  if (begin(&env)) return object_wrap(NULL, 0);
  if (!check_session(s)) {
    if (length < 0 || tag < 1 || tag > 8) fail(AJNI_RT_INVALID_ARGUMENT, "Invalid primitive array type or length");
    else {
      jarray array = NULL;
      switch (tag) {
        case 1: array = (*env)->NewBooleanArray(env, length); break;
        case 2: array = (*env)->NewByteArray(env, length); break;
        case 3: array = (*env)->NewCharArray(env, length); break;
        case 4: array = (*env)->NewShortArray(env, length); break;
        case 5: array = (*env)->NewIntArray(env, length); break;
        case 6: array = (*env)->NewLongArray(env, length); break;
        case 7: array = (*env)->NewFloatArray(env, length); break;
        case 8: array = (*env)->NewDoubleArray(env, length); break;
      }
      if (!capture_exception(env, "New primitive array")) {
        if (!array) fail(AJNI_RT_NATIVE_FAILURE, "Primitive array allocation returned null");
        else { r = global_resource(env, array, 0); if (r) r->array_tag = tag; }
      }
    }
  }
  end(env); return object_wrap(r, 0);
}
MOONBIT_FFI_EXPORT AjniObject *ajni_rt_object_array_new(AjniObject *component, int length, AjniObject *initial) {
  JNIEnv *env; Resource *r = NULL;
  if (begin(&env)) return object_wrap(NULL, 0);
  if (!check_object(component, 0) && !check_object(initial, 1)) {
    if (!component->resource->class_flag || length < 0) fail(AJNI_RT_INVALID_ARGUMENT, "Expected component class and nonnegative length");
    else {
      int valid = 1;
      if (initial->resource) {
        valid = (*env)->IsInstanceOf(env, initial->resource->global, (jclass)component->resource->global);
        if (capture_exception(env, "IsInstanceOf(array initial)")) valid = 0;
        else if (!valid) fail(AJNI_RT_INVALID_ARGUMENT, "Initial value is not assignable to array component");
      }
      if (valid) {
        jobjectArray array = (*env)->NewObjectArray(env, length, (jclass)component->resource->global, initial->resource ? initial->resource->global : NULL);
        if (!capture_exception(env, "NewObjectArray")) {
          if (!array) fail(AJNI_RT_NATIVE_FAILURE, "Object array allocation returned null");
          else { r = global_resource(env, array, 0); if (r) r->array_tag = 9; }
        }
      }
    }
  }
  end(env); return object_wrap(r, 0);
}
MOONBIT_FFI_EXPORT int ajni_rt_array_length(AjniObject *o) {
  JNIEnv *env; int length = 0;
  if (begin(&env)) return 0;
  if (!check_object(o, 0) && array_type(env, o->resource)) {
    length = (*env)->GetArrayLength(env, (jarray)o->resource->global);
    capture_exception(env, "GetArrayLength");
  }
  end(env); return length;
}
static AjniValue *array_get_one(JNIEnv *env, Resource *r, int tag, int index) {
  jvalue result; memset(&result, 0, sizeof(result));
#define AJNI_GET_ELEMENT(TAG, SLOT, NAME, TYPE) case TAG: (*env)->Get##NAME##ArrayRegion(env, (TYPE)r->global, index, 1, &result.SLOT); break
  switch (tag) {
    AJNI_GET_ELEMENT(1,z,Boolean,jbooleanArray); AJNI_GET_ELEMENT(2,b,Byte,jbyteArray);
    AJNI_GET_ELEMENT(3,c,Char,jcharArray); AJNI_GET_ELEMENT(4,s,Short,jshortArray);
    AJNI_GET_ELEMENT(5,i,Int,jintArray); AJNI_GET_ELEMENT(6,j,Long,jlongArray);
    AJNI_GET_ELEMENT(7,f,Float,jfloatArray); AJNI_GET_ELEMENT(8,d,Double,jdoubleArray);
    case 9: result.l = (*env)->GetObjectArrayElement(env, (jobjectArray)r->global, index); break;
  }
#undef AJNI_GET_ELEMENT
  if (capture_exception(env, "Get array element")) return value_new(0);
  AjniValue *out = result_value(env, tag, result);
  if (tag == 9 && result.l) (*env)->DeleteLocalRef(env, result.l);
  return out;
}
static int array_set_one(JNIEnv *env, Resource *r, int tag, int index, AjniValue *v) {
  if (!v || v->error || v->tag != tag) return fail(AJNI_RT_INVALID_ARGUMENT, "Array element type mismatch");
  if (tag == 9 && v->object && check_resource(v->object)) return rt_status;
  if (tag == 9 && v->object) {
    jclass array_class = (*env)->GetObjectClass(env, r->global);
    if (capture_exception(env, "GetObjectClass(array)")) return rt_status;
    jclass class_class = (*env)->FindClass(env, "java/lang/Class");
    if (capture_exception(env, "FindClass(Class)")) return rt_status;
    if (!array_class || !class_class) return fail(AJNI_RT_NATIVE_FAILURE, "Array class lookup returned null");
    jmethodID method = (*env)->GetMethodID(env, class_class, "getComponentType", "()Ljava/lang/Class;");
    if (capture_exception(env, "GetMethodID(getComponentType)")) return rt_status;
    if (!method) return fail(AJNI_RT_NATIVE_FAILURE, "getComponentType method is null");
    jclass component = (jclass)(*env)->CallObjectMethodA(env, array_class, method, NULL);
    if (capture_exception(env, "Class.getComponentType")) return rt_status;
    if (!component) return fail(AJNI_RT_NATIVE_FAILURE, "Array component class is null");
    jboolean valid = (*env)->IsInstanceOf(env, v->object->global, component);
    int thrown = capture_exception(env, "IsInstanceOf(array element)");
    (*env)->DeleteLocalRef(env, component); (*env)->DeleteLocalRef(env, class_class); (*env)->DeleteLocalRef(env, array_class);
    if (thrown) return rt_status;
    if (!valid) return fail(AJNI_RT_INVALID_ARGUMENT, "Object is not assignable to array component");
  }
#define AJNI_SET_ELEMENT(TAG, SLOT, NAME, TYPE) case TAG: (*env)->Set##NAME##ArrayRegion(env, (TYPE)r->global, index, 1, &v->value.SLOT); break
  switch (tag) {
    AJNI_SET_ELEMENT(1,z,Boolean,jbooleanArray); AJNI_SET_ELEMENT(2,b,Byte,jbyteArray);
    AJNI_SET_ELEMENT(3,c,Char,jcharArray); AJNI_SET_ELEMENT(4,s,Short,jshortArray);
    AJNI_SET_ELEMENT(5,i,Int,jintArray); AJNI_SET_ELEMENT(6,j,Long,jlongArray);
    AJNI_SET_ELEMENT(7,f,Float,jfloatArray); AJNI_SET_ELEMENT(8,d,Double,jdoubleArray);
    case 9: (*env)->SetObjectArrayElement(env, (jobjectArray)r->global, index, v->object ? v->object->global : NULL); break;
  }
#undef AJNI_SET_ELEMENT
  capture_exception(env, "Set array element"); return rt_status;
}
MOONBIT_FFI_EXPORT AjniValue *ajni_rt_array_get(AjniObject *o, int index) {
  JNIEnv *env; AjniValue *out = NULL;
  if (begin(&env)) return value_new(0);
  if (!check_object(o, 0)) {
    int tag = array_type(env, o->resource);
    if (tag && !array_bounds(env, o->resource, index, 1)) out = array_get_one(env, o->resource, tag, index);
  }
  end(env); return out ? out : value_new(0);
}
MOONBIT_FFI_EXPORT int ajni_rt_array_set(AjniObject *o, int index, AjniValue *v) {
  JNIEnv *env;
  if (begin(&env)) return rt_status;
  if (!check_object(o, 0)) {
    int tag = array_type(env, o->resource);
    if (tag && !array_bounds(env, o->resource, index, 1)) array_set_one(env, o->resource, tag, index, v);
  }
  end(env); return rt_status;
}
static size_t primitive_size(int tag) {
  static const size_t sizes[] = {0,sizeof(jboolean),sizeof(jbyte),sizeof(jchar),sizeof(jshort),sizeof(jint),sizeof(jlong),sizeof(jfloat),sizeof(jdouble)};
  return sizes[tag];
}
MOONBIT_FFI_EXPORT AjniArgs *ajni_rt_array_read(AjniObject *o, int start, int length) {
  JNIEnv *env;
  if (begin(&env)) return args_new();
  AjniArgs *out = args_new();
  if (check_object(o, 0)) goto done;
  int tag = array_type(env, o->resource);
  if (!tag || array_bounds(env, o->resource, start, length) || !length) goto done;
  if (tag == 9) {
    for (int i = 0; i < length && !rt_status; ++i) {
      AjniValue *v = array_get_one(env, o->resource, tag, start + i);
      if (!rt_status) args_append(out, v);
      moonbit_decref(v);
    }
    goto done;
  }
  union { jvalue alignment; unsigned char bytes[512]; } stack;
  size_t size = primitive_size(tag);
  if ((size_t)length > SIZE_MAX / size) { fail(AJNI_RT_NATIVE_FAILURE, "Array region allocation overflow"); goto done; }
  size_t bytes = (size_t)length * size;
  void *buffer = bytes <= sizeof(stack.bytes) ? stack.bytes : malloc(bytes);
  if (!buffer) { fail(AJNI_RT_NATIVE_FAILURE, "Array region allocation failed"); goto done; }
#define AJNI_READ_REGION(TAG, NAME, TYPE, SCALAR) case TAG: (*env)->Get##NAME##ArrayRegion(env, (TYPE)o->resource->global, start, length, (SCALAR *)buffer); break
  switch (tag) {
    AJNI_READ_REGION(1,Boolean,jbooleanArray,jboolean); AJNI_READ_REGION(2,Byte,jbyteArray,jbyte);
    AJNI_READ_REGION(3,Char,jcharArray,jchar); AJNI_READ_REGION(4,Short,jshortArray,jshort);
    AJNI_READ_REGION(5,Int,jintArray,jint); AJNI_READ_REGION(6,Long,jlongArray,jlong);
    AJNI_READ_REGION(7,Float,jfloatArray,jfloat); AJNI_READ_REGION(8,Double,jdoubleArray,jdouble);
  }
#undef AJNI_READ_REGION
  if (!capture_exception(env, "Read primitive array region")) {
    for (int i = 0; i < length && !rt_status; ++i) {
      AjniValue *v = value_new(tag);
#define AJNI_UNPACK(TAG, SLOT, TYPE) case TAG: v->value.SLOT = ((TYPE *)buffer)[i]; break
      switch (tag) {
        AJNI_UNPACK(1,z,jboolean); AJNI_UNPACK(2,b,jbyte); AJNI_UNPACK(3,c,jchar); AJNI_UNPACK(4,s,jshort);
        AJNI_UNPACK(5,i,jint); AJNI_UNPACK(6,j,jlong); AJNI_UNPACK(7,f,jfloat); AJNI_UNPACK(8,d,jdouble);
      }
#undef AJNI_UNPACK
      args_append(out, v); moonbit_decref(v);
    }
  }
  if (buffer != stack.bytes) free(buffer);
done:
  end(env); return out;
}
MOONBIT_FFI_EXPORT int ajni_rt_array_write(AjniObject *o, int start, AjniArgs *values) {
  JNIEnv *env;
  if (begin(&env)) return rt_status;
  if (check_object(o, 0)) goto done;
  if (!values) { fail(AJNI_RT_INVALID_ARGUMENT, "Array values are missing"); goto done; }
  int tag = array_type(env, o->resource);
  int length = values->length;
  if (!tag || array_bounds(env, o->resource, start, length) || !length) goto done;
  for (int i = 0; i < length; ++i) {
    AjniValue *v = values->values[i];
    if (!v || v->error || v->tag != tag) { fail(AJNI_RT_INVALID_ARGUMENT, "Array element type mismatch"); goto done; }
    if (tag == 9 && v->object && check_resource(v->object)) goto done;
  }
  if (tag == 9) {
    jclass array_class = (*env)->GetObjectClass(env, o->resource->global);
    if (capture_exception(env, "GetObjectClass(array region)")) goto done;
    jclass class_class = (*env)->FindClass(env, "java/lang/Class");
    if (capture_exception(env, "FindClass(Class)")) goto done;
    if (!array_class || !class_class) { fail(AJNI_RT_NATIVE_FAILURE, "Array class lookup returned null"); goto done; }
    jmethodID method = (*env)->GetMethodID(env, class_class, "getComponentType", "()Ljava/lang/Class;");
    if (capture_exception(env, "GetMethodID(getComponentType)")) goto done;
    if (!method) { fail(AJNI_RT_NATIVE_FAILURE, "Array component method is null"); goto done; }
    jclass component = (jclass)(*env)->CallObjectMethodA(env, array_class, method, NULL);
    if (capture_exception(env, "Class.getComponentType")) goto done;
    if (!component) { fail(AJNI_RT_NATIVE_FAILURE, "Array component class is null"); goto done; }
    for (int i = 0; i < length; ++i) if (values->values[i]->object) {
      jboolean valid = (*env)->IsInstanceOf(env, values->values[i]->object->global, component);
      if (capture_exception(env, "IsInstanceOf(array region element)")) goto done;
      if (!valid) { fail(AJNI_RT_INVALID_ARGUMENT, "Object is not assignable to array component"); goto done; }
    }
    for (int i = 0; i < length; ++i) {
      Resource *r = values->values[i]->object;
      (*env)->SetObjectArrayElement(env, (jobjectArray)o->resource->global, start + i, r ? r->global : NULL);
      if (capture_exception(env, "Write object array region")) break;
    }
    goto done;
  }
  union { jvalue alignment; unsigned char bytes[512]; } stack;
  size_t size = primitive_size(tag);
  if ((size_t)length > SIZE_MAX / size) { fail(AJNI_RT_NATIVE_FAILURE, "Array region allocation overflow"); goto done; }
  size_t bytes = (size_t)length * size;
  void *buffer = bytes <= sizeof(stack.bytes) ? stack.bytes : malloc(bytes);
  if (!buffer) { fail(AJNI_RT_NATIVE_FAILURE, "Array region allocation failed"); goto done; }
  for (int i = 0; i < length; ++i) {
    AjniValue *v = values->values[i];
#define AJNI_PACK(TAG, SLOT, TYPE) case TAG: ((TYPE *)buffer)[i] = v->value.SLOT; break
    switch (tag) {
      AJNI_PACK(1,z,jboolean); AJNI_PACK(2,b,jbyte); AJNI_PACK(3,c,jchar); AJNI_PACK(4,s,jshort);
      AJNI_PACK(5,i,jint); AJNI_PACK(6,j,jlong); AJNI_PACK(7,f,jfloat); AJNI_PACK(8,d,jdouble);
    }
#undef AJNI_PACK
  }
#define AJNI_WRITE_REGION(TAG, NAME, TYPE, SCALAR) case TAG: (*env)->Set##NAME##ArrayRegion(env, (TYPE)o->resource->global, start, length, (const SCALAR *)buffer); break
  switch (tag) {
    AJNI_WRITE_REGION(1,Boolean,jbooleanArray,jboolean); AJNI_WRITE_REGION(2,Byte,jbyteArray,jbyte);
    AJNI_WRITE_REGION(3,Char,jcharArray,jchar); AJNI_WRITE_REGION(4,Short,jshortArray,jshort);
    AJNI_WRITE_REGION(5,Int,jintArray,jint); AJNI_WRITE_REGION(6,Long,jlongArray,jlong);
    AJNI_WRITE_REGION(7,Float,jfloatArray,jfloat); AJNI_WRITE_REGION(8,Double,jdoubleArray,jdouble);
  }
#undef AJNI_WRITE_REGION
  capture_exception(env, "Write primitive array region");
  if (buffer != stack.bytes) free(buffer);
done:
  end(env); return rt_status;
}
static const char *box_classes[] = {NULL,"java/lang/Boolean","java/lang/Byte","java/lang/Character","java/lang/Short","java/lang/Integer","java/lang/Long","java/lang/Float","java/lang/Double"};
static const char *unbox_names[] = {NULL,"booleanValue","byteValue","charValue","shortValue","intValue","longValue","floatValue","doubleValue"};
static const char *unbox_signatures[] = {NULL,"()Z","()B","()C","()S","()I","()J","()F","()D"};
static const char *box_signatures[] = {NULL,"(Z)Ljava/lang/Boolean;","(B)Ljava/lang/Byte;","(C)Ljava/lang/Character;","(S)Ljava/lang/Short;","(I)Ljava/lang/Integer;","(J)Ljava/lang/Long;","(F)Ljava/lang/Float;","(D)Ljava/lang/Double;"};
static AjniValue *unbox_value(JNIEnv *env, jobject object, Type *type) {
  if (type->tag == 9) {
    if (object) {
      jboolean valid = (*env)->IsInstanceOf(env, object, type->klass);
      if (capture_exception(env, "IsInstanceOf(callback argument)")) return NULL;
      if (!valid) { fail(AJNI_RT_INVALID_ARGUMENT, "Callback object argument is not assignable"); return NULL; }
    }
    jvalue value; memset(&value, 0, sizeof(value)); value.l = object;
    return result_value(env, 9, value);
  }
  if (!object) { fail(AJNI_RT_INVALID_ARGUMENT, "Callback primitive argument is null"); return NULL; }
  jclass klass = (*env)->FindClass(env, box_classes[type->tag]);
  if (capture_exception(env, "FindClass(callback wrapper)")) return NULL;
  if (!klass) { fail(AJNI_RT_NATIVE_FAILURE, "Primitive wrapper class is null"); return NULL; }
  jboolean valid = (*env)->IsInstanceOf(env, object, klass);
  if (capture_exception(env, "IsInstanceOf(callback wrapper)")) return NULL;
  if (!valid) { fail(AJNI_RT_INVALID_ARGUMENT, "Callback primitive wrapper does not match descriptor"); return NULL; }
  jmethodID method = (*env)->GetMethodID(env, klass, unbox_names[type->tag], unbox_signatures[type->tag]);
  if (capture_exception(env, "GetMethodID(unbox)")) return NULL;
  if (!method) { fail(AJNI_RT_NATIVE_FAILURE, "Unbox method is null"); return NULL; }
  jvalue value; memset(&value, 0, sizeof(value));
  switch (type->tag) {
    case 1: value.z = (*env)->CallBooleanMethodA(env, object, method, NULL); break;
    case 2: value.b = (*env)->CallByteMethodA(env, object, method, NULL); break;
    case 3: value.c = (*env)->CallCharMethodA(env, object, method, NULL); break;
    case 4: value.s = (*env)->CallShortMethodA(env, object, method, NULL); break;
    case 5: value.i = (*env)->CallIntMethodA(env, object, method, NULL); break;
    case 6: value.j = (*env)->CallLongMethodA(env, object, method, NULL); break;
    case 7: value.f = (*env)->CallFloatMethodA(env, object, method, NULL); break;
    case 8: value.d = (*env)->CallDoubleMethodA(env, object, method, NULL); break;
  }
  int thrown = capture_exception(env, "Unbox callback argument");
  (*env)->DeleteLocalRef(env, klass);
  return thrown ? NULL : result_value(env, type->tag, value);
}
static jobject box_value(JNIEnv *env, AjniValue *value, Type *type) {
  if (value_matches(env, value, type)) return NULL;
  if (type->tag == 0) return NULL;
  if (type->tag == 9) {
    jobject local = (*env)->NewLocalRef(env, value->object ? value->object->global : NULL);
    capture_exception(env, "NewLocalRef(callback result)");
    return local;
  }
  jclass klass = (*env)->FindClass(env, box_classes[type->tag]);
  if (capture_exception(env, "FindClass(callback result wrapper)")) return NULL;
  if (!klass) { fail(AJNI_RT_NATIVE_FAILURE, "Primitive result wrapper class is null"); return NULL; }
  jmethodID method = (*env)->GetStaticMethodID(env, klass, "valueOf", box_signatures[type->tag]);
  if (capture_exception(env, "GetStaticMethodID(valueOf)")) return NULL;
  if (!method) { fail(AJNI_RT_NATIVE_FAILURE, "Box method is null"); return NULL; }
  jobject object = (*env)->CallStaticObjectMethodA(env, klass, method, &value->value);
  int thrown = capture_exception(env, "Box callback result");
  (*env)->DeleteLocalRef(env, klass);
  if (!thrown && !object) fail(AJNI_RT_NATIVE_FAILURE, "Primitive boxing returned null");
  return object;
}
static void callback_mark_closed(uint64_t token) {
  pthread_mutex_lock(&registry_lock);
  for (Callback *c = runtime.callbacks; c; c = c->next) if (c->token == token) { c->closed = 1; break; }
  pthread_mutex_unlock(&registry_lock);
}
static void JNICALL callback_java_close(JNIEnv *env, jclass klass, jlong token) {
  (void)env; (void)klass;
  callback_mark_closed((uint64_t)token);
}
static jobject JNICALL callback_dispatch(JNIEnv *env, jclass klass, jlong token, jobjectArray boxed) {
  (void)klass;
  if ((*env)->ExceptionCheck(env)) return NULL;
  pthread_mutex_lock(&registry_lock);
  int live = runtime.live;
  int owner = runtime.initialized && pthread_equal(runtime.owner, pthread_self());
  Callback *callback = NULL;
  if (live && owner) for (Callback *c = runtime.callbacks; c; c = c->next) if (c->token == (uint64_t)token && c->generation == runtime.generation && !c->closed) { callback = c; break; }
  void *closure = NULL;
  if (callback && callback->closure) { ++runtime.active; ++callback->active; closure = callback->closure; moonbit_incref(closure); }
  pthread_mutex_unlock(&registry_lock);
  if (!owner || !live || !callback) {
    throw_text(env, !owner ? AJNI_RT_WRONG_THREAD : AJNI_RT_CLOSED, !owner ? "Callback belongs to another OS thread" : "Callback is closed");
    return NULL;
  }
  drain_callbacks(); clear_error();
  int pushed = (*env)->PushLocalFrame(env, 32);
  if (capture_exception(env, "PushLocalFrame(callback)") || pushed < 0) {
    if (!rt_status) fail(AJNI_RT_NATIVE_FAILURE, "Callback local frame failed");
    if (closure) moonbit_decref(closure);
    --runtime.active;
    pthread_mutex_lock(&registry_lock); --callback->active; pthread_mutex_unlock(&registry_lock);
    ajni_rt_host_throw(env); return NULL;
  }
  jobject result = NULL;
  AjniArgs *args = NULL;
  AjniValue *value = NULL;
  if (!closure) { fail(AJNI_RT_CLOSED, "Callback was closed before dispatch"); goto done; }
  if (!boxed) { fail(AJNI_RT_INVALID_ARGUMENT, "Callback argument array is null"); goto done; }
  jsize count = (*env)->GetArrayLength(env, boxed);
  if (capture_exception(env, "GetArrayLength(callback arguments)")) goto done;
  if (count != callback->signature.count) { fail(AJNI_RT_INVALID_ARGUMENT, "Callback argument count does not match descriptor"); goto done; }
  args = args_new();
  for (int i = 0; i < count; ++i) {
    jobject object = (*env)->GetObjectArrayElement(env, boxed, i);
    if (capture_exception(env, "GetObjectArrayElement(callback argument)")) goto done;
    AjniValue *arg = unbox_value(env, object, &callback->signature.parameters[i]);
    if (object) (*env)->DeleteLocalRef(env, object);
    if (!arg) goto done;
    if (!rt_status) args_append(args, arg);
    moonbit_decref(arg);
    if (rt_status) goto done;
  }
  {
    AjniArgs *owned_args = args; args = NULL;
    void *owned_closure = closure; closure = NULL;
    value = callback->trampoline(owned_closure, owned_args);
  }
  if (!value) { fail(AJNI_RT_NATIVE_FAILURE, "Callback returned no managed value"); goto done; }
  if (value->error) {
    if (value->error == AJNI_RT_JAVA_THROWN) {
      if (validate_throwable(env, value->object)) goto done;
      resource_release(rt_exception); rt_exception = value->object; resource_retain(rt_exception);
    }
    fail(value->error, value->message ? value->message : "MoonBit callback failed");
    goto done;
  }
  clear_error();
  result = box_value(env, value, &callback->signature.result);
done:
  if (closure) moonbit_decref(closure);
  if (args) moonbit_decref(args);
  if (value) moonbit_decref(value);
  if (rt_status) ajni_rt_host_throw(env);
  result = (*env)->PopLocalFrame(env, result);
  --runtime.active;
  pthread_mutex_lock(&registry_lock); --callback->active; pthread_mutex_unlock(&registry_lock);
  return result;
}
int ajni_rt_register_companion(void *raw_env) {
  JNIEnv *env = raw_env;
  if (!env) return fail(AJNI_RT_INVALID_ARGUMENT, "JNIEnv is null");
  if ((*env)->ExceptionCheck(env)) return fail(AJNI_RT_JAVA_THROWN, "Companion registration has pending exception");
  JavaVM *vm = NULL;
  if ((*env)->GetJavaVM(env, &vm) != JNI_OK || !vm) return fail(AJNI_RT_NATIVE_FAILURE, "GetJavaVM failed");
  pthread_mutex_lock(&registry_lock);
  int different = runtime.vm && runtime.vm != vm;
  if (!different) runtime.vm = vm;
  pthread_mutex_unlock(&registry_lock);
  if (different) return fail(AJNI_RT_INVALID_ARGUMENT, "Companion belongs to another JVM");
  jclass klass = (*env)->FindClass(env, "dev/nanaloveyuki/ajni/AjniCallback");
  if ((*env)->ExceptionCheck(env)) {
    if (runtime.live && pthread_equal(runtime.owner, pthread_self())) capture_exception(env, "FindClass(AjniCallback)");
    else fail(AJNI_RT_JAVA_THROWN, "FindClass(AjniCallback) failed");
    return rt_status;
  }
  if (!klass) return fail(AJNI_RT_NATIVE_FAILURE, "AjniCallback class is null");
  JNINativeMethod methods[] = {
    {"dispatch", "(J[Ljava/lang/Object;)Ljava/lang/Object;", (void *)callback_dispatch},
    {"close", "(J)V", (void *)callback_java_close}
  };
  jint status = (*env)->RegisterNatives(env, klass, methods, 2);
  int thrown = (*env)->ExceptionCheck(env);
  (*env)->DeleteLocalRef(env, klass);
  if (thrown) {
    if (runtime.live && pthread_equal(runtime.owner, pthread_self())) capture_exception(env, "RegisterNatives(AjniCallback)");
    else fail(AJNI_RT_JAVA_THROWN, "RegisterNatives(AjniCallback) failed");
    return rt_status;
  }
  if (status != JNI_OK) return fail(AJNI_RT_NATIVE_FAILURE, "RegisterNatives(AjniCallback) failed");
  rt_status = 0; rt_message[0] = 0; return 0;
}
MOONBIT_FFI_EXPORT AjniObject *ajni_rt_callback(AjniSession *session, moonbit_string_t descriptor, AjniTrampoline trampoline, void *closure) {
  JNIEnv *env; Resource *r = NULL;
  if (begin(&env)) { moonbit_decref(closure); return object_wrap(NULL, 0); }
  if (check_session(session)) goto done;
  if (!closure || !trampoline) { fail(AJNI_RT_INVALID_ARGUMENT, "Callback closure and trampoline are required"); goto done; }
  char *text = ascii_input(descriptor);
  if (!text) goto done;
  Signature signature;
  int parsed = parse_signature(text, &signature, 1, 1);
  free(text);
  if (parsed) goto done;
  if (resolve_signature(env, &signature, NULL)) { signature_free(&signature, env); goto done; }
  Callback *callback = calloc(1, sizeof(*callback));
  if (!callback) { signature_free(&signature, env); fail(AJNI_RT_NATIVE_FAILURE, "Callback allocation failed"); goto done; }
  pthread_mutex_lock(&registry_lock);
  if (runtime.next_token == INT64_MAX) {
    pthread_mutex_unlock(&registry_lock); free(callback); signature_free(&signature, env);
    fail(AJNI_RT_NATIVE_FAILURE, "Callback token IDs exhausted"); goto done;
  }
  callback->token = ++runtime.next_token;
  callback->generation = runtime.generation;
  callback->signature = signature;
  callback->trampoline = trampoline; callback->closure = closure; closure = NULL;
  callback->next = runtime.callbacks; runtime.callbacks = callback;
  pthread_mutex_unlock(&registry_lock);
  jclass klass = resolve_class(env, "dev/nanaloveyuki/ajni/AjniCallback");
  if (klass) {
    jmethodID constructor = (*env)->GetMethodID(env, klass, "<init>", "(J)V");
    if (!capture_exception(env, "GetMethodID(AjniCallback constructor)")) {
      if (!constructor) fail(AJNI_RT_NATIVE_FAILURE, "AjniCallback constructor is null");
      else {
        jvalue arg; arg.j = (jlong)callback->token;
        jobject object = (*env)->NewObjectA(env, klass, constructor, &arg);
        if (!capture_exception(env, "NewObjectA(AjniCallback)")) {
          if (!object) fail(AJNI_RT_NATIVE_FAILURE, "AjniCallback construction returned null");
          else {
            callback->companion = (*env)->NewWeakGlobalRef(env, object);
            if (!capture_exception(env, "NewWeakGlobalRef(AjniCallback)")) {
              if (!callback->companion) fail(AJNI_RT_NATIVE_FAILURE, "Callback weak reference is null");
              else { r = global_resource(env, object, 0); if (r) r->token = callback->token; }
            }
          }
        }
      }
    }
  }
  if (!r) { callback_mark_closed(callback->token); drain_callbacks(); }
done:
  if (closure) moonbit_decref(closure);
  end(env); return object_wrap(r, 0);
}
MOONBIT_FFI_EXPORT int ajni_rt_callback_close(AjniObject *object) {
  JNIEnv *env;
  if (gate(&env)) return rt_status;
  if (!object || !object->resource || !object->resource->token) return fail(AJNI_RT_INVALID_ARGUMENT, "Object is not an AjniCallback handle");
  callback_mark_closed(object->resource->token);
  drain_callbacks();
  return 0;
}
MOONBIT_FFI_EXPORT AjniValue *ajni_rt_callback_error(int status, AjniObject *throwable, moonbit_string_t message) {
  clear_error(); AjniValue *v = value_new(0);
  v->error = status >= 1 && status <= 8 ? status : AJNI_RT_NATIVE_FAILURE;
  if (status == AJNI_RT_JAVA_THROWN && throwable && throwable->resource) {
    v->object = throwable->resource; resource_retain(v->object);
  }
  int n = Moonbit_array_length(message);
  v->message = malloc((size_t)n + 1);
  if (v->message) { for (int i = 0; i < n; ++i) v->message[i] = message[i] > 0 && message[i] < 128 ? (char)message[i] : '?'; v->message[n] = 0; }
  return v;
}
MOONBIT_FFI_EXPORT AjniObject *ajni_rt_import_object(AjniSession *session, uint64_t raw_env, uint64_t raw_ref, int class_flag) {
  JNIEnv *env; Resource *r = NULL;
  if (begin(&env)) return object_wrap(NULL, 0);
  if (!check_session(session)) {
    if ((JNIEnv *)(uintptr_t)raw_env != env || (class_flag != 0 && class_flag != 1)) fail(AJNI_RT_INVALID_ARGUMENT, "Invalid import environment or class flag");
    else if (raw_ref) r = global_resource(env, (jobject)(uintptr_t)raw_ref, class_flag);
  }
  end(env); return object_wrap(r, 0);
}
MOONBIT_FFI_EXPORT uint64_t ajni_rt_export_local(AjniObject *object, uint64_t raw_env) {
  JNIEnv *env; jobject local = NULL;
  if (gate(&env) || check_object(object, 1)) return 0;
  if ((JNIEnv *)(uintptr_t)raw_env != env) { fail(AJNI_RT_INVALID_ARGUMENT, "Invalid export environment"); return 0; }
  if (object->resource) { local = (*env)->NewLocalRef(env, object->resource->global); capture_exception(env, "NewLocalRef(export)"); }
  return (uint64_t)(uintptr_t)local;
}
MOONBIT_FFI_EXPORT uint64_t ajni_rt_raw_vm(AjniSession *session) {
  JNIEnv *env;
  if (gate(&env) || check_session(session)) return 0;
  return (uint64_t)(uintptr_t)runtime.vm;
}
MOONBIT_FFI_EXPORT uint64_t ajni_rt_raw_env(AjniSession *session) {
  JNIEnv *env;
  if (gate(&env) || check_session(session)) return 0;
  return (uint64_t)(uintptr_t)env;
}
MOONBIT_FFI_EXPORT int ajni_rt_register_native(AjniObject *klass, moonbit_string_t name, moonbit_string_t descriptor, uint64_t function) {
  JNIEnv *env;
  if (begin(&env)) return rt_status;
  if (!check_object(klass, 0)) {
    if (!klass->resource->class_flag || !function) fail(AJNI_RT_INVALID_ARGUMENT, "Native registration requires a class and function pointer");
    else {
      char *n = ascii_input(name), *d = NULL;
      if (n) d = ascii_input(descriptor);
      if (n && d) {
        Signature signature;
        if (!parse_signature(d, &signature, 1, 1)) {
          signature_free(&signature, env);
          JNINativeMethod method = { n, d, (void *)(uintptr_t)function };
          jint result = (*env)->RegisterNatives(env, (jclass)klass->resource->global, &method, 1);
          if (!capture_exception(env, "RegisterNatives")) { if (result != JNI_OK) fail(AJNI_RT_NATIVE_FAILURE, "RegisterNatives failed"); }
        }
      }
      free(n); free(d);
    }
  }
  end(env); return rt_status;
}
int ajni_rt_env_acquire(AjniEnvLease *lease) {
  if (!lease) return fail(AJNI_RT_INVALID_ARGUMENT, "Lease output is null");
  memset(lease, 0, sizeof(*lease));
  pthread_mutex_lock(&registry_lock);
  JavaVM *vm = runtime.vm;
  int closed = runtime.closing || (runtime.initialized && !runtime.live);
  if (!closed && vm) ++runtime.leases;
  pthread_mutex_unlock(&registry_lock);
  if (closed) return fail(AJNI_RT_CLOSED, "JNI session is closed");
  if (!vm) return fail(AJNI_RT_NOT_INITIALIZED, "No JVM is registered");
  JNIEnv *env = NULL;
  jint status = (*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6);
  if (status == JNI_EDETACHED) {
#if defined(__ANDROID__)
    status = (*vm)->AttachCurrentThread(vm, &env, NULL);
#else
    status = (*vm)->AttachCurrentThread(vm, (void **)&env, NULL);
#endif
    if (status != JNI_OK) {
      pthread_mutex_lock(&registry_lock); --runtime.leases; pthread_mutex_unlock(&registry_lock);
      return fail(AJNI_RT_NATIVE_FAILURE, "AttachCurrentThread failed");
    }
    lease->attached = 1;
  } else if (status != JNI_OK) {
    pthread_mutex_lock(&registry_lock); --runtime.leases; pthread_mutex_unlock(&registry_lock);
    return fail(AJNI_RT_NATIVE_FAILURE, "GetEnv failed");
  }
  lease->vm = vm; lease->env = env;
  rt_status = 0; rt_message[0] = 0;
  return 0;
}
void ajni_rt_env_release(AjniEnvLease *lease) {
  if (!lease) return;
  if (lease->attached && lease->vm) {
    if (raw_exception && lease->env) {
      JNIEnv *env = lease->env;
      (*env)->DeleteGlobalRef(env, raw_exception);
      raw_exception = NULL; raw_exception_vm = NULL;
    }
    JavaVM *vm = lease->vm;
    (*vm)->DetachCurrentThread(vm);
  }
  if (lease->vm) {
    pthread_mutex_lock(&registry_lock); --runtime.leases; pthread_mutex_unlock(&registry_lock);
  }
  memset(lease, 0, sizeof(*lease));
}
int ajni_rt_capture_exception(void *raw_env, const char *operation) {
  JNIEnv *env = raw_env;
  if (!env) return fail(AJNI_RT_INVALID_ARGUMENT, "JNIEnv is null");
  if (!(*env)->ExceptionCheck(env)) return 0;
  jthrowable original = (*env)->ExceptionOccurred(env);
  (*env)->ExceptionClear(env);
  jobject global = original ? (*env)->NewGlobalRef(env, original) : NULL;
  int secondary = (*env)->ExceptionCheck(env);
  if (secondary) (*env)->ExceptionClear(env);
  if (!global) {
    if (original) (*env)->Throw(env, original);
    if (original) (*env)->DeleteLocalRef(env, original);
    return fail(AJNI_RT_NATIVE_FAILURE, "Cannot preserve Java exception");
  }
  if (original) (*env)->DeleteLocalRef(env, original);
  if (raw_exception) (*env)->DeleteGlobalRef(env, raw_exception);
  raw_exception = (jthrowable)global;
  (*env)->GetJavaVM(env, &raw_exception_vm);
  return fail(AJNI_RT_JAVA_THROWN, operation ? operation : "Java exception");
}
void ajni_rt_throw_new(void *raw_env, const char *class_name, const char *message) {
  JNIEnv *env = raw_env;
  if (!env || (*env)->ExceptionCheck(env)) return;
  jclass klass = (*env)->FindClass(env, class_name);
  int thrown = (*env)->ExceptionCheck(env);
  if (!thrown && klass) (*env)->ThrowNew(env, klass, message ? message : "JNI operation failed");
  if (klass) (*env)->DeleteLocalRef(env, klass);
}
void *ajni_rt_string_from_utf8(void *raw_env, const char *text, size_t length) {
  JNIEnv *env = raw_env;
  if (!env || (!text && length) || length > INT32_MAX || length > SIZE_MAX / sizeof(jchar) - 1) {
    fail(AJNI_RT_INVALID_ARGUMENT, "Invalid UTF-8 buffer length"); return NULL;
  }
  if ((*env)->ExceptionCheck(env)) { fail(AJNI_RT_JAVA_THROWN, "UTF conversion has a pending exception"); return NULL; }
  jchar *utf16 = malloc((length + 1) * sizeof(*utf16));
  if (!utf16) { fail(AJNI_RT_NATIVE_FAILURE, "UTF-16 buffer allocation failed"); return NULL; }
  size_t out = 0;
  for (size_t i = 0; i < length;) {
    unsigned char byte = (unsigned char)text[i++];
    uint32_t point = 0xFFFD;
    if (byte < 0x80) point = byte;
    else if (byte >= 0xC2 && byte <= 0xDF && i < length && ((unsigned char)text[i] & 0xC0) == 0x80)
      point = ((byte & 0x1F) << 6) | ((unsigned char)text[i++] & 0x3F);
    else if (byte >= 0xE0 && byte <= 0xEF && length - i >= 2 &&
        ((unsigned char)text[i] & 0xC0) == 0x80 && ((unsigned char)text[i+1] & 0xC0) == 0x80 &&
        (byte != 0xE0 || (unsigned char)text[i] >= 0xA0) && (byte != 0xED || (unsigned char)text[i] < 0xA0)) {
      point = ((byte & 15) << 12) | (((unsigned char)text[i] & 63) << 6) | ((unsigned char)text[i+1] & 63); i += 2;
    } else if (byte >= 0xF0 && byte <= 0xF4 && length - i >= 3 &&
        ((unsigned char)text[i] & 0xC0) == 0x80 && ((unsigned char)text[i+1] & 0xC0) == 0x80 && ((unsigned char)text[i+2] & 0xC0) == 0x80 &&
        (byte != 0xF0 || (unsigned char)text[i] >= 0x90) && (byte != 0xF4 || (unsigned char)text[i] < 0x90)) {
      point = ((byte & 7) << 18) | (((unsigned char)text[i] & 63) << 12) | (((unsigned char)text[i+1] & 63) << 6) | ((unsigned char)text[i+2] & 63); i += 3;
    }
    if (point <= 0xFFFF) utf16[out++] = (jchar)point;
    else { point -= 0x10000; utf16[out++] = (jchar)(0xD800 | (point >> 10)); utf16[out++] = (jchar)(0xDC00 | (point & 1023)); }
  }
  jstring string = (*env)->NewString(env, utf16, (jsize)out);
  free(utf16);
  int thrown = (*env)->ExceptionCheck(env);
  if (thrown) { fail(AJNI_RT_JAVA_THROWN, "NewString(UTF-8) failed"); return NULL; }
  if (!string) fail(AJNI_RT_NATIVE_FAILURE, "NewString(UTF-8) returned null");
  return string;
}
char *ajni_rt_utf8_from_string(void *raw_env, void *raw_text, size_t *out_length) {
  JNIEnv *env = raw_env; jstring text = raw_text;
  if (!env || !text || !out_length) { fail(AJNI_RT_INVALID_ARGUMENT, "Invalid UTF-16 input"); return NULL; }
  *out_length = 0;
  if ((*env)->ExceptionCheck(env)) { fail(AJNI_RT_JAVA_THROWN, "UTF conversion has a pending exception"); return NULL; }
  jsize length = (*env)->GetStringLength(env, text);
  if ((*env)->ExceptionCheck(env)) { fail(AJNI_RT_JAVA_THROWN, "GetStringLength failed"); return NULL; }
  if (length < 0 || (size_t)length > (SIZE_MAX - 1) / 3) { fail(AJNI_RT_NATIVE_FAILURE, "UTF-8 length overflow"); return NULL; }
  const jchar *characters = (*env)->GetStringChars(env, text, NULL);
  int thrown = (*env)->ExceptionCheck(env);
  if (thrown || !characters) {
    if (characters) (*env)->ReleaseStringChars(env, text, characters);
    fail(thrown ? AJNI_RT_JAVA_THROWN : AJNI_RT_NATIVE_FAILURE, "GetStringChars failed"); return NULL;
  }
  char *output = malloc((size_t)length * 3 + 1);
  if (!output) { (*env)->ReleaseStringChars(env, text, characters); fail(AJNI_RT_NATIVE_FAILURE, "UTF-8 buffer allocation failed"); return NULL; }
  size_t out = 0;
  for (jsize i = 0; i < length; ++i) {
    uint32_t point = characters[i];
    if (point >= 0xD800 && point <= 0xDBFF) {
      if (i + 1 < length && characters[i+1] >= 0xDC00 && characters[i+1] <= 0xDFFF)
        point = 0x10000 + ((point - 0xD800) << 10) + (characters[++i] - 0xDC00);
      else point = 0xFFFD;
    } else if (point >= 0xDC00 && point <= 0xDFFF) point = 0xFFFD;
    if (point < 0x80) output[out++] = (char)point;
    else if (point < 0x800) { output[out++] = (char)(0xC0 | (point >> 6)); output[out++] = (char)(0x80 | (point & 63)); }
    else if (point < 0x10000) { output[out++] = (char)(0xE0 | (point >> 12)); output[out++] = (char)(0x80 | ((point >> 6) & 63)); output[out++] = (char)(0x80 | (point & 63)); }
    else { output[out++] = (char)(0xF0 | (point >> 18)); output[out++] = (char)(0x80 | ((point >> 12) & 63)); output[out++] = (char)(0x80 | ((point >> 6) & 63)); output[out++] = (char)(0x80 | (point & 63)); }
  }
  (*env)->ReleaseStringChars(env, text, characters);
  output[out] = 0; *out_length = out;
  return output;
}

#else
struct AjniSession { int unused; };
struct AjniObject { int unused; };
struct AjniMember { int unused; };
struct AjniValue { int unused; };
struct AjniArgs { int unused; };
static void unsupported_finalizer(void *object) { (void)object; }
static void *unsupported_object(size_t size) {
  fail(AJNI_RT_UNSUPPORTED, "JNI headers are not available on this target");
  void *object = moonbit_make_external_object(unsupported_finalizer, (uint32_t)size);
  memset(object, 0, size); return object;
}
static int unsupported(void) { return fail(AJNI_RT_UNSUPPORTED, "JNI headers are not available on this target"); }
#define UNSUP_PTR(TYPE, NAME, PARAMS) MOONBIT_FFI_EXPORT TYPE *NAME PARAMS { return unsupported_object(sizeof(TYPE)); }
#define UNSUP_INT(NAME, PARAMS) MOONBIT_FFI_EXPORT int NAME PARAMS { return unsupported(); }
#define UNSUP_U64(NAME, PARAMS) MOONBIT_FFI_EXPORT uint64_t NAME PARAMS { unsupported(); return 0; }
UNSUP_INT(ajni_rt_host_open, (void *env, void *loader))
UNSUP_INT(ajni_rt_host_enter, (void *env))
void ajni_rt_host_leave(void) { unsupported(); }
UNSUP_INT(ajni_rt_host_close, (void *env))
void ajni_rt_host_throw(void *env) { (void)env; unsupported(); }
UNSUP_INT(ajni_rt_register_companion, (void *env))
UNSUP_PTR(AjniSession, ajni_rt_current, (void))
UNSUP_INT(ajni_rt_session_close, (AjniSession *s))
UNSUP_PTR(AjniObject, ajni_rt_take_exception, (void))
MOONBIT_FFI_EXPORT AjniObject *ajni_rt_null_object(void) {
  AjniObject *object = moonbit_make_external_object(unsupported_finalizer, sizeof(*object));
  object->unused = 0; rt_status = 0; rt_message[0] = 0; return object;
}
UNSUP_INT(ajni_rt_object_close, (AjniObject *object))
UNSUP_INT(ajni_rt_object_same, (AjniObject *a, AjniObject *b))
UNSUP_INT(ajni_rt_object_is_null, (AjniObject *object))
UNSUP_PTR(AjniSession, ajni_rt_object_session, (AjniObject *object))
UNSUP_PTR(AjniObject, ajni_rt_find_class, (AjniSession *s, moonbit_string_t n))
UNSUP_PTR(AjniObject, ajni_rt_new_string, (AjniSession *s, moonbit_string_t n))
MOONBIT_FFI_EXPORT moonbit_string_t ajni_rt_read_string(AjniObject *object) { (void)object; unsupported(); return moonbit_make_string_raw(0); }
UNSUP_PTR(AjniMember, ajni_rt_lookup, (AjniObject *c, moonbit_string_t n, moonbit_string_t d, int k))
UNSUP_PTR(AjniValue, ajni_rt_member_call, (AjniMember *m, AjniObject *o, AjniArgs *a))
UNSUP_PTR(AjniValue, ajni_rt_field_get, (AjniMember *m, AjniObject *o))
UNSUP_INT(ajni_rt_field_set, (AjniMember *m, AjniObject *o, AjniValue *v))
UNSUP_PTR(AjniValue, ajni_rt_value_int, (int t, int64_t n))
UNSUP_PTR(AjniValue, ajni_rt_value_float, (int t, double n))
UNSUP_PTR(AjniValue, ajni_rt_value_object, (AjniObject *o))
UNSUP_PTR(AjniValue, ajni_rt_value_void, (void))
UNSUP_INT(ajni_rt_value_tag, (AjniValue *v))
MOONBIT_FFI_EXPORT int64_t ajni_rt_value_integer(AjniValue *v) { (void)v; unsupported(); return 0; }
MOONBIT_FFI_EXPORT double ajni_rt_value_number(AjniValue *v) { (void)v; unsupported(); return 0; }
UNSUP_PTR(AjniObject, ajni_rt_value_get_object, (AjniValue *v))
UNSUP_PTR(AjniArgs, ajni_rt_args_new, (void))
UNSUP_INT(ajni_rt_args_push, (AjniArgs *a, AjniValue *v))
UNSUP_INT(ajni_rt_args_length, (AjniArgs *a))
UNSUP_PTR(AjniValue, ajni_rt_args_get, (AjniArgs *a, int i))
UNSUP_PTR(AjniObject, ajni_rt_array_new, (AjniSession *s, int t, int n))
UNSUP_INT(ajni_rt_array_length, (AjniObject *o))
UNSUP_PTR(AjniValue, ajni_rt_array_get, (AjniObject *o, int i))
UNSUP_INT(ajni_rt_array_set, (AjniObject *o, int i, AjniValue *v))
UNSUP_PTR(AjniObject, ajni_rt_object_array_new, (AjniObject *c, int n, AjniObject *v))
UNSUP_PTR(AjniArgs, ajni_rt_array_read, (AjniObject *o, int s, int n))
UNSUP_INT(ajni_rt_array_write, (AjniObject *o, int s, AjniArgs *a))
MOONBIT_FFI_EXPORT AjniObject *ajni_rt_callback(AjniSession *s, moonbit_string_t d, AjniTrampoline f, void *closure) {
  (void)s; (void)d; (void)f; if (closure) moonbit_decref(closure); return unsupported_object(sizeof(AjniObject));
}
UNSUP_INT(ajni_rt_callback_close, (AjniObject *o))
UNSUP_PTR(AjniValue, ajni_rt_callback_error, (int s, AjniObject *o, moonbit_string_t m))
UNSUP_PTR(AjniObject, ajni_rt_import_object, (AjniSession *s, uint64_t e, uint64_t o, int c))
UNSUP_U64(ajni_rt_export_local, (AjniObject *o, uint64_t e))
UNSUP_U64(ajni_rt_raw_vm, (AjniSession *s))
UNSUP_U64(ajni_rt_raw_env, (AjniSession *s))
UNSUP_INT(ajni_rt_register_native, (AjniObject *o, moonbit_string_t n, moonbit_string_t d, uint64_t f))
int ajni_rt_env_acquire(AjniEnvLease *lease) { if (lease) memset(lease, 0, sizeof(*lease)); return unsupported(); }
void ajni_rt_env_release(AjniEnvLease *lease) { (void)lease; unsupported(); }
int ajni_rt_capture_exception(void *env, const char *operation) { (void)env; (void)operation; return unsupported(); }
void ajni_rt_throw_new(void *env, const char *name, const char *message) { (void)env; (void)name; (void)message; unsupported(); }
void *ajni_rt_string_from_utf8(void *env, const char *text, size_t length) { (void)env; (void)text; (void)length; unsupported(); return NULL; }
char *ajni_rt_utf8_from_string(void *env, void *text, size_t *length) { (void)env; (void)text; if (length) *length = 0; unsupported(); return NULL; }
#undef UNSUP_PTR
#undef UNSUP_INT
#undef UNSUP_U64
#endif
