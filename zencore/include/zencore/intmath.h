// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include "zencore.h"

#include <stdint.h>
#include <concepts>

//////////////////////////////////////////////////////////////////////////

#pragma intrinsic(_BitScanReverse)
#pragma intrinsic(_BitScanReverse64)

namespace zen {

inline constexpr bool
IsPow2(uint64_t n)
{
	return 0 == (n & (n - 1));
}

/// Round an integer up to the closest integer multiplier of 'base' ('base' must be a power of two)
template<std::integral T>
T
RoundUp(T Value, auto Base)
{
	ZEN_ASSERT_SLOW(IsPow2(Base));
	return ((Value + T(Base - 1)) & (~T(Base - 1)));
}

bool
IsMultipleOf(std::integral auto Value, auto MultiplierPow2)
{
	ZEN_ASSERT_SLOW(IsPow2(MultiplierPow2));
	return (Value & (MultiplierPow2 - 1)) == 0;
}

inline uint64_t
NextPow2(uint64_t n)
{
	// http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2

	--n;

	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	n |= n >> 16;
	n |= n >> 32;

	return n + 1;
}

static inline uint32_t
FloorLog2(uint32_t Value)
{
	// Use BSR to return the log2 of the integer
	unsigned long Log2;
	if (_BitScanReverse(&Log2, Value) != 0)
	{
		return Log2;
	}

	return 0;
}

static inline uint32_t
CountLeadingZeros(uint32_t Value)
{
	unsigned long Log2;
	_BitScanReverse64(&Log2, (uint64_t(Value) << 1) | 1);
	return 32 - Log2;
}

static inline uint64_t
FloorLog2_64(uint64_t Value)
{
	unsigned long Log2;
	long		  Mask = -long(_BitScanReverse64(&Log2, Value) != 0);
	return Log2 & Mask;
}

static inline uint64_t
CountLeadingZeros64(uint64_t Value)
{
	unsigned long Log2;
	long		  Mask = -long(_BitScanReverse64(&Log2, Value) != 0);
	return ((63 - Log2) & Mask) | (64 & ~Mask);
}

static inline uint64_t
CeilLogTwo64(uint64_t Arg)
{
	int64_t Bitmask = ((int64_t)(CountLeadingZeros64(Arg) << 57)) >> 63;
	return (64 - CountLeadingZeros64(Arg - 1)) & (~Bitmask);
}

static inline uint64_t
CountTrailingZeros64(uint64_t Value)
{
	if (Value == 0)
	{
		return 64;
	}
	unsigned long BitIndex;				  // 0-based, where the LSB is 0 and MSB is 31
	_BitScanForward64(&BitIndex, Value);  // Scans from LSB to MSB
	return BitIndex;
}

//////////////////////////////////////////////////////////////////////////

static inline bool
IsPointerAligned(const void* Ptr, uint64_t Alignment)
{
	ZEN_ASSERT_SLOW(IsPow2(Alignment));

	return 0 == (reinterpret_cast<uintptr_t>(Ptr) & (Alignment - 1));
}

//////////////////////////////////////////////////////////////////////////

#ifdef min
#	error "Looks like you did #include <windows.h> -- use <zencore/windows.h> instead"
#endif

auto
Min(auto x, auto y)
{
	return x < y ? x : y;
}

auto
Max(auto x, auto y)
{
	return x > y ? x : y;
}

}  // namespace zen

