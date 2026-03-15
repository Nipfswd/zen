// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <spdlog/spdlog.h>

struct ZenServerOptions;

void InitializeLogging(const ZenServerOptions& GlobalOptions);

spdlog::logger& ConsoleLog();

