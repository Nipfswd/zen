// Copyright Noah Games, Inc. All Rights Reserved.

#include <zencore/streamutil.h>
#include <zencore/string.h>

namespace zen {

BinaryWriter&
operator<<(BinaryWriter& writer, const std::string_view& value)
{
	writer.Write(value.data(), value.size());
	writer << uint8_t(0);

	return writer;
}

BinaryReader&
operator>>(BinaryReader& reader, std::string& value)
{
	for (;;)
	{
		uint8_t x;
		reader.Read(&x, 1);

		if (x == 0)
			return reader;

		value.push_back(char(x));
	}
}

BinaryWriter&
operator<<(BinaryWriter& writer, const std::wstring_view& value)
{
	// write as utf8

	ExtendableStringBuilder<128> utf8;
	WideToUtf8(value, utf8);

	writer.Write(utf8.c_str(), utf8.Size() + 1);

	return writer;
}

BinaryReader&
operator>>(BinaryReader& reader, std::wstring& value)
{
	// read as utf8

	std::string v8;
	reader >> v8;

	ExtendableWideStringBuilder<128> wstr;
	Utf8ToWide(v8, wstr);

	value = wstr.c_str();

	return reader;
}

TextWriter&
operator<<(TextWriter& writer, const zen::SHA1& value)
{
	zen::SHA1::String_t buffer;
	value.ToHexString(buffer);

	writer.Write(buffer, zen::SHA1::StringLength);

	return writer;
}

TextWriter&
operator<<(TextWriter& writer, const zen::BLAKE3& value)
{
	zen::BLAKE3::String_t buffer;
	value.ToHexString(buffer);

	writer.Write(buffer, zen::BLAKE3::StringLength);

	return writer;
}

TextWriter&
operator<<(TextWriter& writer, const zen::IoHash& value)
{
	zen::IoHash::String_t buffer;
	value.ToHexString(buffer);

	writer.Write(buffer, zen::IoHash::StringLength);

	return writer;
}

TextWriter&
operator<<(TextWriter& writer, const std::wstring_view& value)
{
	ExtendableStringBuilder<128> v8;
	WideToUtf8(value, v8);

	writer.Write(v8.c_str(), v8.Size());
	return writer;
}

}  // namespace zen

