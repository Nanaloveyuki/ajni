#include <napi/native_api.h>

#include "moonbit_probe.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr int32_t kCallbackError = std::numeric_limits<int32_t>::min();
constexpr size_t kQueueCapacity = 32;

std::atomic<int64_t> g_live_sessions{0};
std::atomic<int64_t> g_live_payloads{0};
std::atomic<int64_t> g_finalized_sessions{0};
std::atomic<int64_t> g_cleanup_hooks{0};
std::atomic<bool> g_owner_initialized{false};
std::atomic<bool> g_owner_terminated{false};
std::mutex g_owner_mutex;
napi_env g_owner_env = nullptr;
std::thread::id g_owner_thread;

struct EnvState;
struct Session;

struct Payload {
  Session *session;
  int32_t sequence;
  bool holds_slot = false;
};

struct Session {
  explicit Session(napi_env e, uint64_t t, std::thread::id owner)
      : env(e), token(t), owner_thread(owner) {
    g_live_sessions.fetch_add(1, std::memory_order_relaxed);
  }

  std::atomic<uint32_t> refs{1}; // registry reference
  napi_env env;
  const uint64_t token;
  const std::thread::id owner_thread;

  std::mutex mutex;
  std::condition_variable condition;
  napi_ref callback_ref = nullptr;
  napi_threadsafe_function tsfn = nullptr;
  bool tsfn_finalized = false;
  bool closed = false;
  bool stop = false;
  bool producer_active = false;
  bool worker_running = false;
  bool producer_parked = false;
  uint32_t managed_frames = 0;
  std::thread producer;

  int32_t total = 0;
  int32_t managed_entries = 0;
  int64_t accepted = 0;
  int64_t delivered = 0;
  int64_t discarded = 0;
  int64_t allocated = 0;
  int64_t freed = 0;
  int64_t queue_full = 0;
  size_t pending_payloads = 0;
  int64_t callback_errors = 0;
  bool wrong_thread_rejected = false;
  bool finalized = false;
  std::string callback_thread;
  bool counted_live = true;

  void retire_if_drained() {
    std::lock_guard<std::mutex> lock(mutex);
    if (counted_live && closed && tsfn_finalized && !producer_active && allocated == freed) {
      counted_live = false;
      g_live_sessions.fetch_sub(1, std::memory_order_relaxed);
      g_finalized_sessions.fetch_add(1, std::memory_order_relaxed);
    }
  }

  void retain() { refs.fetch_add(1, std::memory_order_relaxed); }

  void release() {
    if (refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      if (counted_live) {
        g_live_sessions.fetch_sub(1, std::memory_order_relaxed);
        g_finalized_sessions.fetch_add(1, std::memory_order_relaxed);
      }
      delete this;
    }
  }
};

struct EnvState {
  explicit EnvState(napi_env e) : env(e) {}
  napi_env env;
  std::mutex mutex;
  std::unordered_map<uint64_t, Session *> sessions;
  bool cleaning = false;
};

std::string thread_name(std::thread::id id) {
  std::ostringstream out;
  out << "thread-" << std::hash<std::thread::id>{}(id);
  return out.str();
}

bool is_owner_thread() {
  std::lock_guard<std::mutex> lock(g_owner_mutex);
  return g_owner_initialized.load(std::memory_order_acquire) &&
         std::this_thread::get_id() == g_owner_thread;
}

void release_payload(Payload *payload) {
  if (payload == nullptr) return;
  Session *session = payload->session;
  {
    std::lock_guard<std::mutex> lock(session->mutex);
    session->freed += 1;
    if (payload->holds_slot) session->pending_payloads -= 1;
  }
  g_live_payloads.fetch_sub(1, std::memory_order_relaxed);
  session->retire_if_drained();
  session->release();
  delete payload;
}

bool pending_exception(napi_env env) {
  bool pending = false;
  return napi_is_exception_pending(env, &pending) == napi_ok && pending;
}

void throw_error(napi_env env, const char *code, const char *message) {
  if (!pending_exception(env)) (void)napi_throw_error(env, code, message);
}

bool get_arg_count(napi_env env, napi_callback_info info, size_t required,
                   napi_value *argv, void **data = nullptr) {
  size_t argc = required;
  napi_status status = napi_get_cb_info(env, info, &argc, argv, nullptr, data);
  if (status != napi_ok || argc < required) {
    throw_error(env, "TypeError", "invalid argument count");
    return false;
  }
  return true;
}

bool get_safe_token(napi_env env, napi_value value, uint64_t *result) {
  napi_valuetype type;
  double number = 0;
  if (napi_typeof(env, value, &type) != napi_ok || type != napi_number ||
      napi_get_value_double(env, value, &number) != napi_ok ||
      !std::isfinite(number) || number < 1 || number > 9007199254740991.0 ||
      std::floor(number) != number) {
    throw_error(env, "TypeError", "token must be a positive safe integer");
    return false;
  }
  *result = static_cast<uint64_t>(number);
  return true;
}

bool get_int32(napi_env env, napi_value value, int32_t *result,
               const char *message) {
  napi_valuetype type;
  double number = 0;
  if (napi_typeof(env, value, &type) != napi_ok || type != napi_number ||
      napi_get_value_double(env, value, &number) != napi_ok ||
      !std::isfinite(number) || std::floor(number) != number ||
      number < std::numeric_limits<int32_t>::min() ||
      number > std::numeric_limits<int32_t>::max()) {
    throw_error(env, "TypeError", message);
    return false;
  }
  *result = static_cast<int32_t>(number);
  return true;
}

bool get_array_buffer(napi_env env, napi_value value, const uint8_t **data,
                      size_t *length) {
  bool is_array_buffer = false;
  void *raw = nullptr;
  if (napi_is_arraybuffer(env, value, &is_array_buffer) != napi_ok ||
      !is_array_buffer || napi_get_arraybuffer_info(env, value, &raw, length) != napi_ok) {
    throw_error(env, "TypeError", "bytes must be an ArrayBuffer");
    return false;
  }
  *data = static_cast<const uint8_t *>(raw);
  return true;
}

bool make_moonbit_string(napi_env env, napi_value value, moonbit_string_t *out) {
  size_t length = 0;
  if (napi_get_value_string_utf16(env, value, nullptr, 0, &length) != napi_ok ||
      length > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
    throw_error(env, "TypeError", "text is too long");
    return false;
  }
  // NAPI's capacity includes its trailing zero; MoonBit stores only code units.
  std::vector<char16_t> buffer(length + 1);
  size_t copied = 0;
  if (napi_get_value_string_utf16(env, value, buffer.data(), buffer.size(), &copied) != napi_ok || copied != length) {
    throw_error(env, "TypeError", "invalid text");
    return false;
  }
  moonbit_string_t text = moonbit_make_string_raw(static_cast<int32_t>(length));
  if (length != 0) std::memcpy(text, buffer.data(), length * sizeof(char16_t));
  *out = text;
  return true;
}

bool make_moonbit_bytes(napi_env env, napi_value value, moonbit_bytes_t *out) {
  const uint8_t *data = nullptr;
  size_t length = 0;
  if (!get_array_buffer(env, value, &data, &length) ||
      length > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
    throw_error(env, "TypeError", "bytes are too long");
    return false;
  }
  moonbit_bytes_t bytes = moonbit_make_bytes_raw(static_cast<int32_t>(length));
  if (bytes == nullptr) {
    throw_error(env, "Error", "unable to allocate managed bytes");
    return false;
  }
  if (length != 0) std::memcpy(bytes, data, length);
  *out = bytes;
  return true;
}

bool make_js_string(napi_env env, moonbit_string_t text, napi_value *result) {
  return napi_create_string_utf16(env, reinterpret_cast<const char16_t *>(text),
                                  static_cast<size_t>(Moonbit_array_length(text)), result) == napi_ok;
}

bool make_js_bytes(napi_env env, moonbit_bytes_t bytes, napi_value *result) {
  size_t length = static_cast<size_t>(Moonbit_array_length(bytes));
  void *data = nullptr;
  if (napi_create_arraybuffer(env, length, &data, result) != napi_ok) return false;
  if (length != 0) std::memcpy(data, bytes, length);
  return true;
}

Session *lookup_session(EnvState *state, uint64_t token) {
  if (state == nullptr) return nullptr;
  std::lock_guard<std::mutex> lock(state->mutex);
  auto found = state->sessions.find(token);
  return found == state->sessions.end() ? nullptr : found->second;
}

void update_managed_snapshot(Session *session) {
  const int32_t total = ajni_ohos_total();
  const int32_t entries = ajni_ohos_entries();
  std::lock_guard<std::mutex> lock(session->mutex);
  session->total = total;
  session->managed_entries = entries;
}

bool is_session_owner(Session *session) {
  return is_owner_thread() && std::this_thread::get_id() == session->owner_thread;
}

bool owner_session(napi_env env, Session *session) {
  if (session == nullptr) {
    throw_error(env, "RangeError", "unknown token");
    return false;
  }
  if (!is_session_owner(session)) {
    throw_error(env, "WrongThread", "managed entry requires the owner thread");
    return false;
  }
  if (g_owner_terminated.load(std::memory_order_acquire)) {
    throw_error(env, "Closed", "managed owner has terminated");
    return false;
  }
  return true;
}

void mark_wrong_thread(Session *session) {
  if (!is_session_owner(session)) {
    std::lock_guard<std::mutex> lock(session->mutex);
    session->wrong_thread_rejected = true;
  }
}

bool acquire_producer(Session *session, napi_threadsafe_function *tsfn) {
  std::lock_guard<std::mutex> lock(session->mutex);
  if (session->closed || session->stop || session->producer_active || session->tsfn == nullptr) return false;
  napi_threadsafe_function handle = session->tsfn;
  if (napi_acquire_threadsafe_function(handle) != napi_ok) return false;
  session->producer_active = true;
  session->worker_running = true;
  *tsfn = handle;
  return true;
}

void finish_producer(Session *session, napi_threadsafe_function tsfn) {
  (void)napi_release_threadsafe_function(tsfn, napi_tsfn_release);
  {
    std::lock_guard<std::mutex> lock(session->mutex);
    session->producer_active = false;
    session->worker_running = false;
    session->producer_parked = false;
    session->condition.notify_all();
  }
}

bool enqueue(Session *session, napi_threadsafe_function tsfn, int32_t sequence) {
  Payload *payload = new (std::nothrow) Payload{session, sequence};
  if (payload == nullptr) return false;
  session->retain();
  bool full = false;
  {
    std::lock_guard<std::mutex> lock(session->mutex);
    session->allocated += 1;
    full = session->pending_payloads >= kQueueCapacity;
    if (full) {
      session->queue_full += 1;
    } else {
      session->pending_payloads += 1;
      payload->holds_slot = true;
    }
  }
  g_live_payloads.fetch_add(1, std::memory_order_relaxed);
  if (full) {
    release_payload(payload);
    return false;
  }
  napi_status status = napi_call_threadsafe_function(tsfn, payload, napi_tsfn_nonblocking);
  if (status == napi_ok) {
    std::lock_guard<std::mutex> lock(session->mutex);
    session->accepted += 1;
    return true;
  }
  if (status == napi_queue_full) {
    std::lock_guard<std::mutex> lock(session->mutex);
    session->queue_full += 1;
  }
  release_payload(payload);
  return false;
}

void worker_body(Session *session, napi_threadsafe_function tsfn, int32_t count,
                 bool park_after_first) {
  mark_wrong_thread(session);
  for (int32_t sequence = 1; sequence <= count; ++sequence) {
    {
      std::lock_guard<std::mutex> lock(session->mutex);
      if (session->stop || session->closed) break;
    }
    (void)enqueue(session, tsfn, sequence);
    if (park_after_first && sequence == 1) {
      std::unique_lock<std::mutex> lock(session->mutex);
      session->producer_parked = true;
      session->condition.notify_all();
      session->condition.wait(lock, [session] { return session->stop || session->closed; });
      break;
    }
  }
  finish_producer(session, tsfn);
}

void call_js(napi_env env, napi_value, void *context, void *data) {
  (void)context;
  Payload *payload = static_cast<Payload *>(data);
  if (payload == nullptr) return;
  Session *session = payload->session;
  if (env == nullptr) {
    {
      std::lock_guard<std::mutex> lock(session->mutex);
      session->discarded += 1;
    }
    release_payload(payload);
    return;
  }
  if (!is_session_owner(session) || g_owner_terminated.load(std::memory_order_acquire)) {
    {
      std::lock_guard<std::mutex> lock(session->mutex);
      session->wrong_thread_rejected = true;
      session->discarded += 1;
    }
    release_payload(payload);
    return;
  }
  bool discard = false;
  {
    std::lock_guard<std::mutex> lock(session->mutex);
    discard = session->closed || session->stop || session->callback_ref == nullptr;
    if (discard) session->discarded += 1;
  }
  if (discard) {
    release_payload(payload);
    return;
  }

  napi_value callback = nullptr;
  if (napi_get_reference_value(env, session->callback_ref, &callback) != napi_ok || callback == nullptr) {
    {
      std::lock_guard<std::mutex> lock(session->mutex);
      session->callback_errors += 1;
      session->discarded += 1;
    }
    release_payload(payload);
    return;
  }

  int32_t total = 0;
  {
    std::lock_guard<std::mutex> lock(session->mutex);
    session->managed_frames += 1;
    session->callback_thread = thread_name(std::this_thread::get_id());
  }
  total = ajni_ohos_worker(payload->sequence);
  update_managed_snapshot(session);
  napi_value kind = nullptr;
  napi_value value = nullptr;
  napi_value text = nullptr;
  napi_value bytes = nullptr;
  void *raw_bytes = nullptr;
  bool ok = napi_create_int32(env, 1, &kind) == napi_ok &&
            napi_create_int32(env, total, &value) == napi_ok &&
            napi_create_string_utf16(env, u"worker", 6, &text) == napi_ok &&
            napi_create_arraybuffer(env, 1, &raw_bytes, &bytes) == napi_ok;
  if (ok && raw_bytes != nullptr) *static_cast<uint8_t *>(raw_bytes) = static_cast<uint8_t>(payload->sequence);
  napi_value argv[4] = {kind, value, text, bytes};
  napi_value ignored = nullptr;
  if (ok) ok = napi_call_function(env, callback, callback, 4, argv, &ignored) == napi_ok;
  bool threw = pending_exception(env);
  if (threw) (void)napi_get_and_clear_last_exception(env, &ignored);
  {
    std::lock_guard<std::mutex> lock(session->mutex);
    session->managed_frames -= 1;
    if (ok && !threw) session->delivered += 1;
    else session->callback_errors += 1;
  }
  release_payload(payload);
}


void tsfn_finalizer(napi_env, void *data, void *) {
  Session *session = static_cast<Session *>(data);
  if (session == nullptr) return;
  {
    std::lock_guard<std::mutex> lock(session->mutex);
    session->tsfn_finalized = true;
    session->finalized = true;
  }
  session->retire_if_drained();
  session->release();
}

napi_value snapshot_value(napi_env env, Session *session) {
  napi_value object = nullptr;
  if (napi_create_object(env, &object) != napi_ok) return nullptr;
  std::lock_guard<std::mutex> lock(session->mutex);
  napi_value value = nullptr;
#define SET_I64(name, number) \
  do { if (napi_create_int64(env, (number), &value) == napi_ok) (void)napi_set_named_property(env, object, (name), value); } while (0)
#define SET_BOOL(name, flag) \
  do { if (napi_get_boolean(env, (flag), &value) == napi_ok) (void)napi_set_named_property(env, object, (name), value); } while (0)
  SET_I64("token", static_cast<int64_t>(session->token));
  SET_I64("total", session->total);
  SET_I64("managedEntries", session->managed_entries);
  SET_I64("accepted", session->accepted);
  SET_I64("delivered", session->delivered);
  SET_I64("discarded", session->discarded);
  SET_I64("allocated", session->allocated);
  SET_I64("freed", session->freed);
  SET_I64("queueFull", session->queue_full);
  SET_BOOL("workerRunning", session->worker_running);
  SET_BOOL("closed", session->closed);
  if (napi_create_string_utf8(env, thread_name(session->owner_thread).c_str(), NAPI_AUTO_LENGTH, &value) == napi_ok)
    (void)napi_set_named_property(env, object, "ownerThread", value);
  if (napi_create_string_utf8(env, session->callback_thread.c_str(), NAPI_AUTO_LENGTH, &value) == napi_ok)
    (void)napi_set_named_property(env, object, "callbackThread", value);
  SET_BOOL("wrongThreadRejected", session->wrong_thread_rejected);
  SET_I64("callbackErrors", session->callback_errors);
  SET_BOOL("finalized", session->finalized);
#undef SET_I64
#undef SET_BOOL
  return object;
}

void stop_session(Session *session, bool abort_tsfn) {
  napi_threadsafe_function tsfn = nullptr;
  std::thread producer;
  napi_ref callback_ref = nullptr;
  {
    std::lock_guard<std::mutex> lock(session->mutex);
    session->stop = true;
    session->closed = true;
    session->condition.notify_all();
    if (session->producer.joinable()) producer = std::move(session->producer);
    tsfn = session->tsfn;
    if (abort_tsfn) session->tsfn = nullptr;
    callback_ref = session->callback_ref;
    session->callback_ref = nullptr;
  }
  if (producer.joinable()) producer.join();
  if (callback_ref != nullptr) (void)napi_delete_reference(session->env, callback_ref);
  if (tsfn != nullptr && abort_tsfn) {
    (void)napi_release_threadsafe_function(tsfn, napi_tsfn_abort);
  }
}

void cleanup_env(void *arg) {
  EnvState *state = static_cast<EnvState *>(arg);
  if (state == nullptr) return;
  std::vector<Session *> sessions;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->cleaning = true;
    for (auto &entry : state->sessions) sessions.push_back(entry.second);
    state->sessions.clear();
  }
  for (Session *session : sessions) stop_session(session, true);
  for (Session *session : sessions) session->release();
  {
    std::lock_guard<std::mutex> lock(g_owner_mutex);
    if (g_owner_env == state->env) g_owner_terminated.store(true, std::memory_order_release);
  }
  g_cleanup_hooks.fetch_add(1, std::memory_order_relaxed);
  delete state;
}

napi_value open_callback(napi_env env, napi_callback_info info) {
  napi_value argv[1] = {nullptr};
  if (!get_arg_count(env, info, 1, argv)) return nullptr;
  napi_valuetype type;
  if (napi_typeof(env, argv[0], &type) != napi_ok || type != napi_function) {
    throw_error(env, "TypeError", "callback must be a function");
    return nullptr;
  }
  {
    std::lock_guard<std::mutex> lock(g_owner_mutex);
    if (g_owner_initialized.load(std::memory_order_acquire) &&
        (std::this_thread::get_id() != g_owner_thread || env != g_owner_env)) {
      throw_error(env, "WrongThread", "only the owner thread may open a session");
      return nullptr;
    }
    if (g_owner_terminated.load(std::memory_order_acquire)) {
      throw_error(env, "Closed", "managed owner has terminated");
      return nullptr;
    }
    if (!g_owner_initialized.load(std::memory_order_relaxed)) {
      moonbit_runtime_init(0, nullptr);
      moonbit_init();
      g_owner_thread = std::this_thread::get_id();
      g_owner_env = env;
      g_owner_initialized.store(true, std::memory_order_release);
    }
  }
  EnvState *state = nullptr;
  if (napi_get_instance_data(env, reinterpret_cast<void **>(&state)) != napi_ok || state == nullptr) {
    throw_error(env, "Error", "module environment is unavailable");
    return nullptr;
  }
  napi_ref callback_ref = nullptr;
  if (napi_create_reference(env, argv[0], 1, &callback_ref) != napi_ok) {
    throw_error(env, "Error", "unable to retain callback");
    return nullptr;
  }
  uint64_t token = 1;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    for (const auto &entry : state->sessions) token = std::max(token, entry.first + 1);
    for (const auto &entry : state->sessions) {
      std::lock_guard<std::mutex> session_lock(entry.second->mutex);
      if (!entry.second->closed) {
        (void)napi_delete_reference(env, callback_ref);
        throw_error(env, "Busy", "a session is already open");
        return nullptr;
      }
    }
  }
  Session *session = new (std::nothrow) Session(env, token, std::this_thread::get_id());
  if (session == nullptr) {
    (void)napi_delete_reference(env, callback_ref);
    throw_error(env, "Error", "unable to allocate session");
    return nullptr;
  }
  session->callback_ref = callback_ref;
  napi_value resource_name = nullptr;
  napi_threadsafe_function tsfn = nullptr;
  if (napi_create_string_utf8(env, "ajni_ohos_probe", NAPI_AUTO_LENGTH, &resource_name) != napi_ok ||
      napi_create_threadsafe_function(env, argv[0], nullptr, resource_name, kQueueCapacity, 1,
                                      session, tsfn_finalizer, session, call_js, &tsfn) != napi_ok) {
    (void)napi_delete_reference(env, callback_ref);
    session->release();
    throw_error(env, "Error", "unable to create callback queue");
    return nullptr;
  }
  session->retain(); // TSFN finalizer reference
  session->tsfn = tsfn;
  ajni_ohos_reset();
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->sessions.emplace(token, session);
  }
  napi_value result = nullptr;
  if (napi_create_int64(env, static_cast<int64_t>(token), &result) != napi_ok) {
    stop_session(session, true);
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      state->sessions.erase(token);
    }
    session->release();
    throw_error(env, "Error", "unable to create token");
    return nullptr;
  }
  return result;
}

napi_value round_trip_callback(napi_env env, napi_callback_info info) {
  napi_value argv[4] = {nullptr};
  void *raw_state = nullptr;
  if (!get_arg_count(env, info, 4, argv, &raw_state)) return nullptr;
  EnvState *state = nullptr;
  (void)napi_get_instance_data(env, reinterpret_cast<void **>(&state));
  int32_t delta = 0;
  uint64_t token = 0;
  if (!get_safe_token(env, argv[0], &token) || !get_int32(env, argv[1], &delta, "delta must be int32")) return nullptr;
  Session *session = lookup_session(state, token);
  if (!owner_session(env, session)) return nullptr;
  napi_valuetype text_type;
  if (napi_typeof(env, argv[2], &text_type) != napi_ok || text_type != napi_string) {
    throw_error(env, "TypeError", "text must be a string");
    return nullptr;
  }
  moonbit_string_t text = nullptr;
  moonbit_bytes_t bytes = nullptr;
  if (!make_moonbit_string(env, argv[2], &text) || !make_moonbit_bytes(env, argv[3], &bytes)) {
    if (text != nullptr) moonbit_decref(text);
    return nullptr;
  }
  struct SyncContext {
    Session *session;
    napi_env env;
    int32_t callback_result = kCallbackError;
    int32_t callback_total = 0;
    napi_value result_text = nullptr;
    napi_value result_bytes = nullptr;
    bool callback_threw = false;
  } context{session, env};
  {
    std::lock_guard<std::mutex> lock(session->mutex);
    if (session->closed) {
      moonbit_decref(text);
      moonbit_decref(bytes);
      throw_error(env, "Closed", "session is closed");
      return nullptr;
    }
    session->managed_frames += 1;
    session->callback_thread = thread_name(std::this_thread::get_id());
  }
  (void)ajni_ohos_round_trip(&context, delta, text, bytes,
                                       [](void *opaque, int32_t value, moonbit_string_t callback_text,
                                          moonbit_bytes_t callback_bytes) -> int32_t {
    SyncContext *ctx = static_cast<SyncContext *>(opaque);
    ctx->callback_total = value;
    napi_value callback = nullptr;
    {
      std::lock_guard<std::mutex> lock(ctx->session->mutex);
      if (ctx->session->callback_ref != nullptr)
        (void)napi_get_reference_value(ctx->env, ctx->session->callback_ref, &callback);
    }
    if (callback == nullptr || !make_js_string(ctx->env, callback_text, &ctx->result_text) ||
        !make_js_bytes(ctx->env, callback_bytes, &ctx->result_bytes)) {
      if (!pending_exception(ctx->env)) (void)napi_throw_error(ctx->env, "Error", "callback value conversion failed");
      moonbit_decref(callback_text);
      moonbit_decref(callback_bytes);
      ctx->callback_threw = pending_exception(ctx->env);
      return kCallbackError;
    }
    napi_value kind = nullptr;
    napi_value number = nullptr;
    if (napi_create_int32(ctx->env, 0, &kind) != napi_ok ||
        napi_create_int32(ctx->env, value, &number) != napi_ok) {
      if (!pending_exception(ctx->env)) (void)napi_throw_error(ctx->env, "Error", "callback argument creation failed");
      moonbit_decref(callback_text);
      moonbit_decref(callback_bytes);
      ctx->callback_threw = pending_exception(ctx->env);
      return kCallbackError;
    }
    napi_value argv[4] = {kind, number, ctx->result_text, ctx->result_bytes};
    napi_value returned = nullptr;
    napi_status status = napi_call_function(ctx->env, callback, callback, 4, argv, &returned);
    bool threw = pending_exception(ctx->env);
    if (threw) {
      ctx->callback_threw = true;
      moonbit_decref(callback_text);
      moonbit_decref(callback_bytes);
      return kCallbackError;
    }
    if (status != napi_ok || returned == nullptr || !get_int32(ctx->env, returned, &ctx->callback_result, "callback must return int32")) {
      throw_error(ctx->env, "TypeError", "callback must return int32");
      ctx->callback_threw = true;
      moonbit_decref(callback_text);
      moonbit_decref(callback_bytes);
      return kCallbackError;
    }
    moonbit_decref(callback_text);
    moonbit_decref(callback_bytes);
    return ctx->callback_result;
  });
  update_managed_snapshot(session);
  {
    std::lock_guard<std::mutex> lock(session->mutex);
    session->managed_frames -= 1;
    if (context.callback_threw) session->callback_errors += 1;
  }
  if (context.callback_threw && pending_exception(env)) return nullptr;
  if (delta < 0) {
    if (!pending_exception(env)) throw_error(env, "MoonBitError", "round trip failed");
    return nullptr;
  }
  napi_value result = nullptr;
  if (napi_create_object(env, &result) != napi_ok) return nullptr;
  napi_value value = nullptr;
  if (napi_create_int32(env, context.callback_total, &value) == napi_ok) (void)napi_set_named_property(env, result, "total", value);
  if (napi_create_int32(env, context.callback_result, &value) == napi_ok) (void)napi_set_named_property(env, result, "callbackResult", value);
  if (napi_set_named_property(env, result, "text", context.result_text) != napi_ok ||
      napi_set_named_property(env, result, "bytes", context.result_bytes) != napi_ok) return nullptr;
  return result;
}

napi_value start_worker_common(napi_env env, napi_callback_info info, bool burst, bool hold) {
  napi_value argv[2] = {nullptr};
  if (!get_arg_count(env, info, hold ? 1 : 2, argv)) return nullptr;
  EnvState *state = nullptr;
  (void)napi_get_instance_data(env, reinterpret_cast<void **>(&state));
  uint64_t token = 0;
  if (!get_safe_token(env, argv[0], &token)) return nullptr;
  Session *session = lookup_session(state, token);
  if (!owner_session(env, session)) return nullptr;
  int32_t count = hold ? 1 : 0;
  if (!hold && !get_int32(env, argv[1], &count, "count must be int32")) return nullptr;
  if ((burst && (count < 1 || count > 256)) || (!burst && !hold && (count < 1 || count > 32))) {
    throw_error(env, "RangeError", "count is outside the supported range");
    return nullptr;
  }
  napi_threadsafe_function tsfn = nullptr;
  {
    std::lock_guard<std::mutex> lock(session->mutex);
    if (session->closed) {
      throw_error(env, "Closed", "session is closed");
      return nullptr;
    }
  }
  if (!acquire_producer(session, &tsfn)) {
    throw_error(env, "Busy", "worker is already running or session is closed");
    return nullptr;
  }
  if (session->producer.joinable()) session->producer.join();
  try {
    session->producer = std::thread(worker_body, session, tsfn, count, hold);
  } catch (...) {
    {
      std::lock_guard<std::mutex> lock(session->mutex);
      session->producer_active = false;
      session->worker_running = false;
    }
    (void)napi_release_threadsafe_function(tsfn, napi_tsfn_release);
    throw_error(env, "Error", "unable to start worker");
    return nullptr;
  }
  if (burst || hold) {
    std::unique_lock<std::mutex> lock(session->mutex);
    if (hold) session->condition.wait(lock, [session] { return session->producer_parked || !session->producer_active; });
    else {
      lock.unlock();
      if (session->producer.joinable()) session->producer.join();
      return nullptr;
    }
  }
  return nullptr;
}

napi_value close_callback(napi_env env, napi_callback_info info) {
  napi_value argv[1] = {nullptr};
  if (!get_arg_count(env, info, 1, argv)) return nullptr;
  EnvState *state = nullptr;
  (void)napi_get_instance_data(env, reinterpret_cast<void **>(&state));
  uint64_t token = 0;
  if (!get_safe_token(env, argv[0], &token)) return nullptr;
  Session *session = lookup_session(state, token);
  if (!owner_session(env, session)) return nullptr;
  {
    std::lock_guard<std::mutex> lock(session->mutex);
    if (session->closed) return nullptr;
    if (session->managed_frames != 0) {
      throw_error(env, "Busy", "session has an active managed callback");
      return nullptr;
    }
  }
  stop_session(session, true);
  return nullptr;
}

napi_value snapshot_callback(napi_env env, napi_callback_info info) {
  napi_value argv[1] = {nullptr};
  if (!get_arg_count(env, info, 1, argv)) return nullptr;
  EnvState *state = nullptr;
  (void)napi_get_instance_data(env, reinterpret_cast<void **>(&state));
  uint64_t token = 0;
  if (!get_safe_token(env, argv[0], &token)) return nullptr;
  Session *session = lookup_session(state, token);
  if (session == nullptr) {
    throw_error(env, "RangeError", "unknown token");
    return nullptr;
  }
  return snapshot_value(env, session);
}

napi_value diagnostics_callback(napi_env env, napi_callback_info info) {
  (void)info;
  napi_value object = nullptr;
  if (napi_create_object(env, &object) != napi_ok) return nullptr;
  napi_value value = nullptr;
#define DIAG_I64(name, atom) \
  do { if (napi_create_int64(env, (atom).load(std::memory_order_acquire), &value) == napi_ok) (void)napi_set_named_property(env, object, (name), value); } while (0)
#define DIAG_BOOL(name, atom) \
  do { if (napi_get_boolean(env, (atom).load(std::memory_order_acquire), &value) == napi_ok) (void)napi_set_named_property(env, object, (name), value); } while (0)
  DIAG_I64("liveSessions", g_live_sessions);
  DIAG_I64("livePayloads", g_live_payloads);
  DIAG_I64("finalizedSessions", g_finalized_sessions);
  DIAG_I64("cleanupHooks", g_cleanup_hooks);
  DIAG_BOOL("ownerInitialized", g_owner_initialized);
  DIAG_BOOL("ownerTerminated", g_owner_terminated);
#undef DIAG_I64
#undef DIAG_BOOL
  return object;
}

napi_value init_module(napi_env env, napi_value exports) {
  EnvState *state = new (std::nothrow) EnvState{env};
  if (state == nullptr) return exports;
  if (napi_set_instance_data(env, state, nullptr, nullptr) != napi_ok) {
    delete state;
    throw_error(env, "Error", "unable to retain module environment");
    return exports;
  }
  if (napi_add_env_cleanup_hook(env, cleanup_env, state) != napi_ok) {
    (void)napi_set_instance_data(env, nullptr, nullptr, nullptr);
    delete state;
    throw_error(env, "Error", "unable to register environment cleanup");
    return exports;
  }
  napi_property_descriptor properties[] = {
      {"open", nullptr, open_callback, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"roundTrip", nullptr, round_trip_callback, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"startWorker", nullptr, [](napi_env e, napi_callback_info i) { return start_worker_common(e, i, false, false); }, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"queueBurst", nullptr, [](napi_env e, napi_callback_info i) { return start_worker_common(e, i, true, false); }, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"holdWorker", nullptr, [](napi_env e, napi_callback_info i) { return start_worker_common(e, i, false, true); }, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"close", nullptr, close_callback, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"snapshot", nullptr, snapshot_callback, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"diagnostics", nullptr, diagnostics_callback, nullptr, nullptr, nullptr, napi_default, nullptr},
  };
  if (napi_define_properties(env, exports, sizeof(properties) / sizeof(properties[0]), properties) != napi_ok) return exports;
  return exports;
}

} // namespace

NAPI_MODULE(ajni_ohos_probe, init_module)
