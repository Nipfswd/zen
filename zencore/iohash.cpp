// Copyright Noah Games, Inc. All Rights Reserved.

#include <zencore/iohash.h>

#include <zencore/blake3.h>
#include <zencore/string.h>

#include <doctest/doctest.h>
#include <gsl/gsl-lite.hpp>

namespace zen {

IoHash IoHash::Zero;  // Initialized to all zeros

IoHash
IoHash::HashMemory(const void* data, size_t byteCount)
{
	BLAKE3 b3 = BLAKE3::HashMemory(data, byteCount);

	IoHash io;
	memcpy(io.Hash, b3.Hash, sizeof io.Hash);

	return io;
}

IoHash
IoHash::FromHexString(const char* string)
{
	return FromHexString({string, sizeof(IoHash::Hash) * 2});
}

IoHash
IoHash::FromHexString(std::string_view string)
{
	ZEN_ASSERT(string.size() == 2 * sizeof(IoHash::Hash));

	IoHash io;

	ParseHexBytes(string.data(), string.size(), io.Hash);

	return io;
}

const char*
IoHash::ToHexString(char* outString /* 40 characters + NUL terminator */) const
{
	ToHexBytes(Hash, sizeof(IoHash), outString);
	outString[2 * sizeof(IoHash)] = '\0';

	return outString;
}

StringBuilderBase&
IoHash::ToHexString(StringBuilderBase& outBuilder) const
{
	String_t Str;
	ToHexString(Str);

	outBuilder.AppendRange(Str, &Str[StringLength]);

	return outBuilder;
}

std::string
IoHash::ToHexString() const
{
	String_t Str;
	ToHexString(Str);

	return Str;
}

}  // namespace zen

