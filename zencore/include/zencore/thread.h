// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include "zencore.h"

namespace zen {

/**
 * Reader-writer lock
 *
 * - A single thread may hold an exclusive lock at any given moment
 *
 * - Multiple threads may hold shared locks, but only if no thread has
 *   acquired an exclusive lock
 */
class RwLock
{
public:
	ZENCORE_API void AcquireShared();
	ZENCORE_API void ReleaseShared();

	ZENCORE_API void AcquireExclusive();
	ZENCORE_API void ReleaseExclusive();

	struct SharedLockScope
	{
		SharedLockScope(RwLock& lock) : m_Lock(lock) { m_Lock.AcquireShared(); }
		~SharedLockScope() { m_Lock.ReleaseShared(); }

	private:
		RwLock& m_Lock;
	};

	struct ExclusiveLockScope
	{
		ExclusiveLockScope(RwLock& lock) : m_Lock(lock) { m_Lock.AcquireExclusive(); }
		~ExclusiveLockScope() { m_Lock.ReleaseExclusive(); }

	private:
		RwLock& m_Lock;
	};

private:
	void* m_Srw = nullptr;
};

/** Basic abstraction of a simple event synchronization mechanism (aka 'binary semaphore')
 */
class Event
{
public:
	ZENCORE_API Event();
	ZENCORE_API ~Event();

	Event(Event&& Rhs) : m_EventHandle(Rhs.m_EventHandle) { Rhs.m_EventHandle = nullptr; }

	Event(const Event& Rhs) = delete;
	Event& operator=(const Event& Rhs) = delete;

	inline Event& operator=(Event&& Rhs)
	{
		m_EventHandle	  = Rhs.m_EventHandle;
		Rhs.m_EventHandle = nullptr;
		return *this;
	}

	ZENCORE_API void Set();
	ZENCORE_API void Reset();
	ZENCORE_API bool Wait(int TimeoutMs = -1);

protected:
	explicit Event(void* EventHandle) : m_EventHandle(EventHandle) {}

	void* m_EventHandle = nullptr;
};

/** Basic abstraction of an IPC mechanism (aka 'binary semaphore')
 */
class NamedEvent : public Event
{
public:
	ZENCORE_API explicit NamedEvent(std::string_view EventName);
	ZENCORE_API explicit NamedEvent(std::u8string_view EventName);
};

/** Basic process abstraction
 */
class Process
{
public:
	ZENCORE_API Process();

	Process(const Process&) = delete;
	Process& operator=(const Process&) = delete;

	ZENCORE_API ~Process();

	ZENCORE_API void Initialize(int Pid);
	ZENCORE_API void Initialize(void* ProcessHandle);  /// Initialize with an existing handle - takes ownership of the handle
	ZENCORE_API bool IsRunning() const;
	ZENCORE_API bool IsValid() const;
	ZENCORE_API bool Wait(int TimeoutMs = -1);
	ZENCORE_API void Terminate(int ExitCode);
	inline int		 Pid() const { return m_Pid; }

private:
	void* m_ProcessHandle = nullptr;
	int	  m_Pid			  = 0;
};

ZENCORE_API bool IsProcessRunning(int pid);

ZENCORE_API void Sleep(int ms);

void thread_forcelink();  // internal

}  // namespace zen

