// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

namespace std::filesystem {
class path;
}

void InitializeCrashReporting(const std::filesystem::path& DumpPath);

