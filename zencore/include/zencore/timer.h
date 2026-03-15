// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <intrin.h>
#include <stdint.h>
#include "zencore.h"

namespace zen {

// High frequency timers

ZENCORE_API uint64_t GetHifreqTimerValue();
ZENCORE_API uint64_t GetHifreqTimerFrequency();
ZENCORE_API uint64_t GetHifreqTimerFrequencySafe();	 // May be used during static init

class Stopwatch
{
public:
	Stopwatch() : m_StartValue(GetHifreqTimerValue()) {}

	inline uint64_t getElapsedTimeMs() { return (GetHifreqTimerValue() - m_StartValue) * 1000 / GetHifreqTimerFrequency(); }

	inline void reset() { m_StartValue = GetHifreqTimerValue(); }

private:
	uint64_t m_StartValue;
};

// CPU timers

inline uint64_t
GetCpuTimerValue()
{
	unsigned int foo;
	return __rdtscp(&foo);
}

void timer_forcelink();	 // internal

}  // namespace zen

