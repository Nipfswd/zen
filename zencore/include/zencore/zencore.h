// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <cinttypes>
#include <exception>
#include <string>

//////////////////////////////////////////////////////////////////////////
// Platform
//

#define ZEN_PLATFORM_WINDOWS 1
#define ZEN_PLATFORM_LINUX	 0
#define ZEN_PLATFORM_MACOS	 0

//////////////////////////////////////////////////////////////////////////
// Compiler
//

#ifdef _MSC_VER
#	define ZEN_COMPILER_MSC 1
#endif

#ifndef ZEN_COMPILER_MSC
#	define ZEN_COMPILER_MSC 0
#endif

#ifndef ZEN_COMPILER_CLANG
#	define ZEN_COMPILER_CLANG 0
#endif

//////////////////////////////////////////////////////////////////////////
// Build flavor
//

#ifdef NDEBUG
#	define ZEN_BUILD_DEBUG	  0
#	define ZEN_BUILD_RELEASE 1
#else
#	define ZEN_BUILD_DEBUG	  1
#	define ZEN_BUILD_RELEASE 0
#endif

//////////////////////////////////////////////////////////////////////////

#define ZEN_PLATFORM_SUPPORTS_UNALIGNED_LOADS 1

//////////////////////////////////////////////////////////////////////////
// Assert
//

namespace zen {

class AssertException : public std::exception
{
public:
	AssertException(const char* Msg) : m_Msg(Msg) {}

	[[nodiscard]] virtual char const* what() const override { return m_Msg.c_str(); }

private:
	std::string m_Msg;
};

}  // namespace zen

#define ZEN_ASSERT(x, ...)                \
	do                                    \
	{                                     \
		if (x)                            \
			break;                        \
		throw ::zen::AssertException{#x}; \
	} while (false)

#ifndef NDEBUG
#	define ZEN_ASSERT_SLOW(x, ...)           \
		do                                    \
		{                                     \
			if (x)                            \
				break;                        \
			throw ::zen::AssertException{#x}; \
		} while (false)
#else
#	define ZEN_ASSERT_SLOW(x, ...)
#endif

//////////////////////////////////////////////////////////////////////////

#ifdef __clang__
template<typename T>
auto ZenArrayCountHelper(T& t) -> typename std::enable_if<__is_array(T), char (&)[sizeof(t) / sizeof(t[0]) + 1]>::Type;
#else
template<typename T, uint32_t N>
char (&ZenArrayCountHelper(const T (&)[N]))[N + 1];
#endif

#define ZEN_ARRAY_COUNT(array) (sizeof(ZenArrayCountHelper(array)) - 1)

//////////////////////////////////////////////////////////////////////////

#define ZEN_NOINLINE			 __declspec(noinline)
#define ZEN_UNUSED(...)			 ((void)__VA_ARGS__)
#define ZEN_NOT_IMPLEMENTED(...) ZEN_ASSERT(false)
#define ZENCORE_API	 // Placeholder to allow DLL configs in the future

ZENCORE_API bool IsPointerToStack(const void* ptr);	 // Query if pointer is within the stack of the currently executing thread
ZENCORE_API bool IsApplicationExitRequested();
ZENCORE_API void RequestApplicationExit(int ExitCode);

ZENCORE_API void zencore_forcelinktests();

//////////////////////////////////////////////////////////////////////////

#if ZEN_COMPILER_MSC
#	define ZEN_DISABLE_OPTIMIZATION_ACTUAL __pragma(optimize("", off))
#	define ZEN_ENABLE_OPTIMIZATION_ACTUAL	__pragma(optimize("", on))
#else
#endif

// Set up optimization control macros, now that we have both the build settings and the platform macros
#define ZEN_DISABLE_OPTIMIZATION ZEN_DISABLE_OPTIMIZATION_ACTUAL

#if ZEN_BUILD_DEBUG
#	define ZEN_ENABLE_OPTIMIZATION ZEN_DISABLE_OPTIMIZATION_ACTUAL
#else
#	define ZEN_ENABLE_OPTIMIZATION ZEN_ENABLE_OPTIMIZATION_ACTUAL
#endif

#define ZEN_ENABLE_OPTIMIZATION_ALWAYS ZEN_ENABLE_OPTIMIZATION_ACTUAL

//////////////////////////////////////////////////////////////////////////

using ThreadId_t = uint32_t;

