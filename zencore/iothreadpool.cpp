// Copyright Noah Games, Inc. All Rights Reserved.

#include "iothreadpool.h"

namespace zen {

WinIoThreadPool::WinIoThreadPool(int InThreadCount)
{
	// Thread pool setup

	m_ThreadPool = CreateThreadpool(NULL);

	SetThreadpoolThreadMinimum(m_ThreadPool, InThreadCount);
	SetThreadpoolThreadMaximum(m_ThreadPool, InThreadCount * 2);

	InitializeThreadpoolEnvironment(&m_CallbackEnvironment);

	m_CleanupGroup = CreateThreadpoolCleanupGroup();

	SetThreadpoolCallbackPool(&m_CallbackEnvironment, m_ThreadPool);

	SetThreadpoolCallbackCleanupGroup(&m_CallbackEnvironment, m_CleanupGroup, NULL);
}

WinIoThreadPool::~WinIoThreadPool()
{
	CloseThreadpool(m_ThreadPool);
}

void
WinIoThreadPool::CreateIocp(HANDLE IoHandle, PTP_WIN32_IO_CALLBACK Callback, void* Context)
{
	m_ThreadPoolIo = CreateThreadpoolIo(IoHandle, Callback, Context, &m_CallbackEnvironment);
}

}  // namespace zen

