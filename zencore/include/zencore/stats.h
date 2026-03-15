// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <atomic>
#include <type_traits>
#include "zencore.h"

namespace zen {

template<typename T>
class Gauge
{
public:
	Gauge() : m_value{0} {}

private:
	T m_value;
};

class Counter
{
public:
	inline void		SetValue(uint64_t Value) { m_count = Value; }
	inline uint64_t Value() const { return m_count; }

	inline void Increment(int64_t AddValue) { m_count += AddValue; }
	inline void Decrement(int64_t SubValue) { m_count -= SubValue; }
	inline void Clear() { m_count = 0; }

private:
	std::atomic_uint64_t m_count{0};
};

/// <summary>
/// Exponential Weighted Moving Average
/// </summary>
class EWMA
{
public:
	/// <summary>
	/// Update EWMA with new measure
	/// </summary>
	/// <param name="Alpha">Smoothing factor (between 0 and 1)</param>
	/// <param name="Interval">Elapsed time since last</param>
	/// <param name="Count">Value</param>
	/// <param name="IsInitialUpdate">Whether this is the first update or not</param>
	void   Tick(double Alpha, uint64_t Interval, uint64_t Count, bool IsInitialUpdate);
	double Rate() const;

private:
	double m_rate = 0;
};

/// <summary>
/// Tracks rate of events over time (i.e requests/sec)
/// </summary>
class Meter
{
public:
private:
};

extern void stats_forcelink();

}  // namespace zen

