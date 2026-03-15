// Copyright Noah Games, Inc. All Rights Reserved.

#include <zenstore/cas.h>

#include "CompactCas.h"

#include <zencore/except.h>
#include <zencore/memory.h>
#include <zencore/string.h>
#include <zencore/thread.h>
#include <zencore/uid.h>

#include <xxhash.h>

#include <gsl/gsl-lite.hpp>

#include <functional>

struct IUnknown;  // Workaround for "combaseapi.h(229): error C2187: syntax error: 'identifier' was unexpected here" when using /permissive-
#include <atlfile.h>
#include <filesystem>

//////////////////////////////////////////////////////////////////////////

namespace zen {

uint32_t
CasLogFile::FileHeader::ComputeChecksum()
{
	return XXH32(&this->Magic, sizeof(FileHeader) - 4, 0xC0C0'BABA);
}

CasLogFile::CasLogFile()
{
}

CasLogFile::~CasLogFile()
{
}

void
CasLogFile::Open(std::filesystem::path FileName, size_t RecordSize, bool IsCreate)
{
	m_RecordSize = RecordSize;

	const DWORD dwCreationDisposition = IsCreate ? CREATE_ALWAYS : OPEN_EXISTING;

	HRESULT hRes = m_File.Create(FileName.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, dwCreationDisposition);

	if (FAILED(hRes))
	{
		throw std::system_error(GetLastError(), std::system_category(), "Failed to open log file" /* TODO: add path */);
	}

	uint64_t AppendOffset = 0;

	if (IsCreate)
	{
		// Initialize log by writing header
		FileHeader Header = {.RecordSize = gsl::narrow<uint32_t>(RecordSize), .LogId = zen::Oid::NewOid(), .ValidatedTail = 0};
		memcpy(Header.Magic, FileHeader::MagicSequence, sizeof Header.Magic);
		Header.Finalize();

		m_File.Write(&Header, sizeof Header);

		AppendOffset = sizeof(FileHeader);

		m_Header = Header;
	}
	else
	{
		// Validate header and log contents and prepare for appending/replay
		FileHeader Header;
		m_File.Read(&Header, sizeof Header);

		if ((0 != memcmp(Header.Magic, FileHeader::MagicSequence, sizeof Header.Magic)) || (Header.Checksum != Header.ComputeChecksum()))
		{
			// TODO: provide more context!
			throw std::exception("Mangled log header");
		}

		ULONGLONG Sz;
		m_File.GetSize(Sz);
		AppendOffset = Sz;

		m_Header = Header;
	}

	m_AppendOffset = AppendOffset;
}

void
CasLogFile::Close()
{
	// TODO: update header and maybe add trailer

	Flush();
}

void
CasLogFile::Replay(std::function<void(const void*)>&& Handler)
{
	ULONGLONG LogFileSize;
	m_File.GetSize(LogFileSize);

	// Ensure we end up on a clean boundary
	const uint64_t LogBaseOffset = sizeof(FileHeader);
	const size_t   LogEntryCount = (LogFileSize - LogBaseOffset) / m_RecordSize;

	if (LogEntryCount == 0)
	{
		return;
	}

	const uint64_t LogDataSize = LogEntryCount * m_RecordSize;

	std::vector<uint8_t> ReadBuffer;
	ReadBuffer.resize(LogDataSize);

	m_File.Seek(LogBaseOffset, FILE_BEGIN);
	HRESULT hRes = m_File.Read(ReadBuffer.data(), gsl::narrow<DWORD>(LogDataSize));

	zen::ThrowIfFailed(hRes, "Failed to read log file");

	for (int i = 0; i < LogEntryCount; ++i)
	{
		Handler(ReadBuffer.data() + (i * m_RecordSize));
	}
}

void
CasLogFile::Append(const void* DataPointer, uint64_t DataSize)
{
	HRESULT hRes = m_File.Write(DataPointer, gsl::narrow<DWORD>(DataSize));

	if (FAILED(hRes))
	{
		throw std::system_error(GetLastError(), std::system_category(), "Failed to write to log file" /* TODO: add context */);
	}
}

void
CasLogFile::Flush()
{
	m_File.Flush();
}

//////////////////////////////////////////////////////////////////////////

void
CasBlobFile::Open(std::filesystem::path FileName, bool isCreate)
{
	const DWORD dwCreationDisposition = isCreate ? CREATE_ALWAYS : OPEN_EXISTING;

	HRESULT hRes = m_File.Create(FileName.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, dwCreationDisposition);

	if (FAILED(hRes))
	{
		throw std::system_error(GetLastError(), std::system_category(), "Failed to open bucket sobs file");
	}
}

void
CasBlobFile::Read(void* Data, uint64_t Size, uint64_t Offset)
{
	OVERLAPPED Ovl{};

	Ovl.Offset	   = DWORD(Offset & 0xffff'ffffu);
	Ovl.OffsetHigh = DWORD(Offset >> 32);

	HRESULT hRes = m_File.Read(Data, gsl::narrow<DWORD>(Size), &Ovl);

	if (FAILED(hRes))
	{
		throw std::system_error(GetLastError(), std::system_category(), "Failed to read from file" /* TODO: add context */);
	}
}

IoBuffer
CasBlobFile::ReadAll()
{
	IoBuffer Buffer(FileSize());

	Read((void*)Buffer.Data(), Buffer.Size(), 0);

	return Buffer;
}

void
CasBlobFile::Write(const void* Data, uint64_t Size, uint64_t Offset)
{
	OVERLAPPED Ovl{};

	Ovl.Offset	   = DWORD(Offset & 0xffff'ffffu);
	Ovl.OffsetHigh = DWORD(Offset >> 32);

	HRESULT hRes = m_File.Write(Data, gsl::narrow<DWORD>(Size), &Ovl);

	if (FAILED(hRes))
	{
		throw std::system_error(GetLastError(), std::system_category(), "Failed to write to file" /* TODO: add context */);
	}
}

void
CasBlobFile::Flush()
{
	m_File.Flush();
}

uint64_t
CasBlobFile::FileSize()
{
	ULONGLONG Sz;
	m_File.GetSize(Sz);

	return uint64_t(Sz);
}

}  // namespace zen

