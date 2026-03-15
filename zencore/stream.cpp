// Copyright Noah Games, Inc. All Rights Reserved.

#include <doctest/doctest.h>
#include <stdarg.h>
#include <zencore/memory.h>
#include <zencore/stream.h>
#include <algorithm>
#include <exception>

namespace zen {

MemoryInStream::MemoryInStream(const void* buffer, size_t size)
: m_Buffer(reinterpret_cast<const uint8_t*>(buffer), reinterpret_cast<const uint8_t*>(buffer) + size)
{
}

void
MemoryInStream::Read(void* buffer, size_t byteCount, uint64_t offset)
{
	RwLock::ExclusiveLockScope _(m_Lock);

	const size_t needEnd = offset + byteCount;

	if (needEnd > m_Buffer.size())
		throw std::exception("read past end of file!");	 // TODO: better exception

	memcpy(buffer, m_Buffer.data() + offset, byteCount);
}

void
MemoryOutStream::Write(const void* data, size_t byteCount, uint64_t offset)
{
	RwLock::ExclusiveLockScope _(m_Lock);

	const size_t needEnd = offset + byteCount;

	if (needEnd > m_Buffer.size())
		m_Buffer.resize(needEnd);

	memcpy(m_Buffer.data() + offset, data, byteCount);
}

void
MemoryOutStream::Flush()
{
	// No-op
}

//////////////////////////////////////////////////////////////////////////

TextWriter::TextWriter(OutStream& stream) : m_Stream(&stream)
{
}

TextWriter::~TextWriter() = default;

void
TextWriter::Write(const void* data, size_t byteCount)
{
	m_Stream->Write(data, byteCount, m_CurrentOffset);
	m_CurrentOffset += byteCount;
}

TextWriter&
operator<<(TextWriter& Writer, const char* value)
{
	if (value)
		Writer.Write(value, strlen(value));
	else
		Writer.Write("(null)", 6);

	return Writer;
}

TextWriter&
operator<<(TextWriter& writer, const std::string_view& value)
{
	writer.Write(value.data(), value.size());

	return writer;
}

TextWriter&
operator<<(TextWriter& writer, bool value)
{
	if (value)
		writer.Write("true", 4);
	else
		writer.Write("false", 5);

	return writer;
}

TextWriter&
operator<<(TextWriter& writer, int8_t value)
{
	char buffer[16];
	_itoa_s(value, buffer, 10);
	writer << buffer;
	return writer;
}

TextWriter&
operator<<(TextWriter& writer, int16_t value)
{
	char buffer[16];
	_itoa_s(value, buffer, 10);
	writer << buffer;
	return writer;
}

TextWriter&
operator<<(TextWriter& writer, int32_t value)
{
	char buffer[16];
	_itoa_s(value, buffer, 10);
	writer << buffer;
	return writer;
}

TextWriter&
operator<<(TextWriter& writer, int64_t value)
{
	char buffer[32];
	_i64toa_s(value, buffer, sizeof buffer, 10);
	writer << buffer;
	return writer;
}

TextWriter&
operator<<(TextWriter& writer, uint8_t value)
{
	char buffer[16];
	_ultoa_s(value, buffer, 10);
	writer << buffer;
	return writer;
}

TextWriter&
operator<<(TextWriter& writer, uint16_t value)
{
	char buffer[16];
	_ultoa_s(value, buffer, 10);
	writer << buffer;
	return writer;
}

TextWriter&
operator<<(TextWriter& writer, uint32_t value)
{
	char buffer[16];
	_ultoa_s(value, buffer, 10);
	writer << buffer;
	return writer;
}

TextWriter&
operator<<(TextWriter& writer, uint64_t value)
{
	char buffer[32];
	_ui64toa_s(value, buffer, sizeof buffer, 10);
	writer << buffer;
	return writer;
}

void
TextWriter::Writef(const char* formatString, ...)
{
	va_list args;
	va_start(args, formatString);

	char* tempBuffer = nullptr;
	char  buffer[4096];
	int	  rv = vsnprintf(buffer, sizeof buffer, formatString, args);

	ZEN_ASSERT(rv >= 0);

	if (rv > sizeof buffer)
	{
		// Need more room -- allocate temporary buffer

		tempBuffer = (char*)Memory::Alloc(rv + 1, 8);

		int rv2 = vsnprintf(tempBuffer, rv + 1, formatString, args);

		ZEN_ASSERT(rv >= 0);
		ZEN_ASSERT(rv2 <= rv);

		rv = rv2;
	}

	m_Stream->Write(tempBuffer ? tempBuffer : buffer, rv, m_CurrentOffset);
	m_CurrentOffset += rv;

	if (tempBuffer)
		Memory::Free(tempBuffer);

	va_end(args);
}

//////////////////////////////////////////////////////////////////////////

IndentTextWriter::IndentTextWriter(OutStream& stream) : TextWriter(stream)
{
}

IndentTextWriter::~IndentTextWriter()
{
}

void
IndentTextWriter::Write(const void* data, size_t byteCount)
{
	const uint8_t* src = reinterpret_cast<const uint8_t*>(data);
	int			   cur = m_LineCursor;

	while (byteCount)
	{
		char c = *src++;

		if (cur == 0)
		{
			const char indentSpaces[] =
				"                                                                                                                          "
				"                                                                              ";

			cur = std::min<int>(m_IndentAmount, sizeof indentSpaces - 1);
			memcpy(m_LineBuffer, indentSpaces, cur);
		}

		m_LineBuffer[cur++] = c;
		--byteCount;

		if (c == '\n' || cur == sizeof m_LineBuffer)
		{
			TextWriter::Write(m_LineBuffer, cur);

			cur = 0;
		}
	}

	m_LineCursor = cur;
}

//////////////////////////////////////////////////////////////////////////
//
// Testing related code follows...
//

void
stream_forcelink()
{
}

TEST_CASE("BinaryWriter and BinaryWriter")
{
	MemoryOutStream stream;
	BinaryWriter	writer(stream);

	CHECK(writer.CurrentOffset() == 0);

	writer.Write("foo!", 4);
	CHECK(writer.CurrentOffset() == 4);

	writer << uint8_t(42) << uint16_t(42) << uint32_t(42) << uint64_t(42);
	writer << int8_t(42) << int16_t(42) << int32_t(42) << int64_t(42);

	CHECK(writer.CurrentOffset() == (4 + 15 * 2));

	// Read the data back

	MemoryInStream instream(stream.Data(), stream.Size());
	BinaryReader   reader(instream);
	CHECK(reader.CurrentOffset() == 0);

	char buffer[4];
	reader.Read(buffer, 4);
	CHECK(reader.CurrentOffset() == 4);

	CHECK(memcmp(buffer, "foo!", 4) == 0);

	uint8_t	 ui8  = 0;
	uint16_t ui16 = 0;
	uint32_t ui32 = 0;
	uint64_t ui64 = 0;
	int8_t	 i8	  = 0;
	int16_t	 i16  = 0;
	int32_t	 i32  = 0;
	int64_t	 i64  = 0;

	reader >> ui8 >> ui16 >> ui32 >> ui64;
	reader >> i8 >> i16 >> i32 >> i64;

	CHECK(reader.CurrentOffset() == (4 + 15 * 2));

	CHECK(ui8 == 42);
	CHECK(ui16 == 42);
	CHECK(ui32 == 42);
	CHECK(ui64 == 42);

	CHECK(i8 == 42);
	CHECK(i16 == 42);
	CHECK(i32 == 42);
	CHECK(i64 == 42);
}

}  // namespace zen

