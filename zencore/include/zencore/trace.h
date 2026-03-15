// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <inttypes.h>
#include "zencore.h"

#pragma section("trace_events", read)
#define U_TRACE_DECL __declspec(allocate("trace_events"))

//////////////////////////////////////////////////////////////////////////

namespace zen {

struct TraceSite
{
	const char*	   sourceFile;
	const uint32_t sourceLine;
	const uint32_t flags;
};

struct TraceEvent
{
	const TraceSite* site;
	ThreadId_t		 threadId;
	const char*		 message;
};

enum TraceFlags
{
	kTrace_Debug = 1 << 0,
	kTrace_Info	 = 1 << 1,
	kTrace_Warn	 = 1 << 2,
	kTrace_Error = 1 << 3,
	kTrace_Fatal = 1 << 4,

	kTrace_Trace = 1 << 7,
};

class Tracer
{
public:
	void Log(const TraceEvent& e);

	__forceinline uint32_t Accept(const TraceSite& e) const { return (m_acceptFlags & e.flags); }

private:
	uint32_t m_acceptFlags = ~0u;
};

ZENCORE_API extern Tracer g_globalTracer;

/** Trace event handler
 */
class TraceHandler
{
public:
	virtual void Trace(const TraceEvent& e) = 0;

private:
};

ZENCORE_API static void TraceBroadcast(const TraceEvent& e);

void trace_forcelink();	 // internal

}  // namespace zen

__forceinline zen::Tracer&
CurrentTracer()
{
	return zen::g_globalTracer;
}

#define U_LOG_GENERIC(msg, flags)                                                          \
	do                                                                                     \
	{                                                                                      \
		zen::Tracer&								 t = CurrentTracer();                  \
		static U_TRACE_DECL constexpr zen::TraceSite traceSite{__FILE__, __LINE__, flags}; \
		const zen::TraceEvent						 traceEvent = {&traceSite, 0u, msg};   \
		if (t.Accept(traceSite))                                                           \
			t.Log(traceEvent);                                                             \
	} while (false)

//////////////////////////////////////////////////////////////////////////

#define U_LOG_DEBUG(msg) U_LOG_GENERIC(msg, zen::kTrace_Debug)
#define U_LOG_INFO(msg)	 U_LOG_GENERIC(msg, zen::kTrace_Info)
#define U_LOG_WARN(msg)	 U_LOG_GENERIC(msg, zen::kTrace_Warn)
#define U_LOG_ERROR(msg) U_LOG_GENERIC(msg, zen::kTrace_Error)
#define U_LOG_FATAL(msg) U_LOG_GENERIC(msg, zen::kTrace_Fatal)

