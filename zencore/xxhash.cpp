// Copyright Noah Games, Inc. All Rights Reserved.

#include <zencore/xxhash.h>

#include <zencore/string.h>

#include <doctest/doctest.h>
#include <gsl/gsl-lite.hpp>

namespace zen {

XXH3_128 XXH3_128::Zero;  // Initialized to all zeros

XXH3_128
XXH3_128::FromHexString(const char* InString)
{
	return FromHexString({InString, sizeof(XXH3_128::Hash) * 2});
}

XXH3_128
XXH3_128::FromHexString(std::string_view InString)
{
	ZEN_ASSERT(InString.size() == 2 * sizeof(XXH3_128::Hash));

	XXH3_128 Xx;
	ParseHexBytes(InString.data(), InString.size(), Xx.Hash);
	return Xx;
}

const char*
XXH3_128::ToHexString(char* OutString /* 40 characters + NUL terminator */) const
{
	ToHexBytes(Hash, sizeof(XXH3_128), OutString);
	OutString[2 * sizeof(XXH3_128)] = '\0';

	return OutString;
}

StringBuilderBase&
XXH3_128::ToHexString(StringBuilderBase& OutBuilder) const
{
	String_t str;
	ToHexString(str);

	OutBuilder.AppendRange(str, &str[StringLength]);

	return OutBuilder;
}

}  // namespace zen

