// Copyright Noah Games, Inc. All Rights Reserved.

#include <zencore/zencore.h>

#include <zencore/windows.h>

#include <zencore/blake3.h>
#include <zencore/compactbinary.h>
#include <zencore/compactbinarybuilder.h>
#include <zencore/compactbinarypackage.h>
#include <zencore/iobuffer.h>
#include <zencore/memory.h>
#include <zencore/refcount.h>
#include <zencore/sha1.h>
#include <zencore/snapshot_manifest.h>
#include <zencore/stats.h>
#include <zencore/stream.h>
#include <zencore/string.h>
#include <zencore/thread.h>
#include <zencore/timer.h>
#include <zencore/trace.h>
#include <zencore/uid.h>

bool
IsPointerToStack(const void* ptr)
{
	ULONG_PTR low, high;
	GetCurrentThreadStackLimits(&low, &high);

	const uintptr_t intPtr = reinterpret_cast<uintptr_t>(ptr);

	return (intPtr - low) < (high - low);
}

static int	s_ApplicationExitCode = 0;
static bool s_ApplicationExitRequested;

bool
IsApplicationExitRequested()
{
	return s_ApplicationExitRequested;
}

void
RequestApplicationExit(int ExitCode)
{
	s_ApplicationExitCode	   = ExitCode;
	s_ApplicationExitRequested = true;
}

void
zencore_forcelinktests()
{
	zen::sha1_forcelink();
	zen::blake3_forcelink();
	zen::trace_forcelink();
	zen::timer_forcelink();
	zen::uid_forcelink();
	zen::string_forcelink();
	zen::thread_forcelink();
	zen::stream_forcelink();
	zen::refcount_forcelink();
	zen::snapshotmanifest_forcelink();
	zen::iobuffer_forcelink();
	zen::stats_forcelink();
	zen::uson_forcelink();
	zen::usonbuilder_forcelink();
	zen::usonpackage_forcelink();
	zen::memory_forcelink();
}

