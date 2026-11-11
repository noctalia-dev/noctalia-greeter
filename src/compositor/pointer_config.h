#pragma once

struct libinput_device;

#ifdef __cplusplus
extern "C" {
#endif

// An empty profile keeps the legacy flat profile; a null speed leaves the
// device's speed unchanged. Apply on each libinput pointer as it is added.
void pointer_config_apply(struct libinput_device* device, const char* name, const char* profile, const double* speed);

#ifdef __cplusplus
}
#endif
