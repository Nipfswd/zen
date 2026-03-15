// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include "zencore.h"

#include <zencore/memory.h>
#include <zencore/refcount.h>
#include <zencore/thread.h>

#include <string_view>
#include <vector>

namespace zen {

/**
 * Basic byte stream interface
 *
 * This is intended as a minimal base class offering only the absolute minimum of functionality.
 *
 * IMPORTANT: To better support concurrency, this abstraction offers no "file pointer". Thus
 * every read or write operation needs to specify the offset from which they wish to read.
 *
 * Most client code will likely want to use reader/writer classes like BinaryWriter/BinaryReader
 *
 */
class OutStream : public RefCounted
{
public:
	virtual void Write(const void* Data, size_t ByteCount, uint64_t Offset) = 0;
	virtual void Flush()													= 0;
};

class InStream : public RefCounted
{
public:
	virtual void	 Read(void* DataPtr, size_t ByteCount, uint64_t Offset) = 0;
	virtual uint64_t Size() const											= 0;
};

/**
 * Stream which writes into a growing memory buffer
 */
class MemoryOutStream : public OutStream
{
public:
	MemoryOutStream()  = default;
	~MemoryOutStream() = default;

	virtual void		  Write(const void* DataPtr, size_t ByteCount, uint64_t Offset) override;
	virtual void		  Flush() override;
	inline const uint8_t* Data() const { return m_Buffer.data(); }
	inline uint64_t		  Size() const { return m_Buffer.size(); }

private:
	RwLock				 m_Lock;
	std::vector<uint8_t> m_Buffer;
};

inline MemoryView
MakeMemoryView(const MemoryOutStream& Stream)
{
	return MemoryView(Stream.Data(), Stream.Size());
}

/**
 * Stream which reads from a memory buffer
 */
class MemoryInStream : public InStream
{
public:
	MemoryInStream(const void* Buffer, size_t Size);
	MemoryInStream(MemoryView View) : MemoryInStream(View.GetData(), View.GetSize()) {}
	~MemoryInStream() = default;

	virtual void		  Read(void* DataPtr, size_t ByteCount, uint64_t ReadOffset) override;
	virtual uint64_t	  Size() const override { return m_Buffer.size(); }
	inline const uint8_t* Data() const { return m_Buffer.data(); }

private:
	RwLock				 m_Lock;
	std::vector<uint8_t> m_Buffer;
};

/**
 * Binary stream writer
 */

class BinaryWriter
{
public:
	inline BinaryWriter(OutStream& Stream) : m_Stream(&Stream) {}
	~BinaryWriter() = default;

	inline void Write(const void* DataPtr, size_t ByteCount)
	{
		m_Stream->Write(DataPtr, ByteCount, m_Offset);
		m_Offset += ByteCount;
	}

	uint64_t CurrentOffset() const { return m_Offset; }

private:
	RefPtr<OutStream> m_Stream;
	uint64_t		  m_Offset = 0;
};

inline BinaryWriter&
operator<<(BinaryWriter& Writer, bool Value)
{
	Writer.Write(&Value, sizeof Value);
	return Writer;
}
inline BinaryWriter&
operator<<(BinaryWriter& Writer, int8_t Value)
{
	Writer.Write(&Value, sizeof Value);
	return Writer;
}
inline BinaryWriter&
operator<<(BinaryWriter& Writer, int16_t Value)
{
	Writer.Write(&Value, sizeof Value);
	return Writer;
}
inline BinaryWriter&
operator<<(BinaryWriter& Writer, int32_t Value)
{
	Writer.Write(&Value, sizeof Value);
	return Writer;
}
inline BinaryWriter&
operator<<(BinaryWriter& Writer, int64_t Value)
{
	Writer.Write(&Value, sizeof Value);
	return Writer;
}
inline BinaryWriter&
operator<<(BinaryWriter& Writer, uint8_t Value)
{
	Writer.Write(&Value, sizeof Value);
	return Writer;
}
inline BinaryWriter&
operator<<(BinaryWriter& Writer, uint16_t Value)
{
	Writer.Write(&Value, sizeof Value);
	return Writer;
}
inline BinaryWriter&
operator<<(BinaryWriter& Writer, uint32_t Value)
{
	Writer.Write(&Value, sizeof Value);
	return Writer;
}
inline BinaryWriter&
operator<<(BinaryWriter& Writer, uint64_t Value)
{
	Writer.Write(&Value, sizeof Value);
	return Writer;
}

/**
 * Binary stream reader
 */

class BinaryReader
{
public:
	inline BinaryReader(InStream& Stream) : m_Stream(&Stream) {}
	~BinaryReader() = default;

	inline void Read(void* DataPtr, size_t ByteCount)
	{
		m_Stream->Read(DataPtr, ByteCount, m_Offset);
		m_Offset += ByteCount;
	}

	void Seek(uint64_t Offset)
	{
		ZEN_ASSERT(Offset <= m_Stream->Size());
		m_Offset = Offset;
	}

	void Skip(uint64_t SkipOffset)
	{
		ZEN_ASSERT((m_Offset + SkipOffset) <= m_Stream->Size());
		m_Offset += SkipOffset;
	}

	inline uint64_t CurrentOffset() const { return m_Offset; }
	inline uint64_t AvailableBytes() const { return m_Stream->Size() - m_Offset; }

private:
	RefPtr<InStream> m_Stream;
	uint64_t		 m_Offset = 0;
};

inline BinaryReader&
operator>>(BinaryReader& Reader, bool& Value)
{
	Reader.Read(&Value, sizeof Value);
	return Reader;
}
inline BinaryReader&
operator>>(BinaryReader& Reader, int8_t& Value)
{
	Reader.Read(&Value, sizeof Value);
	return Reader;
}
inline BinaryReader&
operator>>(BinaryReader& Reader, int16_t& Value)
{
	Reader.Read(&Value, sizeof Value);
	return Reader;
}
inline BinaryReader&
operator>>(BinaryReader& Reader, int32_t& Value)
{
	Reader.Read(&Value, sizeof Value);
	return Reader;
}
inline BinaryReader&
operator>>(BinaryReader& Reader, int64_t& Value)
{
	Reader.Read(&Value, sizeof Value);
	return Reader;
}
inline BinaryReader&
operator>>(BinaryReader& Reader, uint8_t& Value)
{
	Reader.Read(&Value, sizeof Value);
	return Reader;
}
inline BinaryReader&
operator>>(BinaryReader& Reader, uint16_t& Value)
{
	Reader.Read(&Value, sizeof Value);
	return Reader;
}
inline BinaryReader&
operator>>(BinaryReader& Reader, uint32_t& Value)
{
	Reader.Read(&Value, sizeof Value);
	return Reader;
}
inline BinaryReader&
operator>>(BinaryReader& Reader, uint64_t& Value)
{
	Reader.Read(&Value, sizeof Value);
	return Reader;
}

/**
 * Text stream writer
 */

class TextWriter
{
public:
	ZENCORE_API TextWriter(OutStream& Stream);
	ZENCORE_API ~TextWriter();

	ZENCORE_API virtual void Write(const void* DataPtr, size_t ByteCount);
	ZENCORE_API void		 Writef(const char* FormatString, ...);

	inline uint64_t CurrentOffset() const { return m_CurrentOffset; }

private:
	RefPtr<OutStream> m_Stream;
	uint64_t		  m_CurrentOffset = 0;
};

ZENCORE_API TextWriter& operator<<(TextWriter& Writer, const char* Value);
ZENCORE_API TextWriter& operator<<(TextWriter& Writer, const std::string_view& Value);
ZENCORE_API TextWriter& operator<<(TextWriter& Writer, bool Value);
ZENCORE_API TextWriter& operator<<(TextWriter& Writer, int8_t Value);
ZENCORE_API TextWriter& operator<<(TextWriter& Writer, int16_t Value);
ZENCORE_API TextWriter& operator<<(TextWriter& Writer, int32_t Value);
ZENCORE_API TextWriter& operator<<(TextWriter& Writer, int64_t Value);
ZENCORE_API TextWriter& operator<<(TextWriter& Writer, uint8_t Value);
ZENCORE_API TextWriter& operator<<(TextWriter& Writer, uint16_t Value);
ZENCORE_API TextWriter& operator<<(TextWriter& Writer, uint32_t Value);
ZENCORE_API TextWriter& operator<<(TextWriter& Writer, uint64_t Value);

class IndentTextWriter : public TextWriter
{
public:
	ZENCORE_API IndentTextWriter(OutStream& stream);
	ZENCORE_API ~IndentTextWriter();

	ZENCORE_API virtual void Write(const void* DataPtr, size_t ByteCount) override;

	inline void Indent(int Amount) { m_IndentAmount += Amount; }

	struct Scope
	{
		Scope(IndentTextWriter& Outer, int IndentAmount = 2) : m_Outer(Outer), m_IndentAmount(IndentAmount)
		{
			m_Outer.Indent(IndentAmount);
		}

		~Scope() { m_Outer.Indent(-m_IndentAmount); }

	private:
		IndentTextWriter& m_Outer;
		int				  m_IndentAmount;
	};

private:
	int	 m_IndentAmount = 0;
	int	 m_LineCursor	= 0;
	char m_LineBuffer[2048];
};

void stream_forcelink();  // internal

}  // namespace zen

