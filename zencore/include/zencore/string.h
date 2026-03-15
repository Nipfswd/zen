// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include "intmath.h"
#include "zencore.h"

#include <stdint.h>
#include <string.h>
#include <charconv>
#include <codecvt>
#include <concepts>
#include <optional>
#include <span>
#include <string_view>

namespace zen {

//////////////////////////////////////////////////////////////////////////

inline bool
StringEquals(const char8_t* s1, const char* s2)
{
	return strcmp(reinterpret_cast<const char*>(s1), s2) == 0;
}

inline bool
StringEquals(const char* s1, const char* s2)
{
	return strcmp(s1, s2) == 0;
}

inline size_t
StringLength(const char* str)
{
	return strlen(str);
}

inline bool
StringEquals(const wchar_t* s1, const wchar_t* s2)
{
	return wcscmp(s1, s2) == 0;
}

inline size_t
StringLength(const wchar_t* str)
{
	return wcslen(str);
}

//////////////////////////////////////////////////////////////////////////
// File name helpers
//

ZENCORE_API const char* FilepathFindExtension(const std::string_view& path, const char* extensionToMatch = nullptr);

//////////////////////////////////////////////////////////////////////////
// Text formatting of numbers
//

ZENCORE_API bool ToString(std::span<char> Buffer, uint64_t Num);
ZENCORE_API bool ToString(std::span<char> Buffer, int64_t Num);

struct TextNumBase
{
	inline const char* c_str() const { return m_Buffer; }
	inline			   operator std::string_view() const { return std::string_view(m_Buffer); }

protected:
	char m_Buffer[24];
};

struct IntNum : public TextNumBase
{
	inline IntNum(std::unsigned_integral auto Number) { ToString(m_Buffer, uint64_t(Number)); }
	inline IntNum(std::signed_integral auto Number) { ToString(m_Buffer, int64_t(Number)); }
};

//////////////////////////////////////////////////////////////////////////
//
// Quick-and-dirty string builder. Good enough for me, but contains traps
// and not-quite-ideal behaviour especially when mixing character types etc
//

template<typename C>
class StringBuilderImpl
{
public:
	StringBuilderImpl() = default;
	ZENCORE_API ~StringBuilderImpl();

	StringBuilderImpl(const StringBuilderImpl&)	 = delete;
	StringBuilderImpl(const StringBuilderImpl&&) = delete;
	const StringBuilderImpl& operator=(const StringBuilderImpl&) = delete;
	const StringBuilderImpl& operator=(const StringBuilderImpl&&) = delete;

	StringBuilderImpl& Append(C OneChar)
	{
		EnsureCapacity(1);

		*m_CurPos++ = OneChar;

		return *this;
	}

	inline StringBuilderImpl& AppendAscii(const std::string_view& String)
	{
		const size_t len = String.size();

		EnsureCapacity(len);

		for (size_t i = 0; i < len; ++i)
			m_CurPos[i] = String[i];

		m_CurPos += len;

		return *this;
	}

	inline StringBuilderImpl& AppendAscii(const std::u8string_view& String)
	{
		const size_t len = String.size();

		EnsureCapacity(len);

		for (size_t i = 0; i < len; ++i)
			m_CurPos[i] = String[i];

		m_CurPos += len;

		return *this;
	}

	inline StringBuilderImpl& AppendAscii(const char* NulTerminatedString)
	{
		size_t StringLen = StringLength(NulTerminatedString);

		return AppendAscii({NulTerminatedString, StringLen});
	}

	inline StringBuilderImpl& Append(const char8_t* NulTerminatedString)
	{
		// This is super hacky and not fully functional - needs better
		// solution
		if constexpr (sizeof(C) == 1)
		{
			size_t len = StringLength((const char*)NulTerminatedString);

			EnsureCapacity(len);

			for (size_t i = 0; i < len; ++i)
				m_CurPos[i] = C(NulTerminatedString[i]);

			m_CurPos += len;
		}
		else
		{
			ZEN_NOT_IMPLEMENTED();
		}

		return *this;
	}

	inline StringBuilderImpl& AppendAsciiRange(const char* BeginString, const char* EndString)
	{
		EnsureCapacity(EndString - BeginString);

		while (BeginString != EndString)
			*m_CurPos++ = *BeginString++;

		return *this;
	}

	inline StringBuilderImpl& Append(const C* NulTerminatedString)
	{
		size_t Len = StringLength(NulTerminatedString);

		EnsureCapacity(Len);
		memcpy(m_CurPos, NulTerminatedString, Len * sizeof(C));
		m_CurPos += Len;

		return *this;
	}

	inline StringBuilderImpl& Append(const C* NulTerminatedString, size_t MaxChars)
	{
		size_t len = Min(MaxChars, StringLength(NulTerminatedString));

		EnsureCapacity(len);
		memcpy(m_CurPos, NulTerminatedString, len * sizeof(C));
		m_CurPos += len;

		return *this;
	}

	inline StringBuilderImpl& AppendRange(const C* BeginString, const C* EndString)
	{
		size_t Len = EndString - BeginString;

		EnsureCapacity(Len);
		memcpy(m_CurPos, BeginString, Len * sizeof(C));
		m_CurPos += Len;

		return *this;
	}

	inline StringBuilderImpl& Append(const std::basic_string_view<C>& String)
	{
		return AppendRange(String.data(), String.data() + String.size());
	}

	inline const C* c_str() const
	{
		EnsureNulTerminated();
		return m_Base;
	}

	inline C* Data()
	{
		EnsureNulTerminated();
		return m_Base;
	}

	inline const C* Data() const
	{
		EnsureNulTerminated();
		return m_Base;
	}

	inline size_t Size() const { return m_CurPos - m_Base; }
	inline bool	  IsDynamic() const { return m_IsDynamic; }
	inline void	  Reset() { m_CurPos = m_Base; }

	inline StringBuilderImpl& operator<<(uint64_t n)
	{
		IntNum Str(n);
		return AppendAscii(Str);
	}
	inline StringBuilderImpl& operator<<(int64_t n)
	{
		IntNum Str(n);
		return AppendAscii(Str);
	}
	inline StringBuilderImpl& operator<<(uint32_t n)
	{
		IntNum Str(n);
		return AppendAscii(Str);
	}
	inline StringBuilderImpl& operator<<(int32_t n)
	{
		IntNum Str(n);
		return AppendAscii(Str);
	}
	inline StringBuilderImpl& operator<<(uint16_t n)
	{
		IntNum Str(n);
		return AppendAscii(Str);
	}
	inline StringBuilderImpl& operator<<(int16_t n)
	{
		IntNum Str(n);
		return AppendAscii(Str);
	}
	inline StringBuilderImpl& operator<<(uint8_t n)
	{
		IntNum Str(n);
		return AppendAscii(Str);
	}
	inline StringBuilderImpl& operator<<(int8_t n)
	{
		IntNum Str(n);
		return AppendAscii(Str);
	}

	inline StringBuilderImpl& operator<<(const char* str) { return AppendAscii(str); }
	inline StringBuilderImpl& operator<<(const std::string_view str) { return AppendAscii(str); }
	inline StringBuilderImpl& operator<<(const std::u8string_view str) { return AppendAscii(str); }
	inline StringBuilderImpl& operator<<(bool v)
	{
		using namespace std::literals;
		if (v)
		{
			return AppendAscii("true"sv);
		}
		return AppendAscii("false"sv);
	}

protected:
	inline void Init(C* Base, size_t Capacity)
	{
		m_Base = m_CurPos = Base;
		m_End			  = Base + Capacity;
	}

	inline void EnsureNulTerminated() const { *m_CurPos = '\0'; }

	inline void EnsureCapacity(size_t ExtraRequired)
	{
		// precondition: we know the current buffer has enough capacity
		// for the existing string including NUL terminator

		if ((m_CurPos + ExtraRequired) < m_End)
			return;

		Extend(ExtraRequired);
	}

	ZENCORE_API void  Extend(size_t ExtraCapacity);
	ZENCORE_API void* AllocBuffer(size_t ByteCount);
	ZENCORE_API void  FreeBuffer(void* Buffer, size_t ByteCount);

	ZENCORE_API [[noreturn]] void Fail(const char* FailReason);	 // note: throws exception

	C*	 m_Base;
	C*	 m_CurPos;
	C*	 m_End;
	bool m_IsDynamic	= false;
	bool m_IsExtendable = false;
};

//////////////////////////////////////////////////////////////////////////

extern template StringBuilderImpl<char>;

class StringBuilderBase : public StringBuilderImpl<char>
{
public:
	inline StringBuilderBase(char* bufferPointer, size_t bufferCapacity) { Init(bufferPointer, bufferCapacity); }
	inline ~StringBuilderBase() = default;

	// Note that we don't need a terminator for the string_view so we avoid calling data() here
	inline					operator std::string_view() const { return std::string_view(m_Base, m_CurPos - m_Base); }
	inline std::string_view ToView() const { return std::string_view(m_Base, m_CurPos - m_Base); }
	inline std::string		ToString() const { return std::string{Data(), Size()}; }

	inline void AppendCodepoint(uint32_t cp)
	{
		if (cp < 0x80)	// one octet
		{
			Append(static_cast<char8_t>(cp));
		}
		else if (cp < 0x800)
		{
			EnsureCapacity(2);	// two octets
			m_CurPos[0] = static_cast<char8_t>((cp >> 6) | 0xc0);
			m_CurPos[1] = static_cast<char8_t>((cp & 0x3f) | 0x80);
			m_CurPos += 2;
		}
		else if (cp < 0x10000)
		{
			EnsureCapacity(3);	// three octets
			m_CurPos[0] = static_cast<char8_t>((cp >> 12) | 0xe0);
			m_CurPos[1] = static_cast<char8_t>(((cp >> 6) & 0x3f) | 0x80);
			m_CurPos[2] = static_cast<char8_t>((cp & 0x3f) | 0x80);
			m_CurPos += 3;
		}
		else
		{
			EnsureCapacity(4);	// four octets
			m_CurPos[0] = static_cast<char8_t>((cp >> 18) | 0xf0);
			m_CurPos[1] = static_cast<char8_t>(((cp >> 12) & 0x3f) | 0x80);
			m_CurPos[2] = static_cast<char8_t>(((cp >> 6) & 0x3f) | 0x80);
			m_CurPos[3] = static_cast<char8_t>((cp & 0x3f) | 0x80);
			m_CurPos += 4;
		}
	}
};

template<size_t N>
class StringBuilder : public StringBuilderBase
{
public:
	inline StringBuilder() : StringBuilderBase(m_StringBuffer, sizeof m_StringBuffer) {}
	inline ~StringBuilder() = default;

private:
	char m_StringBuffer[N];
};

template<size_t N>
class ExtendableStringBuilder : public StringBuilderBase
{
public:
	inline ExtendableStringBuilder() : StringBuilderBase(m_StringBuffer, sizeof m_StringBuffer) { m_IsExtendable = true; }
	inline ~ExtendableStringBuilder() = default;

private:
	char m_StringBuffer[N];
};

//////////////////////////////////////////////////////////////////////////

extern template StringBuilderImpl<wchar_t>;

class WideStringBuilderBase : public StringBuilderImpl<wchar_t>
{
public:
	inline WideStringBuilderBase(wchar_t* BufferPointer, size_t BufferCapacity) { Init(BufferPointer, BufferCapacity); }
	inline ~WideStringBuilderBase() = default;

	inline					 operator std::wstring_view() const { return std::wstring_view{Data(), Size()}; }
	inline std::wstring_view ToView() const { return std::wstring_view{Data(), Size()}; }
	inline std::wstring		 toString() const { return std::wstring{Data(), Size()}; }

	inline StringBuilderImpl& operator<<(const std::u16string_view str) { return Append((const wchar_t*)str.data(), str.size()); }
	inline StringBuilderImpl& operator<<(const wchar_t* str) { return Append(str); }
	using StringBuilderImpl:: operator<<;
};

template<size_t N>
class WideStringBuilder : public WideStringBuilderBase
{
public:
	inline WideStringBuilder() : WideStringBuilderBase(m_Buffer, N) {}
	~WideStringBuilder() = default;

private:
	wchar_t m_Buffer[N];
};

template<size_t N>
class ExtendableWideStringBuilder : public WideStringBuilderBase
{
public:
	inline ExtendableWideStringBuilder() : WideStringBuilderBase(m_Buffer, N) { m_IsExtendable = true; }
	~ExtendableWideStringBuilder() = default;

private:
	wchar_t m_Buffer[N];
};

//////////////////////////////////////////////////////////////////////////

void		 Utf8ToWide(const char8_t* str, WideStringBuilderBase& out);
void		 Utf8ToWide(const std::u8string_view& wstr, WideStringBuilderBase& out);
void		 Utf8ToWide(const std::string_view& wstr, WideStringBuilderBase& out);
std::wstring Utf8ToWide(const std::string_view& wstr);

void		WideToUtf8(const wchar_t* wstr, StringBuilderBase& out);
std::string WideToUtf8(const wchar_t* wstr);
void		WideToUtf8(const std::u16string_view& wstr, StringBuilderBase& out);
void		WideToUtf8(const std::wstring_view& wstr, StringBuilderBase& out);
std::string WideToUtf8(const std::wstring_view Wstr);

/// <summary>
/// Parse hex string into a byte buffer
/// </summary>
/// <param name="string">Input string</param>
/// <param name="characterCount">Number of characters in string</param>
/// <param name="outPtr">Pointer to output buffer</param>
/// <returns>true if the input consisted of all valid hexadecimal characters</returns>

inline bool
ParseHexBytes(const char* InputString, size_t CharacterCount, uint8_t* OutPtr)
{
	ZEN_ASSERT((CharacterCount & 1) == 0);

	auto char2nibble = [](char c) {
		uint8_t c8 = uint8_t(c - '0');

		if (c8 < 10)
			return c8;

		c8 -= 'A' - '0' - 10;

		if (c8 < 16)
			return c8;

		c8 -= 'a' - 'A';

		if (c8 < 16)
			return c8;

		return uint8_t(0xff);
	};

	uint8_t allBits = 0;

	while (CharacterCount)
	{
		uint8_t n0 = char2nibble(InputString[0]);
		uint8_t n1 = char2nibble(InputString[1]);

		allBits |= n0 | n1;

		*OutPtr = (n0 << 4) | n1;

		OutPtr += 1;
		InputString += 2;
		CharacterCount -= 2;
	}

	return (allBits & 0x80) == 0;
}

inline void
ToHexBytes(const uint8_t* InputData, size_t ByteCount, char* OutString)
{
	const char hexchars[] = "0123456789abcdef";

	while (ByteCount--)
	{
		uint8_t byte = *InputData++;

		*OutString++ = hexchars[byte >> 4];
		*OutString++ = hexchars[byte & 15];
	}
}

//////////////////////////////////////////////////////////////////////////
// Format numbers for humans
//

ZENCORE_API size_t NiceNumToBuffer(uint64_t Num, std::span<char> Buffer);
ZENCORE_API size_t NiceBytesToBuffer(uint64_t Num, std::span<char> Buffer);
ZENCORE_API size_t NiceByteRateToBuffer(uint64_t Num, uint64_t ms, std::span<char> Buffer);
ZENCORE_API size_t NiceLatencyNsToBuffer(uint64_t NanoSeconds, std::span<char> Buffer);
ZENCORE_API size_t NiceTimeSpanMsToBuffer(uint64_t Milliseconds, std::span<char> Buffer);

struct NiceBase
{
	inline const char* c_str() const { return m_Buffer; }
	inline			   operator std::string_view() const { return std::string_view(m_Buffer); }

protected:
	char m_Buffer[16];
};

struct NiceNum : public NiceBase
{
	inline NiceNum(uint64_t Num) { NiceNumToBuffer(Num, m_Buffer); }
};

struct NiceBytes : public NiceBase
{
	inline NiceBytes(uint64_t Num) { NiceBytesToBuffer(Num, m_Buffer); }
};

struct NiceByteRate : public NiceBase
{
	inline NiceByteRate(uint64_t Bytes, uint64_t TimeMilliseconds) { NiceByteRateToBuffer(Bytes, TimeMilliseconds, m_Buffer); }
};

struct NiceLatencyNs : public NiceBase
{
	inline NiceLatencyNs(uint64_t Milliseconds) { NiceLatencyNsToBuffer(Milliseconds, m_Buffer); }
};

struct NiceTimeSpanMs : public NiceBase
{
	inline NiceTimeSpanMs(uint64_t Milliseconds) { NiceTimeSpanMsToBuffer(Milliseconds, m_Buffer); }
};

//////////////////////////////////////////////////////////////////////////

inline std::string
NiceRate(uint64_t Num, uint32_t DurationMilliseconds, const char* Unit = "B")
{
	char Buffer[32];

	if (DurationMilliseconds)
	{
		NiceNumToBuffer(Num * 1000 / DurationMilliseconds, Buffer);
	}
	else
	{
		strcpy_s(Buffer, "0");
	}

	strcat_s(Buffer, Unit);
	strcat_s(Buffer, "/s");

	return Buffer;
}

//////////////////////////////////////////////////////////////////////////

template<std::integral T>
std::optional<T>
ParseInt(const std::string_view& Input)
{
	T							 Out;
	const std::from_chars_result Result = std::from_chars(Input.data(), Input.data() + Input.size(), Out);
	if (Result.ec == std::errc::invalid_argument || Result.ec == std::errc::result_out_of_range)
	{
		return std::nullopt;
	}
	return Out;
}

//////////////////////////////////////////////////////////////////////////

void string_forcelink();  // internal

}  // namespace zen

