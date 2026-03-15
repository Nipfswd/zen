// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <zencore/windows.h>

namespace zen {

//////////////////////////////////////////////////////////////////////////
//
// Thread pool. Implemented in terms of Windows thread pool right now, will
//				need a cross-platform implementation eventually
//

class WinIoThreadPool
{
public:
	WinIoThreadPool(int InThreadCount);
	~WinIoThreadPool();

	void		  CreateIocp(HANDLE IoHandle, PTP_WIN32_IO_CALLBACK Callback, void* Context);
	inline PTP_IO Iocp() const { return m_ThreadPoolIo; }

private:
	PTP_POOL			m_ThreadPool   = nullptr;
	PTP_CLEANUP_GROUP	m_CleanupGroup = nullptr;
	PTP_IO				m_ThreadPoolIo = nullptr;
	TP_CALLBACK_ENVIRON m_CallbackEnvironment;
};

}  // namespace zen

