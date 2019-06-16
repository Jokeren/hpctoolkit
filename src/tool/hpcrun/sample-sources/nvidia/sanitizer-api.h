#ifndef _HPCTOOLKIT_SANITIZER_API_H_
#define _HPCTOOLKIT_SANITIZER_API_H_

bool
sanitizer_bind();

void
sanitizer_callbacks_subscribe();

void
sanitizer_callbacks_unsubscribe();

void
sanitizer_stop_flag_set();

void
sanitizer_stop_flag_unset();

void
sanitizer_device_flush(void *args);

void
sanitizer_device_shutdown(void *args);

#endif
