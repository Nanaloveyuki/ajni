#ifndef AJNI_RUNTIME_H
#define AJNI_RUNTIME_H

#include <stddef.h>
#include <stdint.h>
#if !defined(AJNI_STANDALONE_JNI)
#include <moonbit.h>
#else
typedef uint16_t *moonbit_string_t;
#endif

#if defined(AJNI_STANDALONE_JNI)
#define AJNI_RUNTIME_API
#else
#define AJNI_RUNTIME_API MOONBIT_FFI_EXPORT
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
AJNI_RUNTIME_API int ajni_rt_host_open(void *jni_env, void *class_loader);
AJNI_RUNTIME_API int ajni_rt_host_enter(void *jni_env);
void ajni_rt_host_leave(void);
AJNI_RUNTIME_API int ajni_rt_host_close(void *jni_env);
void ajni_rt_host_throw(void *jni_env);
AJNI_RUNTIME_API int ajni_rt_register_companion(void *jni_env);

typedef struct AjniEnvLease { void *vm; void *env; int attached; } AjniEnvLease;
int ajni_rt_env_acquire(AjniEnvLease *);
void ajni_rt_env_release(AjniEnvLease *);
int ajni_rt_capture_exception(void *, const char *);
void ajni_rt_throw_new(void *, const char *, const char *);
void *ajni_rt_string_from_utf8(void *, const char *, size_t);
char *ajni_rt_utf8_from_string(void *, void *, size_t *);
/* All managed arguments are borrowed, except callback's owned closure.
 * Managed pointer results are always valid external objects, including errors. */
AJNI_RUNTIME_API AjniSession *ajni_rt_current(void);
AJNI_RUNTIME_API int ajni_rt_session_close(AjniSession *);
AJNI_RUNTIME_API int ajni_rt_last_status(void);
AJNI_RUNTIME_API AjniObject *ajni_rt_take_exception(void);
AJNI_RUNTIME_API moonbit_string_t ajni_rt_error_message(void);
AJNI_RUNTIME_API AjniObject *ajni_rt_null_object(void);
AJNI_RUNTIME_API int ajni_rt_object_close(AjniObject *);
AJNI_RUNTIME_API int ajni_rt_object_same(AjniObject *, AjniObject *);
AJNI_RUNTIME_API int ajni_rt_object_is_null(AjniObject *);
AJNI_RUNTIME_API AjniSession *ajni_rt_object_session(AjniObject *);
AJNI_RUNTIME_API AjniObject *ajni_rt_find_class(AjniSession *, moonbit_string_t);
AJNI_RUNTIME_API AjniObject *ajni_rt_new_string(AjniSession *, moonbit_string_t);
AJNI_RUNTIME_API moonbit_string_t ajni_rt_read_string(AjniObject *);
AJNI_RUNTIME_API AjniMember *ajni_rt_lookup(AjniObject *, moonbit_string_t, moonbit_string_t, int);
AJNI_RUNTIME_API AjniValue *ajni_rt_member_call(AjniMember *, AjniObject *, AjniArgs *);
AJNI_RUNTIME_API AjniValue *ajni_rt_field_get(AjniMember *, AjniObject *);
AJNI_RUNTIME_API int ajni_rt_field_set(AjniMember *, AjniObject *, AjniValue *);
AJNI_RUNTIME_API AjniValue *ajni_rt_value_int(int, int64_t);
AJNI_RUNTIME_API AjniValue *ajni_rt_value_float(int, double);
AJNI_RUNTIME_API AjniValue *ajni_rt_value_object(AjniObject *);
AJNI_RUNTIME_API AjniValue *ajni_rt_value_void(void);
AJNI_RUNTIME_API int ajni_rt_value_tag(AjniValue *);
AJNI_RUNTIME_API int64_t ajni_rt_value_integer(AjniValue *);
AJNI_RUNTIME_API double ajni_rt_value_number(AjniValue *);
AJNI_RUNTIME_API AjniObject *ajni_rt_value_get_object(AjniValue *);
AJNI_RUNTIME_API AjniArgs *ajni_rt_args_new(void);
AJNI_RUNTIME_API int ajni_rt_args_push(AjniArgs *, AjniValue *);
AJNI_RUNTIME_API int ajni_rt_args_length(AjniArgs *);
AJNI_RUNTIME_API AjniValue *ajni_rt_args_get(AjniArgs *, int);
AJNI_RUNTIME_API AjniObject *ajni_rt_array_new(AjniSession *, int, int);
AJNI_RUNTIME_API int ajni_rt_array_length(AjniObject *);
AJNI_RUNTIME_API AjniValue *ajni_rt_array_get(AjniObject *, int);
AJNI_RUNTIME_API int ajni_rt_array_set(AjniObject *, int, AjniValue *);
AJNI_RUNTIME_API AjniObject *ajni_rt_object_array_new(AjniObject *, int, AjniObject *);
AJNI_RUNTIME_API AjniArgs *ajni_rt_array_read(AjniObject *, int, int);
AJNI_RUNTIME_API int ajni_rt_array_write(AjniObject *, int, AjniArgs *);
AJNI_RUNTIME_API AjniObject *ajni_rt_callback(AjniSession *, moonbit_string_t, AjniTrampoline, void *);
AJNI_RUNTIME_API int ajni_rt_callback_close(AjniObject *);
AJNI_RUNTIME_API AjniValue *ajni_rt_callback_error(int, AjniObject *, moonbit_string_t);

/* Unsafe host-facing raw values are caller-validated JNI pointers. */
AJNI_RUNTIME_API AjniObject *ajni_rt_import_object(AjniSession *, uint64_t, uint64_t, int);
AJNI_RUNTIME_API uint64_t ajni_rt_export_local(AjniObject *, uint64_t);
AJNI_RUNTIME_API uint64_t ajni_rt_raw_vm(AjniSession *);
AJNI_RUNTIME_API uint64_t ajni_rt_raw_env(AjniSession *);
AJNI_RUNTIME_API int ajni_rt_register_native(AjniObject *, moonbit_string_t, moonbit_string_t, uint64_t);

#ifdef __cplusplus
}
#endif

#undef AJNI_RUNTIME_API
#endif
