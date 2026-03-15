// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <zencore/iohash.h>
#include <zencore/string.h>
#include <zencore/uid.h>

#include <fmt/format.h>
#include <filesystem>
#include <string_view>

// Custom formatting for some zencore types

template<>
struct fmt::formatter<zen::IoHash> : formatter<string_view>
{
	template<typename FormatContext>
	auto format(const zen::IoHash& Hash, FormatContext& ctx)
	{
		zen::IoHash::String_t String;
		Hash.ToHexString(String);
		return formatter<string_view>::format({String, zen::IoHash::StringLength}, ctx);
	}
};

template<>
struct fmt::formatter<zen::Oid> : formatter<string_view>
{
	template<typename FormatContext>
	auto format(const zen::Oid& Id, FormatContext& ctx)
	{
		zen::StringBuilder<32> String;
		Id.ToString(String);
		return formatter<string_view>::format({String.c_str(), zen::Oid::StringLength}, ctx);
	}
};

template<>
struct fmt::formatter<std::filesystem::path> : formatter<string_view>
{
	template<typename FormatContext>
	auto format(const std::filesystem::path& Path, FormatContext& ctx)
	{
		zen::ExtendableStringBuilder<128> String;
		WideToUtf8(Path.c_str(), String);
		return formatter<string_view>::format(String.ToView(), ctx);
	}
};

