// //////////////////////////////////////////////////////////
// sha1.h
// Copyright (c) 2014,2015 Stephan Brumme. All rights reserved.
// see http://create.stephan-brumme.com/disclaimer.html
//

#pragma once

#include <stdint.h>
#include <compare>
#include "zencore.h"

namespace zen {

class StringBuilderBase;

struct SHA1
{
	uint8_t Hash[20];

	inline auto operator<=>(const SHA1& rhs) const = default;

	static const int StringLength = 40;
	typedef char	 String_t[StringLength + 1];

	static SHA1		   HashMemory(const void* data, size_t byteCount);
	static SHA1		   FromHexString(const char* string);
	const char*		   ToHexString(char* outString /* 40 characters + NUL terminator */) const;
	StringBuilderBase& ToHexString(StringBuilderBase& outBuilder) const;

	static SHA1 Zero;  // Initialized to all zeroes
};

/**
 * Utility class for computing SHA1 hashes
 */
class SHA1Stream
{
public:
	SHA1Stream();

	/** compute SHA1 of a memory block

		\note SHA1 class contains a slightly more convenient helper function for this use case
		\see SHA1::fromMemory()
	  */
	SHA1 Compute(const void* data, size_t byteCount);

	/// Begin streaming SHA1 compute (not needed on freshly constructed SHA1Stream instance)
	void Reset();
	/// Append another chunk
	SHA1Stream& Append(const void* data, size_t byteCount);
	/// Obtain final SHA1 hash. If you wish to reuse the SHA1Stream instance call reset()
	SHA1 GetHash();

private:
	void ProcessBlock(const void* data);
	void ProcessBuffer();

	enum
	{
		/// split into 64 byte blocks (=> 512 bits)
		BlockSize  = 512 / 8,
		HashBytes  = 20,
		HashValues = HashBytes / 4
	};

	uint64_t m_NumBytes;		   // size of processed data in bytes
	size_t	 m_BufferSize;		   // valid bytes in m_buffer
	uint8_t	 m_Buffer[BlockSize];  // bytes not processed yet
	uint32_t m_Hash[HashValues];
};

void sha1_forcelink();	// internal

}  // namespace zen
