// Copyright Noah Games, Inc. All Rights Reserved.

#include "zencore/stats.h"
#include <doctest/doctest.h>
#include <cmath>
#include "zencore/timer.h"

//
// Derived from https://github.com/dln/medida/blob/master/src/medida/stats/ewma.cc
//

namespace zen {

static constexpr int	kTickInterval	  = 5;	// In seconds
static constexpr double kSecondsPerMinute = 60.0;
static constexpr int	kOneMinute		  = 1;
static constexpr int	kFiveMinutes	  = 5;
static constexpr int	kFifteenMinutes	  = 15;

static double kM1_ALPHA	 = 1.0 - std::exp(-kTickInterval / kSecondsPerMinute / kOneMinute);
static double kM5_ALPHA	 = 1.0 - std::exp(-kTickInterval / kSecondsPerMinute / kFiveMinutes);
static double kM15_ALPHA = 1.0 - std::exp(-kTickInterval / kSecondsPerMinute / kFifteenMinutes);

static uint64_t CountPerTick   = GetHifreqTimerFrequencySafe() * kTickInterval;
static uint64_t CountPerSecond = GetHifreqTimerFrequencySafe();

void
EWMA::Tick(double Alpha, uint64_t Interval, uint64_t Count, bool IsInitialUpdate)
{
	double InstantRate = double(Count) / Interval;

	if (IsInitialUpdate)
	{
		m_rate = InstantRate;
	}
	else
	{
		m_rate += Alpha * (InstantRate - m_rate);
	}
}

double
EWMA::Rate() const
{
	return m_rate * CountPerSecond;
}

//////////////////////////////////////////////////////////////////////////

TEST_CASE("Stats")
{
	SUBCASE("Simple")
	{
		EWMA ewma1;
		ewma1.Tick(kM1_ALPHA, CountPerSecond, 5, true);

		CHECK(ewma1.Rate() - 5 < 0.001);

		for (int i = 0; i < 60; ++i)
			ewma1.Tick(kM1_ALPHA, CountPerSecond, 10, false);

		CHECK(ewma1.Rate() - 10 < 0.001);

		ewma1.Tick(kM1_ALPHA, CountPerSecond, 10, false);
	}
}

void
stats_forcelink()
{
}

}  // namespace zen

