// Copyright Noah Games, Inc. All Rights Reserved.

#include "zencore/trace.h"
#include <doctest/doctest.h>
#include <zencore/windows.h>

namespace zen {

void
Tracer::Log(const TraceEvent& e)
{
	TraceBroadcast(e);
}

Tracer g_globalTracer;

struct alignas(64) TraceHandlerList
{
	enum
	{
		kMaxHandlers = 7
	};

	uint8_t		  handlerCount = 0;
	TraceHandler* handlers[kMaxHandlers];
};

static TraceHandlerList g_traceHandlers;

void
TraceBroadcast(const TraceEvent& e)
{
	for (size_t i = 0; i < g_traceHandlers.handlerCount; ++i)
	{
		g_traceHandlers.handlers[i]->Trace(e);
	}
}

void
trace_forcelink()
{
}

//////////////////////////////////////////////////////////////////////////

TEST_CASE("Tracer")
{
	SUBCASE("Simple") { U_LOG_INFO("bajs"); }
}

}  // namespace zen

