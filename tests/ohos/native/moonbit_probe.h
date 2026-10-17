#ifndef AJNI_OHOS_MOONBIT_PROBE_H
#define AJNI_OHOS_MOONBIT_PROBE_H

#include <stdint.h>
#include <moonbit.h>

#ifdef __cplusplus
extern "C" {
#endif

void moonbit_runtime_init(int argc, char **argv);
void moonbit_init(void);
void ajni_ohos_reset(void);
int32_t ajni_ohos_total(void);
int32_t ajni_ohos_entries(void);
int32_t ajni_ohos_worker(int32_t sequence);

/* Export and callback string/bytes arguments transfer one managed reference. */
int32_t ajni_ohos_round_trip(
    void *context, int32_t delta, moonbit_string_t text, moonbit_bytes_t bytes,
    int32_t (*callback)(void *, int32_t, moonbit_string_t, moonbit_bytes_t));

#ifdef __cplusplus
}
#endif

#endif
