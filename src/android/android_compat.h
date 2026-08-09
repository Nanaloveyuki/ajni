#ifndef AJNI_ANDROID_COMPAT_H
#define AJNI_ANDROID_COMPAT_H

#if defined(__ANDROID__) && __ANDROID_API__ < 28
#include <stddef.h>

int getentropy(void *buffer, size_t length);
#endif

#endif
