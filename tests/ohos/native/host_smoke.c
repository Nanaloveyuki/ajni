#include "moonbit_probe.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

struct CallbackState {
  int calls;
  int reenter;
  int failed;
};

static int32_t receive(void *context, int32_t total, moonbit_string_t text,
                       moonbit_bytes_t bytes) {
  struct CallbackState *state = context;
  static const uint16_t expected_text[] = {0, 0x4e2d, 0xd83d, 0xde80, 0xd800, 0xdc00};
  static const uint8_t expected_bytes[] = {0, 0x80, 0xff};
  state->calls++;
  if (Moonbit_array_length(text) != 6 || Moonbit_array_length(bytes) != 3 ||
      memcmp(text, expected_text, sizeof expected_text) != 0 ||
      memcmp(bytes, expected_bytes, sizeof expected_bytes) != 0) {
    state->failed = 1;
  }
  if (state->reenter) {
    state->reenter = 0;
    moonbit_incref(text);
    moonbit_incref(bytes);
    int32_t nested = ajni_ohos_round_trip(context, 2, text, bytes, receive);
    if (nested != 14) state->failed = 1;
  }
  moonbit_decref(text);
  moonbit_decref(bytes);
  return total * 2;
}

int main(void) {
  moonbit_runtime_init(0, NULL);
  moonbit_init();
  ajni_ohos_reset();
  moonbit_string_t text = moonbit_make_string(6, 0);
  const uint16_t units[] = {0, 0x4e2d, 0xd83d, 0xde80, 0xd800, 0xdc00};
  memcpy(text, units, sizeof units);
  moonbit_bytes_t bytes = moonbit_make_bytes(3, 0);
  bytes[1] = 0x80;
  bytes[2] = 0xff;
  struct CallbackState state = {0, 1, 0};
  moonbit_incref(text);
  moonbit_incref(bytes);
  int32_t result = ajni_ohos_round_trip(&state, 5, text, bytes, receive);
  if (result != 10 || state.calls != 2 || state.failed || ajni_ohos_total() != 7) return 1;
  moonbit_incref(text);
  moonbit_incref(bytes);
  if (ajni_ohos_round_trip(&state, -1, text, bytes, receive) != INT32_MIN ||
      state.calls != 2 || ajni_ohos_total() != 7) return 2;
  int32_t recovered = ajni_ohos_round_trip(&state, 3, text, bytes, receive);
  if (recovered != 20 || state.calls != 3 || state.failed || ajni_ohos_total() != 10) return 3;
  if (ajni_ohos_worker(4) != 14 || ajni_ohos_entries() != 5) return 4;
  ajni_ohos_reset();
  if (ajni_ohos_total() != 0 || ajni_ohos_entries() != 0) return 5;
  puts("Managed Harmony fixture host smoke passed: reentry, UTF-16, bytes, error recovery, worker state");
  return 0;
}
