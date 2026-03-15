// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <zencore/windows.h>

#include <winioctl.h>

#include <filesystem>

namespace zen {

class UsnJournalReader
{
public:
	UsnJournalReader();
	~UsnJournalReader();

	bool Initialize(std::filesystem::path VolumePath);

private:
	void*					 m_VolumeHandle;
	READ_USN_JOURNAL_DATA_V1 m_ReadUsnJournalData;
	bool					 m_IncursSeekPenalty = true;

	uint8_t* m_JournalReadBuffer = nullptr;
	uint64_t m_ReadBufferSize	 = 64 * 1024;

	struct Frn
	{
		uint8_t IdBytes[16];

		Frn() = default;

		Frn(const FILE_ID_128& Rhs) { memcpy(IdBytes, Rhs.Identifier, sizeof IdBytes); }
		Frn& operator=(const FILE_ID_128& Rhs) { memcpy(IdBytes, Rhs.Identifier, sizeof IdBytes); }

		Frn(const uint64_t& Rhs)
		{
			memcpy(IdBytes, &Rhs, sizeof Rhs);
			memset(&IdBytes[8], 0, 8);
		}

		Frn& operator=(const uint64_t& Rhs)
		{
			memcpy(IdBytes, &Rhs, sizeof Rhs);
			memset(&IdBytes[8], 0, 8);
		}

		std::strong_ordering operator<=>(const Frn&) const = default;
	};

	enum class FileSystemType
	{
		ReFS,
		NTFS
	};

	FileSystemType m_FileSystemType = FileSystemType::NTFS;
};

}  // namespace zen

