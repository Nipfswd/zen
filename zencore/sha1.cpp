// //////////////////////////////////////////////////////////
// sha1.cpp
// Copyright (c) 2014,2015 Stephan Brumme. All rights reserved.
// see http://create.stephan-brumme.com/disclaimer.html
//

#include <zencore/sha1.h>
#include <zencore/string.h>
#include <zencore/zencore.h>

#include <doctest/doctest.h>
#include <string.h>

// big endian architectures need #define __BYTE_ORDER __BIG_ENDIAN
#ifndef _MSC_VER
#	include <endian.h>
#endif

namespace zen {

//////////////////////////////////////////////////////////////////////////

SHA1 SHA1::Zero;  // Initialized to all zeroes

//////////////////////////////////////////////////////////////////////////

SHA1Stream::SHA1Stream()
{
	Reset();
}

void
SHA1Stream::Reset()
{
	m_NumBytes	 = 0;
	m_BufferSize = 0;

	// according to RFC 1321
	m_Hash[0] = 0x67452301;
	m_Hash[1] = 0xefcdab89;
	m_Hash[2] = 0x98badcfe;
	m_Hash[3] = 0x10325476;
	m_Hash[4] = 0xc3d2e1f0;
}

namespace {
	// mix functions for processBlock()
	inline uint32_t f1(uint32_t b, uint32_t c, uint32_t d)
	{
		return d ^ (b & (c ^ d));  // original: f = (b & c) | ((~b) & d);
	}

	inline uint32_t f2(uint32_t b, uint32_t c, uint32_t d) { return b ^ c ^ d; }

	inline uint32_t f3(uint32_t b, uint32_t c, uint32_t d) { return (b & c) | (b & d) | (c & d); }

	inline uint32_t rotate(uint32_t a, uint32_t c) { return (a << c) | (a >> (32 - c)); }

	inline uint32_t swap(uint32_t x)
	{
#if defined(__GNUC__) || defined(__clang__)
		return __builtin_bswap32(x);
#endif
#ifdef MSC_VER
		return _byteswap_ulong(x);
#endif

		return (x >> 24) | ((x >> 8) & 0x0000FF00) | ((x << 8) & 0x00FF0000) | (x << 24);
	}
}  // namespace

/// process 64 bytes
void
SHA1Stream::ProcessBlock(const void* data)
{
	// get last hash
	uint32_t a = m_Hash[0];
	uint32_t b = m_Hash[1];
	uint32_t c = m_Hash[2];
	uint32_t d = m_Hash[3];
	uint32_t e = m_Hash[4];

	// data represented as 16x 32-bit words
	const uint32_t* input = (uint32_t*)data;
	// convert to big endian
	uint32_t words[80];
	for (int i = 0; i < 16; i++)
#if defined(__BYTE_ORDER) && (__BYTE_ORDER != 0) && (__BYTE_ORDER == __BIG_ENDIAN)
		words[i] = input[i];
#else
		words[i] = swap(input[i]);
#endif

	// extend to 80 words
	for (int i = 16; i < 80; i++)
		words[i] = rotate(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);

	// first round
	for (int i = 0; i < 4; i++)
	{
		int offset = 5 * i;
		e += rotate(a, 5) + f1(b, c, d) + words[offset] + 0x5a827999;
		b = rotate(b, 30);
		d += rotate(e, 5) + f1(a, b, c) + words[offset + 1] + 0x5a827999;
		a = rotate(a, 30);
		c += rotate(d, 5) + f1(e, a, b) + words[offset + 2] + 0x5a827999;
		e = rotate(e, 30);
		b += rotate(c, 5) + f1(d, e, a) + words[offset + 3] + 0x5a827999;
		d = rotate(d, 30);
		a += rotate(b, 5) + f1(c, d, e) + words[offset + 4] + 0x5a827999;
		c = rotate(c, 30);
	}

	// second round
	for (int i = 4; i < 8; i++)
	{
		int offset = 5 * i;
		e += rotate(a, 5) + f2(b, c, d) + words[offset] + 0x6ed9eba1;
		b = rotate(b, 30);
		d += rotate(e, 5) + f2(a, b, c) + words[offset + 1] + 0x6ed9eba1;
		a = rotate(a, 30);
		c += rotate(d, 5) + f2(e, a, b) + words[offset + 2] + 0x6ed9eba1;
		e = rotate(e, 30);
		b += rotate(c, 5) + f2(d, e, a) + words[offset + 3] + 0x6ed9eba1;
		d = rotate(d, 30);
		a += rotate(b, 5) + f2(c, d, e) + words[offset + 4] + 0x6ed9eba1;
		c = rotate(c, 30);
	}

	// third round
	for (int i = 8; i < 12; i++)
	{
		int offset = 5 * i;
		e += rotate(a, 5) + f3(b, c, d) + words[offset] + 0x8f1bbcdc;
		b = rotate(b, 30);
		d += rotate(e, 5) + f3(a, b, c) + words[offset + 1] + 0x8f1bbcdc;
		a = rotate(a, 30);
		c += rotate(d, 5) + f3(e, a, b) + words[offset + 2] + 0x8f1bbcdc;
		e = rotate(e, 30);
		b += rotate(c, 5) + f3(d, e, a) + words[offset + 3] + 0x8f1bbcdc;
		d = rotate(d, 30);
		a += rotate(b, 5) + f3(c, d, e) + words[offset + 4] + 0x8f1bbcdc;
		c = rotate(c, 30);
	}

	// fourth round
	for (int i = 12; i < 16; i++)
	{
		int offset = 5 * i;
		e += rotate(a, 5) + f2(b, c, d) + words[offset] + 0xca62c1d6;
		b = rotate(b, 30);
		d += rotate(e, 5) + f2(a, b, c) + words[offset + 1] + 0xca62c1d6;
		a = rotate(a, 30);
		c += rotate(d, 5) + f2(e, a, b) + words[offset + 2] + 0xca62c1d6;
		e = rotate(e, 30);
		b += rotate(c, 5) + f2(d, e, a) + words[offset + 3] + 0xca62c1d6;
		d = rotate(d, 30);
		a += rotate(b, 5) + f2(c, d, e) + words[offset + 4] + 0xca62c1d6;
		c = rotate(c, 30);
	}

	// update hash
	m_Hash[0] += a;
	m_Hash[1] += b;
	m_Hash[2] += c;
	m_Hash[3] += d;
	m_Hash[4] += e;
}

/// add arbitrary number of bytes
SHA1Stream&
SHA1Stream::Append(const void* data, size_t byteCount)
{
	const uint8_t* current = (const uint8_t*)data;

	if (m_BufferSize > 0)
	{
		while (byteCount > 0 && m_BufferSize < BlockSize)
		{
			m_Buffer[m_BufferSize++] = *current++;
			byteCount--;
		}
	}

	// full buffer
	if (m_BufferSize == BlockSize)
	{
		ProcessBlock((void*)m_Buffer);
		m_NumBytes += BlockSize;
		m_BufferSize = 0;
	}

	// no more data ?
	if (byteCount == 0)
		return *this;

	// process full blocks
	while (byteCount >= BlockSize)
	{
		ProcessBlock(current);
		current += BlockSize;
		m_NumBytes += BlockSize;
		byteCount -= BlockSize;
	}

	// keep remaining bytes in buffer
	while (byteCount > 0)
	{
		m_Buffer[m_BufferSize++] = *current++;
		byteCount--;
	}

	return *this;
}

/// process final block, less than 64 bytes
void
SHA1Stream::ProcessBuffer()
{
	// the input bytes are considered as bits strings, where the first bit is the most significant bit of the byte

	// - append "1" bit to message
	// - append "0" bits until message length in bit mod 512 is 448
	// - append length as 64 bit integer

	// number of bits
	size_t paddedLength = m_BufferSize * 8;

	// plus one bit set to 1 (always appended)
	paddedLength++;

	// number of bits must be (numBits % 512) = 448
	size_t lower11Bits = paddedLength & 511;
	if (lower11Bits <= 448)
		paddedLength += 448 - lower11Bits;
	else
		paddedLength += 512 + 448 - lower11Bits;
	// convert from bits to bytes
	paddedLength /= 8;

	// only needed if additional data flows over into a second block
	unsigned char extra[BlockSize];

	// append a "1" bit, 128 => binary 10000000
	if (m_BufferSize < BlockSize)
		m_Buffer[m_BufferSize] = 128;
	else
		extra[0] = 128;

	size_t i;
	for (i = m_BufferSize + 1; i < BlockSize; i++)
		m_Buffer[i] = 0;
	for (; i < paddedLength; i++)
		extra[i - BlockSize] = 0;

	// add message length in bits as 64 bit number
	uint64_t msgBits = 8 * (m_NumBytes + m_BufferSize);
	// find right position
	unsigned char* addLength;
	if (paddedLength < BlockSize)
		addLength = m_Buffer + paddedLength;
	else
		addLength = extra + paddedLength - BlockSize;

	// must be big endian
	*addLength++ = (unsigned char)((msgBits >> 56) & 0xFF);
	*addLength++ = (unsigned char)((msgBits >> 48) & 0xFF);
	*addLength++ = (unsigned char)((msgBits >> 40) & 0xFF);
	*addLength++ = (unsigned char)((msgBits >> 32) & 0xFF);
	*addLength++ = (unsigned char)((msgBits >> 24) & 0xFF);
	*addLength++ = (unsigned char)((msgBits >> 16) & 0xFF);
	*addLength++ = (unsigned char)((msgBits >> 8) & 0xFF);
	*addLength	 = (unsigned char)(msgBits & 0xFF);

	// process blocks
	ProcessBlock(m_Buffer);
	// flowed over into a second block ?
	if (paddedLength > BlockSize)
		ProcessBlock(extra);
}

/// return latest hash as bytes
SHA1
SHA1Stream::GetHash()
{
	SHA1 sha1;
	// save old hash if buffer is partially filled
	uint32_t oldHash[HashValues];
	for (int i = 0; i < HashValues; i++)
		oldHash[i] = m_Hash[i];

	// process remaining bytes
	ProcessBuffer();

	unsigned char* current = sha1.Hash;
	for (int i = 0; i < HashValues; i++)
	{
		*current++ = (m_Hash[i] >> 24) & 0xFF;
		*current++ = (m_Hash[i] >> 16) & 0xFF;
		*current++ = (m_Hash[i] >> 8) & 0xFF;
		*current++ = m_Hash[i] & 0xFF;

		// restore old hash
		m_Hash[i] = oldHash[i];
	}

	return sha1;
}

/// compute SHA1 of a memory block
SHA1
SHA1Stream::Compute(const void* data, size_t byteCount)
{
	Reset();
	Append(data, byteCount);
	return GetHash();
}

SHA1
SHA1::HashMemory(const void* data, size_t byteCount)
{
	return SHA1Stream().Append(data, byteCount).GetHash();
}

SHA1
SHA1::FromHexString(const char* string)
{
	SHA1 sha1;

	ParseHexBytes(string, 40, sha1.Hash);

	return sha1;
}

const char*
SHA1::ToHexString(char* outString /* 40 characters + NUL terminator */) const
{
	ToHexBytes(Hash, sizeof(SHA1), outString);
	outString[2 * sizeof(SHA1)] = '\0';

	return outString;
}

StringBuilderBase&
SHA1::ToHexString(StringBuilderBase& outBuilder) const
{
	char str[41];
	ToHexString(str);

	outBuilder.AppendRange(str, &str[40]);

	return outBuilder;
}

//////////////////////////////////////////////////////////////////////////
//
// Testing related code follows...
//

void
sha1_forcelink()
{
}

doctest::String
toString(const SHA1& value)
{
	char sha1text[2 * sizeof(SHA1) + 1];
	value.ToHexString(sha1text);

	return sha1text;
}

TEST_CASE("SHA1")
{
	uint8_t sha1_empty[20] = {0xda, 0x39, 0xa3, 0xee, 0x5e, 0x6b, 0x4b, 0x0d, 0x32, 0x55,
							  0xbf, 0xef, 0x95, 0x60, 0x18, 0x90, 0xaf, 0xd8, 0x07, 0x09};
	SHA1	sha1z;
	memcpy(sha1z.Hash, sha1_empty, sizeof sha1z.Hash);

	SUBCASE("Empty string")
	{
		SHA1 sha1 = SHA1::HashMemory(nullptr, 0);

		CHECK(sha1 == sha1z);
	}

	SUBCASE("Empty stream")
	{
		SHA1Stream sha1s;
		sha1s.Append(nullptr, 0);
		sha1s.Append(nullptr, 0);
		sha1s.Append(nullptr, 0);
		CHECK(sha1s.GetHash() == sha1z);
	}

	SUBCASE("SHA1 from string")
	{
		const SHA1 sha1empty = SHA1::FromHexString("da39a3ee5e6b4b0d3255bfef95601890afd80709");

		CHECK(sha1z == sha1empty);
	}

	SUBCASE("SHA1 to string")
	{
		char sha1str[41];
		sha1z.ToHexString(sha1str);

		CHECK(StringEquals(sha1str, "da39a3ee5e6b4b0d3255bfef95601890afd80709"));
	}

	SUBCASE("Hash ABC")
	{
		const SHA1 sha1abc = SHA1::FromHexString("3c01bdbb26f358bab27f267924aa2c9a03fcfdb8");

		SHA1Stream sha1s;

		sha1s.Append("A", 1);
		sha1s.Append("B", 1);
		sha1s.Append("C", 1);
		CHECK(sha1s.GetHash() == sha1abc);

		sha1s.Reset();
		sha1s.Append("AB", 2);
		sha1s.Append("C", 1);
		CHECK(sha1s.GetHash() == sha1abc);

		sha1s.Reset();
		sha1s.Append("ABC", 3);
		CHECK(sha1s.GetHash() == sha1abc);

		sha1s.Reset();
		sha1s.Append("A", 1);
		sha1s.Append("BC", 2);
		CHECK(sha1s.GetHash() == sha1abc);
	}
}

}  // namespace zen
