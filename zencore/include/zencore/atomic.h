// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <intrin.h>
#include <cinttypes>

namespace zen {

inline uint32_t
AtomicIncrement(volatile uint32_t& value)
{
	return _InterlockedIncrement((long volatile*)&value);
}
inline uint32_t
AtomicDecrement(volatile uint32_t& value)
{
	return _InterlockedDecrement((long volatile*)&value);
}

inline uint64_t
AtomicIncrement(volatile uint64_t& value)
{
	return _InterlockedIncrement64((__int64 volatile*)&value);
}
inline uint64_t
AtomicDecrement(volatile uint64_t& value)
{
	return _InterlockedDecrement64((__int64 volatile*)&value);
}

inline uint32_t
AtomicAdd(volatile uint32_t& value, uint32_t amount)
{
	return _InterlockedExchangeAdd((long volatile*)&value, amount);
}
inline uint64_t
AtomicAdd(volatile uint64_t& value, uint64_t amount)
{
	return _InterlockedExchangeAdd64((__int64 volatile*)&value, amount);
}

}  // namespace zen

