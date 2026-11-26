#pragma once

// Initialize LC_TIME once at startup, before creating threads. When LC_ALL,
// LC_TIME and LANG are all unset or empty, use LC_TIME (then LANG) from the
// first usable system locale file, otherwise the first available locale from
// en_US.UTF-8, C.UTF-8 and C. Explicit environment settings always win.
// Returns false if the selected locale is unavailable.
[[nodiscard]] bool initializeTimeLocale(
    const char* localeConfigPath = "/etc/locale.conf", const char* fallbackConfigPath = "/etc/default/locale"
);
