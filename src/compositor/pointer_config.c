#include "compositor/pointer_config.h"

#include <libinput.h>
#include <string.h>
#include <wlr/util/log.h>

void pointer_config_apply(struct libinput_device* device, const char* name, const char* profile, const double* speed) {
  if (device == NULL || !libinput_device_config_accel_is_available(device)) {
    return;
  }

  const enum libinput_config_accel_profile selected =
      strcmp(profile, "adaptive") == 0 ? LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE : LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT;
  const char* selected_name = selected == LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE ? "adaptive" : "flat";
  if ((libinput_device_config_accel_get_profiles(device) & (uint32_t)selected) == 0) {
    wlr_log(WLR_ERROR, "pointer %s does not support accel_profile=%s", name, selected_name);
  } else if (libinput_device_config_accel_set_profile(device, selected) != LIBINPUT_CONFIG_STATUS_SUCCESS) {
    wlr_log(WLR_ERROR, "pointer %s: failed to set accel_profile=%s", name, selected_name);
  } else {
    wlr_log(WLR_INFO, "pointer %s: accel_profile=%s", name, selected_name);
  }

  if (speed != NULL) {
    if (libinput_device_config_accel_set_speed(device, *speed) != LIBINPUT_CONFIG_STATUS_SUCCESS) {
      wlr_log(WLR_ERROR, "pointer %s: failed to set accel_speed=%.3f", name, *speed);
    } else {
      wlr_log(WLR_INFO, "pointer %s: accel_speed=%.3f", name, *speed);
    }
  }
}
