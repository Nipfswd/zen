// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <cinttypes>
#include <compare>
#include <cstring>

namespace zen {

class StringBuilderBase;

/**
 * BLAKE3 hash - 256 bits
 */
struct BLAKE3
{
	uint8_t Hash[32];

	inline auto operator<=>(const BLAKE3& rhs) const = default;

	static BLAKE3	   HashMemory(const void* data, size_t byteCount);
	static BLAKE3	   FromHexString(const char* string);
	const char*		   ToHexString(char* outString /* 40 characters + NUL terminator */) const;
	StringBuilderBase& ToHexString(StringBuilderBase& outBuilder) const;

	static const int StringLength = 64;
	typedef char	 String_t[StringLength + 1];

	static BLAKE3 Zero;	 // Initialized to all zeroes

	struct Hasher
	{
		size_t operator()(const BLAKE3& v) const
		{
			size_t h;
			memcpy(&h, v.Hash, sizeof h);
			return h;
		}
	};
};

struct BLAKE3Stream
{
	BLAKE3Stream();

	void		  Reset();									   /// Begin streaming hash compute (not needed on freshly constructed instance)
	BLAKE3Stream& Append(const void* data, size_t byteCount);  /// Append another chunk
	BLAKE3		  GetHash();								   /// Obtain final hash. If you wish to reuse the instance call reset()

private:
	alignas(16) uint8_t m_HashState[2048];
};

void blake3_forcelink();  // internal

}  // namespace zen

