// Copyright Noah Games, Inc. All Rights Reserved.

#include <doctest/doctest.h>
#include <inttypes.h>
#include <stdio.h>
#include <zencore/memory.h>
#include <zencore/string.h>
#include <exception>
#include <ostream>

#include <utf8.h>

template<typename u16bit_iterator>
void
utf16to8_impl(u16bit_iterator StartIt, u16bit_iterator EndIt, ::zen::StringBuilderBase& OutString)
{
	while (StartIt != EndIt)
	{
		uint32_t cp = utf8::internal::mask16(*StartIt++);
		// Take care of surrogate pairs first
		if (utf8::internal::is_lead_surrogate(cp))
		{
			uint32_t trail_surrogate = utf8::internal::mask16(*StartIt++);
			cp						 = (cp << 10) + trail_surrogate + utf8::internal::SURROGATE_OFFSET;
		}
		OutString.AppendCodepoint(cp);
	}
}

//////////////////////////////////////////////////////////////////////////

namespace zen {

bool
ToString(std::span<char> Buffer, uint64_t Num)
{
	snprintf(Buffer.data(), Buffer.size(), "%I64u", Num);

	return true;
}
bool
ToString(std::span<char> Buffer, int64_t Num)
{
	snprintf(Buffer.data(), Buffer.size(), "%I64d", Num);

	return true;
}

//////////////////////////////////////////////////////////////////////////

const char*
FilepathFindExtension(const std::string_view& Path, const char* ExtensionToMatch)
{
	const size_t PathLen = Path.size();

	if (ExtensionToMatch)
	{
		size_t ExtLen = strlen(ExtensionToMatch);

		if (ExtLen > PathLen)
			return nullptr;

		const char* PathExtension = Path.data() + PathLen - ExtLen;

		if (StringEquals(PathExtension, ExtensionToMatch))
			return PathExtension;

		return nullptr;
	}

	if (PathLen == 0)
		return nullptr;

	// Look for extension introducer ('.')

	for (size_t i = PathLen - 1; i >= 0; --i)
	{
		if (Path[i] == '.')
			return Path.data() + i;
	}

	return nullptr;
}

//////////////////////////////////////////////////////////////////////////

void
Utf8ToWide(const char8_t* Str8, WideStringBuilderBase& OutString)
{
	Utf8ToWide(std::u8string_view(Str8), OutString);
}

void
Utf8ToWide(const std::string_view& Str8, WideStringBuilderBase& OutString)
{
	Utf8ToWide(std::u8string_view{reinterpret_cast<const char8_t*>(Str8.data()), Str8.size()}, OutString);
}

std::wstring
Utf8ToWide(const std::string_view& Wstr)
{
	ExtendableWideStringBuilder<128> String;
	Utf8ToWide(Wstr, String);

	return String.c_str();
}

void
Utf8ToWide(const std::u8string_view& Str8, WideStringBuilderBase& OutString)
{
	const char*	 str	= (const char*)Str8.data();
	const size_t strLen = Str8.size();

	const char* endStr		   = str + strLen;
	size_t		ByteCount	   = 0;
	size_t		CurrentOutChar = 0;

	for (; str != endStr; ++str)
	{
		unsigned char Data = static_cast<unsigned char>(*str);

		if (!(Data & 0x80))
		{
			// ASCII
			OutString.Append(wchar_t(Data));
			continue;
		}
		else if (!ByteCount)
		{
			// Start of multi-byte sequence. Figure out how
			// many bytes we're going to consume

			size_t Count = 0;

			for (size_t Temp = Data; Temp & 0x80; Temp <<= 1)
				++Count;

			ByteCount	   = Count - 1;
			CurrentOutChar = Data & (0xff >> (Count + 1));
		}
		else
		{
			--ByteCount;

			if ((Data & 0xc0) != 0x80)
			{
				break;
			}

			CurrentOutChar = (CurrentOutChar << 6) | (Data & 0x3f);

			if (!ByteCount)
			{
				OutString.Append(wchar_t(CurrentOutChar));
				CurrentOutChar = 0;
			}
		}
	}
}

void
WideToUtf8(const wchar_t* Wstr, StringBuilderBase& OutString)
{
	WideToUtf8(std::u16string_view{(char16_t*)Wstr}, OutString);
}

void
WideToUtf8(const std::wstring_view& Wstr, StringBuilderBase& OutString)
{
	WideToUtf8(std::u16string_view{(char16_t*)Wstr.data(), Wstr.size()}, OutString);
}

void
WideToUtf8(const std::u16string_view& Wstr, StringBuilderBase& OutString)
{
	utf16to8_impl(begin(Wstr), end(Wstr), OutString);
}

std::string
WideToUtf8(const wchar_t* Wstr)
{
	ExtendableStringBuilder<128> String;
	WideToUtf8(std::u16string_view{(char16_t*)Wstr}, String);

	return String.c_str();
}

std::string
WideToUtf8(const std::wstring_view Wstr)
{
	ExtendableStringBuilder<128> String;
	WideToUtf8(std::u16string_view{(char16_t*)Wstr.data(), Wstr.size()}, String);

	return String.c_str();
}

//////////////////////////////////////////////////////////////////////////

enum NicenumFormat
{
	kNicenum1024	= 0,  // Print kilo, mega, tera, peta, exa..
	kNicenumBytes	= 1,  // Print single bytes ("13B"), kilo, mega, tera...
	kNicenumTime	= 2,  // Print nanosecs, microsecs, millisecs, seconds...
	kNicenumRaw		= 3,  // Print the raw number without any formatting
	kNicenumRawTime = 4	  // Same as RAW, but print dashes ('-') for zero.
};

namespace {
	static const char* UnitStrings[3][7] = {
		/* kNicenum1024  */ {"", "K", "M", "G", "T", "P", "E"},
		/* kNicenumBytes */ {"B", "K", "M", "G", "T", "P", "E"},
		/* kNicenumTime  */ {"ns", "us", "ms", "s", "?", "?", "?"}};

	static const int UnitsLen[] = {
		/* kNicenum1024  */ 6,
		/* kNicenumBytes */ 6,
		/* kNicenumTime  */ 3};

	static const uint64_t KiloUnit[] = {
		/* kNicenum1024  */ 1024,
		/* kNicenumBytes */ 1024,
		/* kNicenumTime  */ 1000};
}  // namespace

/*
 * Convert a number to an appropriately human-readable output.
 */
int
NiceNumGeneral(uint64_t Num, std::span<char> Buffer, NicenumFormat Format)
{
	switch (Format)
	{
		case kNicenumRaw:
			return snprintf(Buffer.data(), Buffer.size(), "%llu", (uint64_t)Num);

		case kNicenumRawTime:
			if (Num > 0)
			{
				return snprintf(Buffer.data(), Buffer.size(), "%llu", (uint64_t)Num);
			}
			else
			{
				return snprintf(Buffer.data(), Buffer.size(), "%s", "-");
			}
			break;

		case kNicenum1024:
		case kNicenumBytes:
		case kNicenumTime:
		default:
			break;
	}

	// Bring into range and select unit

	int		 Index = 0;
	uint64_t n	   = Num;

	{
		const uint64_t Unit		= KiloUnit[Format];
		const int	   maxIndex = UnitsLen[Format];

		while (n >= Unit && Index < maxIndex)
		{
			n /= Unit;
			Index++;
		}
	}

	const char* u = UnitStrings[Format][Index];

	if ((Index == 0) || ((Num % (uint64_t)powl((int)KiloUnit[Format], Index)) == 0))
	{
		/*
		 * If this is an even multiple of the base, always display
		 * without any decimal precision.
		 */
		return snprintf(Buffer.data(), Buffer.size(), "%llu%s", (uint64_t)n, u);
	}
	else
	{
		/*
		 * We want to choose a precision that reflects the best choice
		 * for fitting in 5 characters.  This can get rather tricky when
		 * we have numbers that are very close to an order of magnitude.
		 * For example, when displaying 10239 (which is really 9.999K),
		 * we want only a single place of precision for 10.0K.  We could
		 * develop some complex heuristics for this, but it's much
		 * easier just to try each combination in turn.
		 */

		int StrLen = 0;

		for (int i = 2; i >= 0; i--)
		{
			double Value = (double)Num / (uint64_t)powl((int)KiloUnit[Format], Index);

			/*
			 * Don't print floating point values for time.  Note,
			 * we use floor() instead of round() here, since
			 * round can result in undesirable results.  For
			 * example, if "num" is in the range of
			 * 999500-999999, it will print out "1000us".  This
			 * doesn't happen if we use floor().
			 */
			if (Format == kNicenumTime)
			{
				StrLen = snprintf(Buffer.data(), Buffer.size(), "%d%s", (unsigned int)floor(Value), u);

				if (StrLen <= 5)
					break;
			}
			else
			{
				StrLen = snprintf(Buffer.data(), Buffer.size(), "%.*f%s", i, Value, u);

				if (StrLen <= 5)
					break;
			}
		}

		return StrLen;
	}
}

size_t
NiceNumToBuffer(uint64_t Num, std::span<char> Buffer)
{
	return NiceNumGeneral(Num, Buffer, kNicenum1024);
}

size_t
NiceBytesToBuffer(uint64_t Num, std::span<char> Buffer)
{
	return NiceNumGeneral(Num, Buffer, kNicenumBytes);
}

size_t
NiceByteRateToBuffer(uint64_t Num, uint64_t ElapsedMs, std::span<char> Buffer)
{
	size_t n = NiceNumGeneral(Num * 1000 / ElapsedMs, Buffer, kNicenumBytes);

	Buffer[n++] = '/';
	Buffer[n++] = 's';
	Buffer[n++] = '\0';

	return n;
}

size_t
NiceLatencyNsToBuffer(uint64_t Nanos, std::span<char> Buffer)
{
	return NiceNumGeneral(Nanos, Buffer, kNicenumTime);
}

size_t
NiceTimeSpanMsToBuffer(uint64_t Millis, std::span<char> Buffer)
{
	if (Millis < 1000)
	{
		return snprintf(Buffer.data(), Buffer.size(), "%" PRIu64 "ms", Millis);
	}
	else if (Millis < 10000)
	{
		return snprintf(Buffer.data(), Buffer.size(), "%.2fs", Millis / 1000.0);
	}
	else if (Millis < 60000)
	{
		return snprintf(Buffer.data(), Buffer.size(), "%.1fs", Millis / 1000.0);
	}
	else if (Millis < 60 * 60000)
	{
		return snprintf(Buffer.data(), Buffer.size(), "%" PRIu64 "m%02" PRIu64 "s", Millis / 60000, (Millis / 1000) % 60);
	}
	else
	{
		return snprintf(Buffer.data(), Buffer.size(), "%" PRIu64 "h%02" PRIu64 "m", Millis / 3600000, (Millis / 60000) % 60);
	}
}

//////////////////////////////////////////////////////////////////////////

template<typename C>
StringBuilderImpl<C>::~StringBuilderImpl()
{
	if (m_IsDynamic)
	{
		FreeBuffer(m_Base, m_End - m_Base);
	}
}

template<typename C>
void
StringBuilderImpl<C>::Extend(size_t extraCapacity)
{
	if (!m_IsExtendable)
	{
		Fail("exceeded capacity");
	}

	const size_t oldCapacity = m_End - m_Base;
	const size_t newCapacity = NextPow2(oldCapacity + extraCapacity);

	C* newBase = (C*)AllocBuffer(newCapacity);

	size_t pos = m_CurPos - m_Base;
	memcpy(newBase, m_Base, pos * sizeof(C));

	if (m_IsDynamic)
	{
		FreeBuffer(m_Base, oldCapacity);
	}

	m_Base		= newBase;
	m_CurPos	= newBase + pos;
	m_End		= newBase + newCapacity;
	m_IsDynamic = true;
}

template<typename C>
void*
StringBuilderImpl<C>::AllocBuffer(size_t byteCount)
{
	return Memory::Alloc(byteCount * sizeof(C));
}

template<typename C>
void
StringBuilderImpl<C>::FreeBuffer(void* buffer, size_t byteCount)
{
	ZEN_UNUSED(byteCount);

	Memory::Free(buffer);
}

template<typename C>
[[noreturn]] void
StringBuilderImpl<C>::Fail(const char* reason)
{
	throw std::exception(reason);
}

// Instantiate templates once

template class StringBuilderImpl<char>;
template class StringBuilderImpl<wchar_t>;

//////////////////////////////////////////////////////////////////////////
//
// Unit tests
//

TEST_CASE("niceNum")
{
	char Buffer[16];

	SUBCASE("raw")
	{
		NiceNumGeneral(1, Buffer, kNicenumRaw);
		CHECK(StringEquals(Buffer, "1"));

		NiceNumGeneral(10, Buffer, kNicenumRaw);
		CHECK(StringEquals(Buffer, "10"));

		NiceNumGeneral(100, Buffer, kNicenumRaw);
		CHECK(StringEquals(Buffer, "100"));

		NiceNumGeneral(1000, Buffer, kNicenumRaw);
		CHECK(StringEquals(Buffer, "1000"));

		NiceNumGeneral(10000, Buffer, kNicenumRaw);
		CHECK(StringEquals(Buffer, "10000"));

		NiceNumGeneral(100000, Buffer, kNicenumRaw);
		CHECK(StringEquals(Buffer, "100000"));
	}

	SUBCASE("1024")
	{
		NiceNumGeneral(1, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "1"));

		NiceNumGeneral(10, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "10"));

		NiceNumGeneral(100, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "100"));

		NiceNumGeneral(1000, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "1000"));

		NiceNumGeneral(10000, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "9.77K"));

		NiceNumGeneral(100000, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "97.7K"));

		NiceNumGeneral(1000000, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "977K"));

		NiceNumGeneral(10000000, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "9.54M"));

		NiceNumGeneral(100000000, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "95.4M"));

		NiceNumGeneral(1000000000, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "954M"));

		NiceNumGeneral(10000000000, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "9.31G"));

		NiceNumGeneral(100000000000, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "93.1G"));

		NiceNumGeneral(1000000000000, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "931G"));

		NiceNumGeneral(10000000000000, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "9.09T"));

		NiceNumGeneral(100000000000000, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "90.9T"));

		NiceNumGeneral(1000000000000000, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "909T"));

		NiceNumGeneral(10000000000000000, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "8.88P"));

		NiceNumGeneral(100000000000000000, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "88.8P"));

		NiceNumGeneral(1000000000000000000, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "888P"));

		NiceNumGeneral(10000000000000000000, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "8.67E"));

		// pow2

		NiceNumGeneral(0, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "0"));

		NiceNumGeneral(1, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "1"));

		NiceNumGeneral(1024, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "1K"));

		NiceNumGeneral(1024 * 1024, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "1M"));

		NiceNumGeneral(1024 * 1024 * 1024, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "1G"));

		NiceNumGeneral(1024llu * 1024 * 1024 * 1024, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "1T"));

		NiceNumGeneral(1024llu * 1024 * 1024 * 1024 * 1024, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "1P"));

		NiceNumGeneral(1024llu * 1024 * 1024 * 1024 * 1024 * 1024, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "1E"));

		// pow2-1

		NiceNumGeneral(1023, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "1023"));

		NiceNumGeneral(2047, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "2.00K"));

		NiceNumGeneral(9 * 1024 - 1, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "9.00K"));

		NiceNumGeneral(10 * 1024 - 1, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "10.0K"));

		NiceNumGeneral(10 * 1024 - 5, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "10.0K"));

		NiceNumGeneral(10 * 1024 - 6, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "9.99K"));

		NiceNumGeneral(10 * 1024 - 10, Buffer, kNicenum1024);
		CHECK(StringEquals(Buffer, "9.99K"));
	}

	SUBCASE("time")
	{
		NiceNumGeneral(1, Buffer, kNicenumTime);
		CHECK(StringEquals(Buffer, "1ns"));

		NiceNumGeneral(100, Buffer, kNicenumTime);
		CHECK(StringEquals(Buffer, "100ns"));

		NiceNumGeneral(1000, Buffer, kNicenumTime);
		CHECK(StringEquals(Buffer, "1us"));

		NiceNumGeneral(10000, Buffer, kNicenumTime);
		CHECK(StringEquals(Buffer, "10us"));

		NiceNumGeneral(100000, Buffer, kNicenumTime);
		CHECK(StringEquals(Buffer, "100us"));

		NiceNumGeneral(1000000, Buffer, kNicenumTime);
		CHECK(StringEquals(Buffer, "1ms"));

		NiceNumGeneral(10000000, Buffer, kNicenumTime);
		CHECK(StringEquals(Buffer, "10ms"));

		NiceNumGeneral(100000000, Buffer, kNicenumTime);
		CHECK(StringEquals(Buffer, "100ms"));

		NiceNumGeneral(1000000000, Buffer, kNicenumTime);
		CHECK(StringEquals(Buffer, "1s"));

		NiceNumGeneral(10000000000, Buffer, kNicenumTime);
		CHECK(StringEquals(Buffer, "10s"));

		NiceNumGeneral(100000000000, Buffer, kNicenumTime);
		CHECK(StringEquals(Buffer, "100s"));

		NiceNumGeneral(1000000000000, Buffer, kNicenumTime);
		CHECK(StringEquals(Buffer, "1000s"));

		NiceNumGeneral(10000000000000, Buffer, kNicenumTime);
		CHECK(StringEquals(Buffer, "10000s"));

		NiceNumGeneral(100000000000000, Buffer, kNicenumTime);
		CHECK(StringEquals(Buffer, "100000s"));
	}

	SUBCASE("bytes")
	{
		NiceNumGeneral(1, Buffer, kNicenumBytes);
		CHECK(StringEquals(Buffer, "1B"));

		NiceNumGeneral(10, Buffer, kNicenumBytes);
		CHECK(StringEquals(Buffer, "10B"));

		NiceNumGeneral(100, Buffer, kNicenumBytes);
		CHECK(StringEquals(Buffer, "100B"));

		NiceNumGeneral(1000, Buffer, kNicenumBytes);
		CHECK(StringEquals(Buffer, "1000B"));

		NiceNumGeneral(10000, Buffer, kNicenumBytes);
		CHECK(StringEquals(Buffer, "9.77K"));
	}

	SUBCASE("byteRate")
	{
		NiceByteRateToBuffer(1, 1, Buffer);
		CHECK(StringEquals(Buffer, "1000B/s"));

		NiceByteRateToBuffer(1000, 1000, Buffer);
		CHECK(StringEquals(Buffer, "1000B/s"));

		NiceByteRateToBuffer(1024, 1, Buffer);
		CHECK(StringEquals(Buffer, "1000K/s"));

		NiceByteRateToBuffer(1024, 1000, Buffer);
		CHECK(StringEquals(Buffer, "1K/s"));
	}

	SUBCASE("timespan")
	{
		NiceTimeSpanMsToBuffer(1, Buffer);
		CHECK(StringEquals(Buffer, "1ms"));

		NiceTimeSpanMsToBuffer(900, Buffer);
		CHECK(StringEquals(Buffer, "900ms"));

		NiceTimeSpanMsToBuffer(1000, Buffer);
		CHECK(StringEquals(Buffer, "1.00s"));

		NiceTimeSpanMsToBuffer(1900, Buffer);
		CHECK(StringEquals(Buffer, "1.90s"));

		NiceTimeSpanMsToBuffer(19000, Buffer);
		CHECK(StringEquals(Buffer, "19.0s"));

		NiceTimeSpanMsToBuffer(60000, Buffer);
		CHECK(StringEquals(Buffer, "1m00s"));

		NiceTimeSpanMsToBuffer(600000, Buffer);
		CHECK(StringEquals(Buffer, "10m00s"));

		NiceTimeSpanMsToBuffer(3600000, Buffer);
		CHECK(StringEquals(Buffer, "1h00m"));

		NiceTimeSpanMsToBuffer(36000000, Buffer);
		CHECK(StringEquals(Buffer, "10h00m"));

		NiceTimeSpanMsToBuffer(360000000, Buffer);
		CHECK(StringEquals(Buffer, "100h00m"));
	}
}

void
string_forcelink()
{
}

TEST_CASE("StringBuilder")
{
	StringBuilder<64> sb;

	SUBCASE("Empty init")
	{
		const char* str = sb.c_str();

		CHECK(StringLength(str) == 0);
	}

	SUBCASE("Append single character")
	{
		sb.Append('a');

		const char* str = sb.c_str();
		CHECK(StringLength(str) == 1);
		CHECK(str[0] == 'a');

		sb.Append('b');
		str = sb.c_str();
		CHECK(StringLength(str) == 2);
		CHECK(str[0] == 'a');
		CHECK(str[1] == 'b');
	}

	SUBCASE("Append string")
	{
		sb.Append("a");

		const char* str = sb.c_str();
		CHECK(StringLength(str) == 1);
		CHECK(str[0] == 'a');

		sb.Append("b");
		str = sb.c_str();
		CHECK(StringLength(str) == 2);
		CHECK(str[0] == 'a');
		CHECK(str[1] == 'b');

		sb.Append("cdefghijklmnopqrstuvwxyz");
		CHECK(sb.Size() == 26);

		sb.Append("abcdefghijklmnopqrstuvwxyz");
		CHECK(sb.Size() == 52);

		sb.Append("abcdefghijk");
		CHECK(sb.Size() == 63);
	}
}

TEST_CASE("ExtendableStringBuilder")
{
	ExtendableStringBuilder<16> sb;

	SUBCASE("Empty init")
	{
		const char* str = sb.c_str();

		CHECK(StringLength(str) == 0);
	}

	SUBCASE("Short append")
	{
		sb.Append("abcd");
		CHECK(sb.IsDynamic() == false);
	}

	SUBCASE("Short+long append")
	{
		sb.Append("abcd");
		CHECK(sb.IsDynamic() == false);
		// This should trigger a dynamic buffer allocation since the required
		// capacity exceeds the internal fixed buffer.
		sb.Append("abcdefghijklmnopqrstuvwxyz");
		CHECK(sb.IsDynamic() == true);
		CHECK(sb.Size() == 30);
		CHECK(sb.Size() == StringLength(sb.c_str()));
	}
}

TEST_CASE("WideStringBuilder")
{
	WideStringBuilder<64> sb;

	SUBCASE("Empty init")
	{
		const wchar_t* str = sb.c_str();

		CHECK(StringLength(str) == 0);
	}

	SUBCASE("Append single character")
	{
		sb.Append(L'a');

		const wchar_t* str = sb.c_str();
		CHECK(StringLength(str) == 1);
		CHECK(str[0] == L'a');

		sb.Append(L'b');
		str = sb.c_str();
		CHECK(StringLength(str) == 2);
		CHECK(str[0] == L'a');
		CHECK(str[1] == L'b');
	}

	SUBCASE("Append string")
	{
		sb.Append(L"a");

		const wchar_t* str = sb.c_str();
		CHECK(StringLength(str) == 1);
		CHECK(str[0] == L'a');

		sb.Append(L"b");
		str = sb.c_str();
		CHECK(StringLength(str) == 2);
		CHECK(str[0] == L'a');
		CHECK(str[1] == L'b');

		sb.Append(L"cdefghijklmnopqrstuvwxyz");
		CHECK(sb.Size() == 26);

		sb.Append(L"abcdefghijklmnopqrstuvwxyz");
		CHECK(sb.Size() == 52);

		sb.Append(L"abcdefghijk");
		CHECK(sb.Size() == 63);
	}
}

TEST_CASE("ExtendableWideStringBuilder")
{
	ExtendableWideStringBuilder<16> sb;

	SUBCASE("Empty init")
	{
		CHECK(sb.Size() == 0);

		const wchar_t* str = sb.c_str();
		CHECK(StringLength(str) == 0);
	}

	SUBCASE("Short append")
	{
		sb.Append(L"abcd");
		CHECK(sb.IsDynamic() == false);
	}

	SUBCASE("Short+long append")
	{
		sb.Append(L"abcd");
		CHECK(sb.IsDynamic() == false);
		// This should trigger a dynamic buffer allocation since the required
		// capacity exceeds the internal fixed buffer.
		sb.Append(L"abcdefghijklmnopqrstuvwxyz");
		CHECK(sb.IsDynamic() == true);
		CHECK(sb.Size() == 30);
		CHECK(sb.Size() == StringLength(sb.c_str()));
	}
}

TEST_CASE("utf8")
{
	SUBCASE("utf8towide")
	{
		// TODO: add more extensive testing here - this covers a very small space

		WideStringBuilder<32> wout;
		Utf8ToWide(u8"abcdefghi", wout);
		CHECK(StringEquals(L"abcdefghi", wout.c_str()));

		wout.Reset();

		Utf8ToWide(u8"abc���", wout);
		CHECK(StringEquals(L"abc���", wout.c_str()));
	}

	SUBCASE("widetoutf8")
	{
		// TODO: add more extensive testing here - this covers a very small space

		StringBuilder<32> out;

		WideToUtf8(L"abcdefghi", out);
		CHECK(StringEquals("abcdefghi", out.c_str()));

		out.Reset();

		WideToUtf8(L"abc���", out);
		CHECK(StringEquals(u8"abc���", out.c_str()));
	}
}

TEST_CASE("filepath")
{
	CHECK(FilepathFindExtension("foo\\bar\\baz.txt", ".txt") != nullptr);
	CHECK(FilepathFindExtension("foo\\bar\\baz.txt", ".zap") == nullptr);

	CHECK(FilepathFindExtension("foo\\bar\\baz.txt") != nullptr);
	CHECK(FilepathFindExtension("foo\\bar\\baz.txt") == std::string_view(".txt"));

	CHECK(FilepathFindExtension(".txt") == std::string_view(".txt"));
}

}  // namespace zen

