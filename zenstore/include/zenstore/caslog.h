// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <zencore/zencore.h>

#include <zencore/iobuffer.h>
#include <zencore/string.h>
#include <zencore/thread.h>
#include <zencore/uid.h>
#include <zencore/windows.h>
#include <zenstore/cas.h>

#include <atlfile.h>
#include <functional>

namespace zen {

class CasLogFile
{
public:
	CasLogFile();
	~CasLogFile();

	void Open(std::filesystem::path FileName, size_t RecordSize, bool isCreate);
	void Append(const void* DataPointer, uint64_t DataSize);
	void Replay(std::function<void(const void*)>&& Handler);
	void Flush();
	void Close();

private:
	struct FileHeader
	{
		uint8_t	 Magic[16];
		uint32_t RecordSize = 0;
		zen::Oid LogId;
		uint32_t ValidatedTail = 0;
		uint32_t Pad[6];
		uint32_t Checksum = 0;

		static const inline uint8_t MagicSequence[16] = {'.', '-', '=', ' ', 'C', 'A', 'S', 'L', 'O', 'G', 'v', '1', ' ', '=', '-', '.'};

		ZENCORE_API uint32_t ComputeChecksum();
		void				 Finalize() { Checksum = ComputeChecksum(); }
	};

	static_assert(sizeof(FileHeader) == 64);

private:
	CAtlFile   m_File;
	FileHeader m_Header;
	size_t	   m_RecordSize	  = 1;
	uint64_t   m_AppendOffset = 0;
};

template<typename T>
class TCasLogFile : public CasLogFile
{
public:
	// This should be called before the Replay() is called to do some basic sanity checking
	bool Initialize() { return true; }

	void Replay(std::invocable<const T&> auto Handler)
	{
		CasLogFile::Replay([&](const void* VoidPtr) {
			const T& Record = *reinterpret_cast<const T*>(VoidPtr);

			Handler(Record);
		});
	}

	void Append(const T& Record) { CasLogFile::Append(&Record, sizeof Record); }
	void Open(std::filesystem::path FileName, bool IsCreate) { CasLogFile::Open(FileName, sizeof(T), IsCreate); }
};

//////////////////////////////////////////////////////////////////////////
//
// This should go in its own header
//

class CasBlobFile
{
public:
	void	 Open(std::filesystem::path FileName, bool IsCreate);
	void	 Read(void* Data, uint64_t Size, uint64_t Offset);
	void	 Write(const void* Data, uint64_t Size, uint64_t Offset);
	void	 Flush();
	uint64_t FileSize();
	void*	 Handle() { return m_File; }
	IoBuffer ReadAll();

private:
	CAtlFile m_File;
};

}  // namespace zen

