#pragma once

#include "core/log.h"

void emergencyLogBootstrap(int argc, char* argv[]);
void initLogging();
[[nodiscard]] const char* loggingPaths();
void installWlrLogHandler();
