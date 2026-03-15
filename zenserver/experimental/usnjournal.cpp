// Copyright Noah Games, Inc. All Rights Reserved.

#include "usnjournal.h"

#include <zencore/except.h>
#include <zencore/timer.h>
#include <zencore/zencore.h>

#include <spdlog/spdlog.h>

#include <atlfile.h>

#include <filesystem>

namespace zen {

UsnJournalReader::UsnJournalReader()
{
}

UsnJournalReader::~UsnJournalReader()
{
	delete[] m_JournalReadBuffer;
}

bool
UsnJournalReader::Initialize(std::filesystem::path VolumePath)
{
	TCHAR VolumeName[MAX_PATH];
	TCHAR VolumePathName[MAX_PATH];

	{
		auto NativePath = VolumePath.native();
		BOOL Success	= GetVolumePathName(NativePath.c_str(), VolumePathName, ZEN_ARRAY_COUNT(VolumePathName));

		if (!Success)
		{
			zen::ThrowSystemException("GetVolumePathName failed");
		}

		Success = GetVolumeNameForVolumeMountPoint(VolumePathName, VolumeName, ZEN_ARRAY_COUNT(VolumeName));

		if (!Success)
		{
			zen::ThrowSystemException("GetVolumeNameForVolumeMountPoint failed");
		}

		// Chop off trailing slash since we want to open a volume handle, not a handle to the volume root directory

		const size_t VolumeNameLength = wcslen(VolumeName);

		if (VolumeNameLength)
		{
			VolumeName[VolumeNameLength - 1] = '\0';
		}
	}

	m_VolumeHandle = CreateFile(VolumeName,
								GENERIC_READ | GENERIC_WRITE,
								FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
								nullptr, /* no custom security */
								OPEN_EXISTING,
								FILE_FLAG_BACKUP_SEMANTICS,
								nullptr); /* template */

	if (m_VolumeHandle == INVALID_HANDLE_VALUE)
	{
		ThrowSystemException("Volume handle open failed");
	}

	// Figure out which file system is in use for volume

	{
		WCHAR InfoVolumeName[MAX_PATH + 1]{};
		WCHAR FileSystemName[MAX_PATH + 1]{};
		DWORD MaximumComponentLength = 0;
		DWORD FileSystemFlags		 = 0;

		BOOL Success = GetVolumeInformationByHandleW(m_VolumeHandle,
													 InfoVolumeName,
													 MAX_PATH + 1,
													 NULL,
													 &MaximumComponentLength,
													 &FileSystemFlags,
													 FileSystemName,
													 ZEN_ARRAY_COUNT(FileSystemName));

		if (!Success)
		{
			ThrowSystemException("Failed to get volume information");
		}

		spdlog::debug("File system type is {}", WideToUtf8(FileSystemName));

		if (wcscmp(L"ReFS", FileSystemName) == 0)
		{
			m_FileSystemType = FileSystemType::ReFS;
		}
		else if (wcscmp(L"NTFS", FileSystemName) == 0)
		{
			m_FileSystemType = FileSystemType::NTFS;
		}
		else
		{
			// Unknown file system type!
		}
	}

	// Determine if volume is on fast storage, where seeks aren't so expensive

	{
		STORAGE_PROPERTY_QUERY StorageQuery{};
		StorageQuery.PropertyId = StorageDeviceSeekPenaltyProperty;
		StorageQuery.QueryType	= PropertyStandardQuery;
		DWORD						   BytesWritten;
		DEVICE_SEEK_PENALTY_DESCRIPTOR Result{};

		if (DeviceIoControl(m_VolumeHandle,
							IOCTL_STORAGE_QUERY_PROPERTY,
							&StorageQuery,
							sizeof(StorageQuery),
							&Result,
							sizeof(Result),
							&BytesWritten,
							nullptr))
		{
			m_IncursSeekPenalty = !!Result.IncursSeekPenalty;
		}
	}

	// Query Journal

	USN_JOURNAL_DATA_V2 UsnData{};

	{
		DWORD BytesWritten = 0;

		const BOOL Success =
			DeviceIoControl(m_VolumeHandle, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &UsnData, sizeof UsnData, &BytesWritten, nullptr);

		if (!Success)
		{
			switch (DWORD Error = GetLastError())
			{
				case ERROR_JOURNAL_NOT_ACTIVE:
					spdlog::info("No USN journal active on drive");

					// TODO: optionally activate USN journal on drive?

					ThrowSystemException(HRESULT_FROM_WIN32(Error), "No USN journal active on drive");
					break;

				default:
					ThrowSystemException(HRESULT_FROM_WIN32(Error), "FSCTL_QUERY_USN_JOURNAL failed");
			}
		}
	}

	m_JournalReadBuffer = new uint8_t[m_ReadBufferSize];

	// Catch up to USN start

	CAtlFile VolumeRootDir;
	HRESULT	 hRes =
		VolumeRootDir.Create(VolumePathName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS);

	ThrowIfFailed(hRes, "Failed to open handle to volume root");

	FILE_ID_INFO FileInformation{};
	BOOL		 Success = GetFileInformationByHandleEx(VolumeRootDir, FileIdInfo, &FileInformation, sizeof FileInformation);

	if (!Success)
	{
		ThrowSystemException("GetFileInformationByHandleEx failed");
	}

	const Frn VolumeRootFrn = FileInformation.FileId;

	// Enumerate MFT (but not for ReFS)

	if (m_FileSystemType == FileSystemType::NTFS)
	{
		spdlog::info("Enumerating MFT for {}", WideToUtf8(VolumePathName));

		zen::Stopwatch Timer;
		uint64_t	   MftBytesProcessed = 0;

		MFT_ENUM_DATA_V1 MftEnumData{.StartFileReferenceNumber = 0, .LowUsn = 0, .HighUsn = 0, .MinMajorVersion = 2, .MaxMajorVersion = 3};

		BYTE  MftBuffer[64 * 1024 + sizeof(DWORDLONG)];
		DWORD BytesWritten = 0;

		for (;;)
		{
			Success = DeviceIoControl(m_VolumeHandle,
									  FSCTL_ENUM_USN_DATA,
									  &MftEnumData,
									  sizeof MftEnumData,
									  MftBuffer,
									  sizeof MftBuffer,
									  &BytesWritten,
									  nullptr);

			if (!Success)
			{
				DWORD Error = GetLastError();

				if (Error == ERROR_HANDLE_EOF)
				{
					break;
				}

				ThrowSystemException(HRESULT_FROM_WIN32(Error), "FSCTL_ENUM_USN_DATA failed");
			}

			void* BufferEnd = (void*)&MftBuffer[BytesWritten];

			// The enumeration call returns the next FRN ahead of the other data in the buffer
			MftEnumData.StartFileReferenceNumber = ((DWORDLONG*)MftBuffer)[0];

			PUSN_RECORD_UNION CommonRecord = PUSN_RECORD_UNION(&((DWORDLONG*)MftBuffer)[1]);

			while (CommonRecord < BufferEnd)
			{
				switch (CommonRecord->Header.MajorVersion)
				{
					case 2:
						{
							USN_RECORD_V2& Record = CommonRecord->V2;

							const Frn		  FileReference	  = Record.FileReferenceNumber;
							const Frn		  ParentReference = Record.ParentFileReferenceNumber;
							std::wstring_view FileName{Record.FileName, Record.FileNameLength};
						}
						break;
					case 3:
						{
							USN_RECORD_V3& Record = CommonRecord->V3;

							const Frn		  FileReference	  = Record.FileReferenceNumber;
							const Frn		  ParentReference = Record.ParentFileReferenceNumber;
							std::wstring_view FileName{Record.FileName, Record.FileNameLength};
						}
						break;
					case 4:
						{
							// This captures file modification ranges. We do not yet support this however
							USN_RECORD_V4& Record = CommonRecord->V4;
						}
						break;
				}

				const DWORD RecordLength = CommonRecord->Header.RecordLength;
				CommonRecord			 = PUSN_RECORD_UNION(((uint8_t*)CommonRecord) + RecordLength);
				MftBytesProcessed += RecordLength;
			}
		}

		const auto ElapsedMs = Timer.getElapsedTimeMs();

		spdlog::info("MFT enumeration of {} completed after {} ({})",
					 zen::NiceBytes(MftBytesProcessed),
					 zen::NiceTimeSpanMs(ElapsedMs),
					 zen::NiceByteRate(MftBytesProcessed, ElapsedMs));
	}

	// Populate by traversal
	if (m_FileSystemType == FileSystemType::ReFS)
	{
		uint64_t FileInfoBuffer[8 * 1024];

		FILE_INFO_BY_HANDLE_CLASS FibClass = FileIdBothDirectoryRestartInfo;
		bool					  Continue = true;

		while (Continue)
		{
			Success	 = GetFileInformationByHandleEx(VolumeRootDir, FibClass, FileInfoBuffer, sizeof FileInfoBuffer);
			FibClass = FileIdBothDirectoryInfo;	 // Set up for next iteration

			uint64_t EntryOffset = 0;

			if (!Success)
			{
				// Report failure?

				break;
			}

			do
			{
				const FILE_ID_BOTH_DIR_INFO* DirInfo =
					reinterpret_cast<const FILE_ID_BOTH_DIR_INFO*>(reinterpret_cast<const uint8_t*>(FileInfoBuffer) + EntryOffset);

				const uint64_t NextOffset = DirInfo->NextEntryOffset;

				if (NextOffset == 0)
				{
					if (EntryOffset == 0)
					{
						// First and last - end of iteration
						Continue = false;
					}
					break;
				}

				if (DirInfo->FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				{
					// TODO Directory
				}
				else if (DirInfo->FileAttributes & FILE_ATTRIBUTE_DEVICE)
				{
					// TODO Device
				}
				else
				{
					// TODO File
				}

				EntryOffset += DirInfo->NextEntryOffset;
			} while (EntryOffset);
		}
	}

	// Initialize journal reading

	m_ReadUsnJournalData = {.StartUsn	= UsnData.FirstUsn,
							.ReasonMask = USN_REASON_BASIC_INFO_CHANGE | USN_REASON_CLOSE | USN_REASON_DATA_EXTEND |
										  USN_REASON_DATA_OVERWRITE | USN_REASON_DATA_TRUNCATION | USN_REASON_FILE_CREATE |
										  USN_REASON_FILE_DELETE | USN_REASON_HARD_LINK_CHANGE | USN_REASON_RENAME_NEW_NAME |
										  USN_REASON_RENAME_OLD_NAME | USN_REASON_REPARSE_POINT_CHANGE,
							.ReturnOnlyOnClose = true,
							.Timeout		   = 0,
							.BytesToWaitFor	   = 0,
							.UsnJournalID	   = UsnData.UsnJournalID,
							.MinMajorVersion   = 0,
							.MaxMajorVersion   = 0};

	return false;
}

}  // namespace zen

