#ifndef AJNI_RUNTIME_H
#define AJNI_RUNTIME_H

#include <stddef.h>
#include <stdint.h>
#if !defined(AJNI_STANDALONE_JNI)
#include <moonbit.h>
#else
typedef uint16_t *moonbit_string_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AjniSession AjniSession;
typedef struct AjniObject AjniObject;
typedef struct AjniMember AjniMember;
typedef struct AjniValue AjniValue;
typedef struct AjniArgs AjniArgs;
typedef AjniValue *(*AjniTrampoline)(void *closure, AjniArgs *args);

enum {
  AJNI_RT_OK = 0, AJNI_RT_UNSUPPORTED = 1, AJNI_RT_NOT_INITIALIZED = 2,
  AJNI_RT_CLOSED = 3, AJNI_RT_WRONG_THREAD = 4, AJNI_RT_BUSY = 5,
  AJNI_RT_INVALID_ARGUMENT = 6, AJNI_RT_JAVA_THROWN = 7,
  AJNI_RT_NATIVE_FAILURE = 8
};

/* Host entry points never allocate or retain managed values before owner checks. */
int ajni_rt_host_open(void *jni_env, void *class_loader);
int ajni_rt_host_enter(void *jni_env);
void ajni_rt_host_leave(void);
int ajni_rt_host_close(void *jni_env);
void ajni_rt_host_throw(void *jni_env);
int ajni_rt_register_companion(void *jni_env);

typedef struct AjniEnvLease { void *vm; void *env; int attached; } AjniEnvLease;
int ajni_rt_env_acquire(AjniEnvLease *);
void ajni_rt_env_release(AjniEnvLease *);
int ajni_rt_capture_exception(void *, const char *);
void ajni_rt_throw_new(void *, const char *, const char *);
void *ajni_rt_string_from_utf8(void *, const char *, size_t);
char *ajni_rt_utf8_from_string(void *, void *, size_t *);
/* All managed arguments are borrowed, except callback's owned closure.
 * Managed pointer results are always valid external objects, including errors. */
AjniSession *ajni_rt_current(void);
int ajni_rt_session_close(AjniSession *);
int ajni_rt_last_status(void);
AjniObject *ajni_rt_take_exception(void);
moonbit_string_t ajni_rt_error_message(void);
AjniObject *ajni_rt_null_object(void);
int ajni_rt_object_close(AjniObject *);
int ajni_rt_object_same(AjniObject *, AjniObject *);
int ajni_rt_object_is_null(AjniObject *);
AjniSession *ajni_rt_object_session(AjniObject *);
AjniObject *ajni_rt_find_class(AjniSession *, moonbit_string_t);
AjniObject *ajni_rt_new_string(AjniSession *, moonbit_string_t);
moonbit_string_t ajni_rt_read_string(AjniObject *);
AjniMember *ajni_rt_lookup(AjniObject *, moonbit_string_t, moonbit_string_t, int);
AjniValue *ajni_rt_member_call(AjniMember *, AjniObject *, AjniArgs *);
AjniValue *ajni_rt_field_get(AjniMember *, AjniObject *);
int ajni_rt_field_set(AjniMember *, AjniObject *, AjniValue *);
AjniValue *ajni_rt_value_int(int, int64_t);
AjniValue *ajni_rt_value_float(int, double);
AjniValue *ajni_rt_value_object(AjniObject *);
AjniValue *ajni_rt_value_void(void);
int ajni_rt_value_tag(AjniValue *);
int64_t ajni_rt_value_integer(AjniValue *);
double ajni_rt_value_number(AjniValue *);
AjniObject *ajni_rt_value_get_object(AjniValue *);
AjniArgs *ajni_rt_args_new(void);
int ajni_rt_args_push(AjniArgs *, AjniValue *);
int ajni_rt_args_length(AjniArgs *);
AjniValue *ajni_rt_args_get(AjniArgs *, int);
AjniObject *ajni_rt_array_new(AjniSession *, int, int);
int ajni_rt_array_length(AjniObject *);
AjniValue *ajni_rt_array_get(AjniObject *, int);
int ajni_rt_array_set(AjniObject *, int, AjniValue *);
AjniObject *ajni_rt_object_array_new(AjniObject *, int, AjniObject *);
AjniArgs *ajni_rt_array_read(AjniObject *, int, int);
int ajni_rt_array_write(AjniObject *, int, AjniArgs *);
AjniObject *ajni_rt_callback(AjniSession *, moonbit_string_t, AjniTrampoline, void *);
int ajni_rt_callback_close(AjniObject *);
AjniValue *ajni_rt_callback_error(int, AjniObject *, moonbit_string_t);

/* Unsafe host-facing raw values are caller-validated JNI pointers. */
AjniObject *ajni_rt_import_object(AjniSession *, uint64_t, uint64_t, int);
uint64_t ajni_rt_export_local(AjniObject *, uint64_t);
uint64_t ajni_rt_raw_vm(AjniSession *);
uint64_t ajni_rt_raw_env(AjniSession *);
int ajni_rt_register_native(AjniObject *, moonbit_string_t, moonbit_string_t, uint64_t);

#ifdef __cplusplus
}
#endif
#endif
