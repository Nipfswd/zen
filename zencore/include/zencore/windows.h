// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

struct IUnknown;  // Workaround for "combaseapi.h(229): error C2187: syntax error: 'identifier' was unexpected here" when using /permissive-
#ifndef NOMINMAX
#	define NOMINMAX  // We don't want your min/max macros
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

