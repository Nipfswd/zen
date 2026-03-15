// Copyright Noah Games, Inc. All Rights Reserved.

#include <zencore/thread.h>

#include <zencore/except.h>
#include <zencore/string.h>
#include <zencore/windows.h>
#include <thread>

namespace zen {

void
RwLock::AcquireShared()
{
	AcquireSRWLockShared((PSRWLOCK)&m_Srw);
}

void
RwLock::ReleaseShared()
{
	ReleaseSRWLockShared((PSRWLOCK)&m_Srw);
}

void
RwLock::AcquireExclusive()
{
	AcquireSRWLockExclusive((PSRWLOCK)&m_Srw);
}

void
RwLock::ReleaseExclusive()
{
	ReleaseSRWLockExclusive((PSRWLOCK)&m_Srw);
}

Event::Event()
{
	m_EventHandle = CreateEvent(nullptr, true, false, nullptr);
}

Event::~Event()
{
	CloseHandle(m_EventHandle);
}

void
Event::Set()
{
	SetEvent(m_EventHandle);
}

void
Event::Reset()
{
	ResetEvent(m_EventHandle);
}

bool
Event::Wait(int TimeoutMs)
{
	const DWORD Timeout = (TimeoutMs < 0) ? INFINITE : TimeoutMs;

	DWORD Result = WaitForSingleObject(m_EventHandle, Timeout);

	if (Result == WAIT_FAILED)
	{
		throw WindowsException("Event wait failed");
	}

	return (Result == WAIT_OBJECT_0);
}

NamedEvent::NamedEvent(std::u8string_view EventName) : Event(nullptr)
{
	using namespace std::literals;

	ExtendableStringBuilder<64> Name;
	Name << "Local\\"sv;
	Name << EventName;

	m_EventHandle = CreateEventA(nullptr, true, false, Name.c_str());
}

NamedEvent::NamedEvent(std::string_view EventName) : Event(nullptr)
{
	using namespace std::literals;

	ExtendableStringBuilder<64> Name;
	Name << "Local\\"sv;
	Name << EventName;

	m_EventHandle = CreateEventA(nullptr, true, false, Name.c_str());
}

Process::Process() = default;

void
Process::Initialize(void* ProcessHandle)
{
	ZEN_ASSERT(m_ProcessHandle == nullptr);
	// TODO: perform some debug verification here to verify it's a valid handle?
	m_ProcessHandle = ProcessHandle;
}

Process::~Process()
{
	if (IsValid())
	{
		CloseHandle(m_ProcessHandle);
		m_ProcessHandle = nullptr;
	}
}

void
Process::Initialize(int Pid)
{
	ZEN_ASSERT(m_ProcessHandle == nullptr);
	m_ProcessHandle = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, Pid);
	m_Pid			= Pid;
}

bool
Process::IsRunning() const
{
	DWORD ExitCode = 0;
	GetExitCodeProcess(m_ProcessHandle, &ExitCode);

	return ExitCode == STILL_ACTIVE;
}

bool
Process::IsValid() const
{
	return (m_ProcessHandle != nullptr) && (m_ProcessHandle != INVALID_HANDLE_VALUE);
}

void
Process::Terminate(int ExitCode)
{
	if (IsRunning())
	{
		TerminateProcess(m_ProcessHandle, ExitCode);
	}

	DWORD WaitResult = WaitForSingleObject(m_ProcessHandle, INFINITE);

	if (WaitResult != WAIT_OBJECT_0)
	{
		// What might go wrong here, and what is meaningful to act on?
	}
}

bool
Process::Wait(int TimeoutMs)
{
	const DWORD Timeout = (TimeoutMs < 0) ? INFINITE : TimeoutMs;

	const DWORD WaitResult = WaitForSingleObject(m_ProcessHandle, Timeout);

	switch (WaitResult)
	{
		case WAIT_OBJECT_0:
			return true;

		case WAIT_TIMEOUT:
			return false;

		case WAIT_FAILED:
			// What might go wrong here, and what is meaningful to act on?
			throw WindowsException("Process::Wait failed");
	}

	return false;
}

void
Sleep(int ms)
{
	::Sleep(ms);
}

//////////////////////////////////////////////////////////////////////////
//
// Testing related code follows...
//

void
thread_forcelink()
{
}

}  // namespace zen

