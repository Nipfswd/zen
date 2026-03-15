// Copyright Noah Games, Inc. All Rights Reserved.

#include <zencore/blake3.h>

#include <zencore/string.h>
#include <zencore/zencore.h>
#include "../3rdparty/BLAKE3/c/blake3.h"

#pragma comment(lib, "blake3.lib")

#include <doctest/doctest.h>
#include <string.h>

//////////////////////////////////////////////////////////////////////////

namespace zen {

void
blake3_forcelink()
{
}

BLAKE3 BLAKE3::Zero;  // Initialized to all zeroes

BLAKE3
BLAKE3::HashMemory(const void* data, size_t byteCount)
{
	BLAKE3 b3;

	blake3_hasher b3h;
	blake3_hasher_init(&b3h);
	blake3_hasher_update(&b3h, data, byteCount);
	blake3_hasher_finalize(&b3h, b3.Hash, sizeof b3.Hash);

	return b3;
}

BLAKE3
BLAKE3::FromHexString(const char* string)
{
	BLAKE3 b3;

	ParseHexBytes(string, 2 * sizeof b3.Hash, b3.Hash);

	return b3;
}

const char*
BLAKE3::ToHexString(char* outString /* 40 characters + NUL terminator */) const
{
	ToHexBytes(Hash, sizeof(BLAKE3), outString);
	outString[2 * sizeof(BLAKE3)] = '\0';

	return outString;
}

StringBuilderBase&
BLAKE3::ToHexString(StringBuilderBase& outBuilder) const
{
	char str[65];
	ToHexString(str);

	outBuilder.AppendRange(str, &str[65]);

	return outBuilder;
}

BLAKE3Stream::BLAKE3Stream()
{
	blake3_hasher* b3h = reinterpret_cast<blake3_hasher*>(m_HashState);
	static_assert(sizeof(blake3_hasher) <= sizeof(m_HashState));
	blake3_hasher_init(b3h);
}

void
BLAKE3Stream::Reset()
{
	blake3_hasher* b3h = reinterpret_cast<blake3_hasher*>(m_HashState);
	blake3_hasher_init(b3h);
}

BLAKE3Stream&
BLAKE3Stream::Append(const void* data, size_t byteCount)
{
	blake3_hasher* b3h = reinterpret_cast<blake3_hasher*>(m_HashState);
	blake3_hasher_update(b3h, data, byteCount);

	return *this;
}

BLAKE3
BLAKE3Stream::GetHash()
{
	BLAKE3 b3;

	blake3_hasher* b3h = reinterpret_cast<blake3_hasher*>(m_HashState);
	blake3_hasher_finalize(b3h, b3.Hash, sizeof b3.Hash);

	return b3;
}

//////////////////////////////////////////////////////////////////////////
//
// Testing related code follows...
//

doctest::String
toString(const BLAKE3& value)
{
	char text[2 * sizeof(BLAKE3) + 1];
	value.ToHexString(text);

	return text;
}

TEST_CASE("BLAKE3")
{
	SUBCASE("Basics")
	{
		BLAKE3 b3 = BLAKE3::HashMemory(nullptr, 0);
		CHECK(BLAKE3::FromHexString("af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262") == b3);

		BLAKE3::String_t b3s;
		std::string		 b3ss = b3.ToHexString(b3s);
		CHECK(b3ss == "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262");
	}

	SUBCASE("hashes")
	{
		CHECK(BLAKE3::FromHexString("00307ced6a8b278d5e3a9f77b138d0e9d2209717c9d45b205f427a73565cc5fb") == BLAKE3::HashMemory("abc123", 6));
		CHECK(BLAKE3::FromHexString("a7142c8c3905cd11b1e35105c7ac588b75d6798822f71e1145187ad46f3e8df4") ==
			  BLAKE3::HashMemory("1234567890123456789012345678901234567890", 40));
		CHECK(BLAKE3::FromHexString("70e708532559265c4662d0285e5e0a4be8bd972bd1f255a93ddf342243adc427") ==
			  BLAKE3::HashMemory("The HttpSendHttpResponse function sends an HTTP response to the specified HTTP request.", 87));
	}

	SUBCASE("streamHashes")
	{
		auto streamHash = [](const void* data, size_t dataBytes) -> BLAKE3 {
			BLAKE3Stream b3s;
			b3s.Append(data, dataBytes);
			return b3s.GetHash();
		};

		CHECK(BLAKE3::FromHexString("00307ced6a8b278d5e3a9f77b138d0e9d2209717c9d45b205f427a73565cc5fb") == streamHash("abc123", 6));
		CHECK(BLAKE3::FromHexString("a7142c8c3905cd11b1e35105c7ac588b75d6798822f71e1145187ad46f3e8df4") ==
			  streamHash("1234567890123456789012345678901234567890", 40));
		CHECK(BLAKE3::FromHexString("70e708532559265c4662d0285e5e0a4be8bd972bd1f255a93ddf342243adc427") ==
			  streamHash("The HttpSendHttpResponse function sends an HTTP response to the specified HTTP request.", 87));
	}
}

}  // namespace zen

