// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include "zencore.h"

#include <zencore/memory.h>

#include <xxh3.h>
#include <string_view>

namespace zen {

class StringBuilderBase;

/**
 * XXH3 hash
 */
struct XXH3_128
{
	uint8_t Hash[16];

	static XXH3_128 MakeFrom(const void* data /* 16 bytes */)
	{
		XXH3_128 Xx;
		memcpy(Xx.Hash, data, sizeof Xx);
		return Xx;
	}

	static inline XXH3_128 HashMemory(const void* data, size_t byteCount)
	{
		XXH3_128 Hash;
		XXH128_canonicalFromHash((XXH128_canonical_t*)Hash.Hash, XXH3_128bits(data, byteCount));
		return Hash;
	}
	static XXH3_128	   HashMemory(MemoryView Data) { return HashMemory(Data.GetData(), Data.GetSize()); }
	static XXH3_128	   FromHexString(const char* string);
	static XXH3_128	   FromHexString(const std::string_view string);
	const char*		   ToHexString(char* outString /* 32 characters + NUL terminator */) const;
	StringBuilderBase& ToHexString(StringBuilderBase& outBuilder) const;

	static const int StringLength = 32;
	typedef char	 String_t[StringLength + 1];

	static XXH3_128 Zero;  // Initialized to all zeros

	inline auto operator<=>(const XXH3_128& rhs) const = default;

	struct Hasher
	{
		size_t operator()(const XXH3_128& v) const
		{
			size_t h;
			memcpy(&h, v.Hash, sizeof h);
			return h;
		}
	};
};

struct XXH3_128Stream
{
	/// Begin streaming hash compute (not needed on freshly constructed instance)
	void Reset() { memset(&m_State, 0, sizeof m_State); }

	/// Append another chunk
	XXH3_128Stream& Append(const void* Data, size_t ByteCount)
	{
		XXH3_128bits_update(&m_State, Data, ByteCount);
		return *this;
	}

	/// Append another chunk
	XXH3_128Stream& Append(MemoryView Data) { return Append(Data.GetData(), Data.GetSize()); }

	/// Obtain final hash. If you wish to reuse the instance call reset()
	XXH3_128 GetHash()
	{
		XXH3_128 Hash;
		XXH128_canonicalFromHash((XXH128_canonical_t*)Hash.Hash, XXH3_128bits_digest(&m_State));
		return Hash;
	}

private:
	XXH3_state_s m_State{};
};

}  // namespace zen

