// Copyright Noah Games, Inc. All Rights Reserved.

#include "projectclient.h"

#include <zencore/compactbinary.h>
#include <zencore/sharedbuffer.h>
#include <zencore/string.h>
#include <zencore/zencore.h>

#include <spdlog/spdlog.h>
#include <asio.hpp>
#include <gsl/gsl-lite.hpp>

#include <atlbase.h>

namespace zen {

using namespace fmt::literals;

struct ProjectClientConnection
{
	ProjectClientConnection(int BasePort) { Connect(BasePort); }

	void Connect(int BasePort)
	{
		WideStringBuilder<64> PipeName;
		PipeName << "\\\\.\\pipe\\zenprj";	// TODO: this should use an instance-specific identifier!

		HANDLE hPipe = CreateFileW(PipeName.c_str(),
								   GENERIC_READ | GENERIC_WRITE,
								   0,			   // Sharing doesn't make any sense
								   nullptr,		   // No security attributes
								   OPEN_EXISTING,  // Open existing pipe
								   0,			   // Attributes
								   nullptr		   // Template file
		);

		if (hPipe == INVALID_HANDLE_VALUE)
		{
			spdlog::warn("failed while creating named pipe {}", WideToUtf8(PipeName));

			throw std::system_error(GetLastError(), std::system_category(), "Failed to open named pipe '{}'"_format(WideToUtf8(PipeName)));
		}

		// Change to message mode
		DWORD dwMode  = PIPE_READMODE_MESSAGE;
		BOOL  Success = SetNamedPipeHandleState(hPipe, &dwMode, nullptr, nullptr);

		if (!Success)
		{
			throw std::system_error(GetLastError(),
									std::system_category(),
									"Failed to change named pipe '{}' to message mode"_format(WideToUtf8(PipeName)));
		}

		m_hPipe.Attach(hPipe);	// This now owns the handle and will close it
	}

	~ProjectClientConnection() {}

	CbObject MessageTransaction(CbObject Request)
	{
		DWORD dwWrittenBytes = 0;

		MemoryView View = Request.GetView();

		BOOL Success = ::WriteFile(m_hPipe, View.GetData(), gsl::narrow_cast<DWORD>(View.GetSize()), &dwWrittenBytes, nullptr);

		if (!Success)
		{
			throw std::system_error(GetLastError(), std::system_category(), "Failed to write pipe message");
		}

		ZEN_ASSERT(dwWrittenBytes == View.GetSize());

		DWORD dwReadBytes = 0;

		Success = ReadFile(m_hPipe, m_Buffer, sizeof m_Buffer, &dwReadBytes, nullptr);

		if (!Success)
		{
			DWORD ErrorCode = GetLastError();

			if (ERROR_MORE_DATA == ErrorCode)
			{
				// Response message is larger than our buffer - handle it by allocating a larger
				// buffer on the heap and read the remainder into that buffer

				DWORD dwBytesAvail = 0, dwLeftThisMessage = 0;

				Success = PeekNamedPipe(m_hPipe, nullptr, 0, nullptr, &dwBytesAvail, &dwLeftThisMessage);

				if (Success)
				{
					UniqueBuffer MessageBuffer = UniqueBuffer::Alloc(dwReadBytes + dwLeftThisMessage);

					memcpy(MessageBuffer.GetData(), m_Buffer, dwReadBytes);

					Success = ReadFile(m_hPipe,
									   reinterpret_cast<uint8_t*>(MessageBuffer.GetData()) + dwReadBytes,
									   dwLeftThisMessage,
									   &dwReadBytes,
									   nullptr);

					if (Success)
					{
						return CbObject(SharedBuffer(std::move(MessageBuffer)));
					}
				}
			}

			throw std::system_error(GetLastError(), std::system_category(), "Failed to read pipe message");
		}

		return CbObject(SharedBuffer::MakeView(MakeMemoryView(m_Buffer)));
	}

private:
	static const int kEmbeddedBufferSize = 512 - 16;

	CHandle m_hPipe;
	uint8_t m_Buffer[kEmbeddedBufferSize];
};

struct LocalProjectClient::ClientImpl
{
	ClientImpl(int BasePort) : m_BasePort(BasePort) {}
	~ClientImpl() {}

	void Start() {}
	void Stop() {}

	inline int BasePort() const { return m_BasePort; }

private:
	int m_BasePort = 0;
};

LocalProjectClient::LocalProjectClient(int BasePort)
{
	m_Impl = std::make_unique<ClientImpl>(BasePort);
	m_Impl->Start();
}

LocalProjectClient::~LocalProjectClient()
{
	m_Impl->Stop();
}

CbObject
LocalProjectClient::MessageTransaction(CbObject Request)
{
	ProjectClientConnection Cx(m_Impl->BasePort());

	return Cx.MessageTransaction(Request);
}

}  // namespace zen

