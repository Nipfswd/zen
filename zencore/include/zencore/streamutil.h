// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <fmt/format.h>
#include <zencore/string.h>
#include <string>
#include <string_view>

#include "blake3.h"
#include "iohash.h"
#include "sha1.h"
#include "stream.h"

namespace zen {

ZENCORE_API BinaryWriter& operator<<(BinaryWriter& writer, const std::string_view& value);
ZENCORE_API BinaryReader& operator>>(BinaryReader& reader, std::string& value);

ZENCORE_API BinaryWriter& operator<<(BinaryWriter& writer, const std::wstring_view& value);
ZENCORE_API BinaryReader& operator>>(BinaryReader& reader, std::wstring& value);
ZENCORE_API TextWriter& operator<<(TextWriter& writer, const std::wstring_view& value);

inline BinaryWriter&
operator<<(BinaryWriter& writer, const SHA1& value)
{
	writer.Write(value.Hash, sizeof value.Hash);
	return writer;
}
inline BinaryReader&
operator>>(BinaryReader& reader, SHA1& value)
{
	reader.Read(value.Hash, sizeof value.Hash);
	return reader;
}
ZENCORE_API TextWriter& operator<<(TextWriter& writer, const zen::SHA1& value);

inline BinaryWriter&
operator<<(BinaryWriter& writer, const BLAKE3& value)
{
	writer.Write(value.Hash, sizeof value.Hash);
	return writer;
}
inline BinaryReader&
operator>>(BinaryReader& reader, BLAKE3& value)
{
	reader.Read(value.Hash, sizeof value.Hash);
	return reader;
}
ZENCORE_API TextWriter& operator<<(TextWriter& writer, const BLAKE3& value);

inline BinaryWriter&
operator<<(BinaryWriter& writer, const IoHash& value)
{
	writer.Write(value.Hash, sizeof value.Hash);
	return writer;
}
inline BinaryReader&
operator>>(BinaryReader& reader, IoHash& value)
{
	reader.Read(value.Hash, sizeof value.Hash);
	return reader;
}
ZENCORE_API TextWriter& operator<<(TextWriter& writer, const IoHash& value);

}  // namespace zen

//////////////////////////////////////////////////////////////////////////

template<>
struct fmt::formatter<zen::IoHash>
{
	constexpr auto parse(format_parse_context& ctx)
	{
		// Parse the presentation format and store it in the formatter:
		auto it = ctx.begin(), end = ctx.end();

		// Check if reached the end of the range:
		if (it != end && *it != '}')
			throw format_error("invalid format");

		// Return an iterator past the end of the parsed range:
		return it;
	}

	template<typename FormatContext>
	auto format(const zen::IoHash& h, FormatContext& ctx)
	{
		zen::ExtendableStringBuilder<48> String;
		h.ToHexString(String);
		return format_to(ctx.out(), std::string_view(String));
	}
};

template<>
struct fmt::formatter<zen::BLAKE3>
{
	constexpr auto parse(format_parse_context& ctx)
	{
		// Parse the presentation format and store it in the formatter:
		auto it = ctx.begin(), end = ctx.end();

		// Check if reached the end of the range:
		if (it != end && *it != '}')
			throw format_error("invalid format");

		// Return an iterator past the end of the parsed range:
		return it;
	}

	template<typename FormatContext>
	auto format(const zen::BLAKE3& h, FormatContext& ctx)
	{
		zen::ExtendableStringBuilder<80> String;
		h.ToHexString(String);
		return format_to(ctx.out(), std::string_view(String));
	}
};

