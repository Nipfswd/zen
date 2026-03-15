// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <stdint.h>
#include <compare>
#include "zencore.h"

namespace zen {

class StringBuilderBase;

struct MD5
{
	uint8_t Hash[16];

	inline auto operator<=>(const MD5& rhs) const = default;

	static const int StringLength = 40;
	typedef char	 String_t[StringLength + 1];

	static MD5		   HashMemory(const void* data, size_t byteCount);
	static MD5		   FromHexString(const char* string);
	const char*		   ToHexString(char* outString /* 40 characters + NUL terminator */) const;
	StringBuilderBase& ToHexString(StringBuilderBase& outBuilder) const;

	static MD5 Zero;  // Initialized to all zeroes
};

/**
 * Utility class for computing SHA1 hashes
 */
class MD5Stream
{
public:
	MD5Stream();

	/// Begin streaming MD5 compute (not needed on freshly constructed MD5Stream instance)
	void Reset();
	/// Append another chunk
	MD5Stream& Append(const void* data, size_t byteCount);
	/// Obtain final MD5 hash. If you wish to reuse the MD5Stream instance call reset()
	MD5 GetHash();

private:
};

void md5_forcelink();  // internal

}  // namespace zen

