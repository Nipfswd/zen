// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <zencore/string.h>
#include <zencore/windows.h>
#include <string>

namespace zen {

class WindowsException : public std::exception
{
public:
	WindowsException(const char* Message)
	{
		m_hResult = HRESULT_FROM_WIN32(GetLastError());
		m_Message = Message;
	}

	WindowsException(HRESULT hRes, const char* Message)
	{
		m_hResult = hRes;
		m_Message = Message;
	}

	WindowsException(HRESULT hRes, const char* Message, const char* Detail)
	{
		m_hResult = hRes;

		ExtendableStringBuilder<128> msg;
		msg.Append(Message);
		msg.Append(" (detail: '");
		msg.Append(Detail);
		msg.Append("')");

		m_Message = msg.c_str();
	}

	virtual const char* what() const override { return m_Message.c_str(); }

private:
	std::string m_Message;
	HRESULT		m_hResult;
};

ZENCORE_API void ThrowSystemException(HRESULT hRes, const char* Message);
inline void
ThrowSystemException(const char* Message)
{
	throw WindowsException(Message);
}

inline void
ThrowIfFailed(HRESULT hRes, const char* Message)
{
	if (FAILED(hRes))
		ThrowSystemException(hRes, Message);
}

}  // namespace zen

