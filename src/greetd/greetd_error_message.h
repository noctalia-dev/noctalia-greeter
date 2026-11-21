#pragma once

#include "greetd/greetd_client.h"

#include <string>

[[nodiscard]] std::string greetdErrorMessage(const GreetdError& error);
