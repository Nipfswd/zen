// Copyright Noah Games, Inc. All Rights Reserved.

#include <doctest/doctest.h>
#include <zencore/thread.h>
#include <zencore/timer.h>
#include <zencore/windows.h>

namespace zen {

uint64_t
GetHifreqTimerValue()
{
	LARGE_INTEGER li;
	QueryPerformanceCounter(&li);

	return li.QuadPart;
}

uint64_t
internalGetHifreqTimerFrequency()
{
	LARGE_INTEGER li;
	QueryPerformanceFrequency(&li);

	return li.QuadPart;
}

static uint64_t qpcFreq = internalGetHifreqTimerFrequency();

uint64_t
GetHifreqTimerFrequency()
{
	return qpcFreq;
}

uint64_t
GetHifreqTimerFrequencySafe()
{
	if (!qpcFreq)
		qpcFreq = internalGetHifreqTimerFrequency();

	return qpcFreq;
}

//////////////////////////////////////////////////////////////////////////
//
// Testing related code follows...
//

void
timer_forcelink()
{
}

TEST_CASE("Timer")
{
	uint64_t s0 = GetHifreqTimerValue();
	uint64_t t0 = GetCpuTimerValue();
	Sleep(1000);
	uint64_t s1 = GetHifreqTimerValue();
	uint64_t t1 = GetCpuTimerValue();
	// double r = double(t1 - t0) / (s1 - s0);
	CHECK_NE(t0, t1);
	CHECK_NE(s0, s1);
}

}  // namespace zen

