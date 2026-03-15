// Copyright Noah Games, Inc. All Rights Reserved.

#include <zencore/filesystem.h>

#include <zencore/except.h>
#include <zencore/fmtutils.h>
#include <zencore/iobuffer.h>
#include <zencore/string.h>
#include <zencore/windows.h>

#include <atlbase.h>
#include <atlfile.h>
#include <winioctl.h>
#include <winnt.h>
#include <filesystem>

#include <spdlog/spdlog.h>

#include <gsl/gsl-lite.hpp>

namespace zen {

using namespace std::literals;

static bool
DeleteReparsePoint(const wchar_t* Path, DWORD dwReparseTag)
{
	CHandle hDir(CreateFileW(Path,
							 GENERIC_WRITE,
							 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
							 nullptr,
							 OPEN_EXISTING,
							 FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
							 nullptr));

	if (hDir != INVALID_HANDLE_VALUE)
	{
		REPARSE_GUID_DATA_BUFFER Rgdb = {};
		Rgdb.ReparseTag				  = dwReparseTag;

		DWORD	   dwBytes;
		const BOOL bOK =
			DeviceIoControl(hDir, FSCTL_DELETE_REPARSE_POINT, &Rgdb, REPARSE_GUID_DATA_BUFFER_HEADER_SIZE, nullptr, 0, &dwBytes, nullptr);

		return bOK == TRUE;
	}

	return false;
}

bool
CreateDirectories(const wchar_t* Dir)
{
	return std::filesystem::create_directories(Dir);
}

bool
CreateDirectories(const std::filesystem::path& Dir)
{
	return std::filesystem::create_directories(Dir);
}

// Erase all files and directories in a given directory, leaving an empty directory
// behind

static bool
WipeDirectory(const wchar_t* DirPath)
{
	ExtendableWideStringBuilder<128> Pattern;
	Pattern.Append(DirPath);
	Pattern.Append(L"\\*");

	WIN32_FIND_DATAW FindData;
	HANDLE			 hFind = FindFirstFileW(Pattern.c_str(), &FindData);

	bool AllOk = true;

	if (hFind != nullptr)
	{
		do
		{
			bool IsRegular = true;

			if (FindData.cFileName[0] == L'.')
			{
				if (FindData.cFileName[1] == L'.')
				{
					if (FindData.cFileName[2] == L'\0')
					{
						IsRegular = false;
					}
				}
				else if (FindData.cFileName[1] == L'\0')
				{
					IsRegular = false;
				}
			}

			if (IsRegular)
			{
				ExtendableWideStringBuilder<128> Path;
				Path.Append(DirPath);
				Path.Append(L'\\');
				Path.Append(FindData.cFileName);

				// if (fd.dwFileAttributes & FILE_ATTRIBUTE_RECALL_ON_OPEN)
				//	deleteReparsePoint(path.c_str(), fd.dwReserved0);

				if (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				{
					if (FindData.dwFileAttributes & FILE_ATTRIBUTE_RECALL_ON_OPEN)
					{
						DeleteReparsePoint(Path.c_str(), FindData.dwReserved0);
					}

					if (FindData.dwFileAttributes & FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS)
					{
						DeleteReparsePoint(Path.c_str(), FindData.dwReserved0);
					}

					bool Success = DeleteDirectories(Path.c_str());

					if (!Success)
					{
						if (FindData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
						{
							DeleteReparsePoint(Path.c_str(), FindData.dwReserved0);
						}
					}
				}
				else
				{
					DeleteFileW(Path.c_str());
				}
			}
		} while (FindNextFileW(hFind, &FindData) == TRUE);
	}

	FindClose(hFind);

	return true;
}

bool
DeleteDirectories(const wchar_t* DirPath)
{
	return WipeDirectory(DirPath) && RemoveDirectoryW(DirPath) == TRUE;
}

bool
CleanDirectory(const wchar_t* DirPath)
{
	if (std::filesystem::exists(DirPath))
	{
		return WipeDirectory(DirPath);
	}
	else
	{
		return CreateDirectories(DirPath);
	}
}

bool
DeleteDirectories(const std::filesystem::path& Dir)
{
	return DeleteDirectories(Dir.c_str());
}

bool
CleanDirectory(const std::filesystem::path& Dir)
{
	return CleanDirectory(Dir.c_str());
}

//////////////////////////////////////////////////////////////////////////

bool
SupportsBlockRefCounting(std::filesystem::path Path)
{
	ATL::CHandle Handle(CreateFileW(Path.c_str(),
									GENERIC_READ,
									FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
									nullptr,
									OPEN_EXISTING,
									FILE_FLAG_BACKUP_SEMANTICS,
									nullptr));

	if (Handle == INVALID_HANDLE_VALUE)
	{
		Handle.Detach();
		return false;
	}

	ULONG FileSystemFlags = 0;
	if (!GetVolumeInformationByHandleW(Handle, nullptr, 0, nullptr, nullptr, /* lpFileSystemFlags */ &FileSystemFlags, nullptr, 0))
	{
		return false;
	}

	if (!(FileSystemFlags & FILE_SUPPORTS_BLOCK_REFCOUNTING))
	{
		return false;
	}

	return true;
}

bool
CloneFile(std::filesystem::path FromPath, std::filesystem::path ToPath)
{
	ATL::CHandle FromFile(CreateFileW(FromPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
	if (FromFile == INVALID_HANDLE_VALUE)
	{
		FromFile.Detach();
		return false;
	}

	ULONG FileSystemFlags;
	if (!GetVolumeInformationByHandleW(FromFile, nullptr, 0, nullptr, nullptr, /* lpFileSystemFlags */ &FileSystemFlags, nullptr, 0))
	{
		return false;
	}
	if (!(FileSystemFlags & FILE_SUPPORTS_BLOCK_REFCOUNTING))
	{
		SetLastError(ERROR_NOT_CAPABLE);
		return false;
	}

	FILE_END_OF_FILE_INFO FileSize;
	if (!GetFileSizeEx(FromFile, &FileSize.EndOfFile))
	{
		return false;
	}

	FILE_BASIC_INFO BasicInfo;
	if (!GetFileInformationByHandleEx(FromFile, FileBasicInfo, &BasicInfo, sizeof BasicInfo))
	{
		return false;
	}

	DWORD								   dwBytesReturned = 0;
	FSCTL_GET_INTEGRITY_INFORMATION_BUFFER GetIntegrityInfoBuffer;
	if (!DeviceIoControl(FromFile,
						 FSCTL_GET_INTEGRITY_INFORMATION,
						 nullptr,
						 0,
						 &GetIntegrityInfoBuffer,
						 sizeof GetIntegrityInfoBuffer,
						 &dwBytesReturned,
						 nullptr))
	{
		return false;
	}

	SetFileAttributesW(ToPath.c_str(), FILE_ATTRIBUTE_NORMAL);

	ATL::CHandle TargetFile(CreateFileW(ToPath.c_str(),
										GENERIC_READ | GENERIC_WRITE | DELETE,
										/* no sharing */ FILE_SHARE_READ,
										nullptr,
										OPEN_ALWAYS,
										0,
										/* hTemplateFile */ FromFile));

	if (TargetFile == INVALID_HANDLE_VALUE)
	{
		TargetFile.Detach();
		return false;
	}

	// Delete target file when handle is closed (we only reset this if the copy succeeds)
	FILE_DISPOSITION_INFO FileDisposition = {TRUE};
	if (!SetFileInformationByHandle(TargetFile, FileDispositionInfo, &FileDisposition, sizeof FileDisposition))
	{
		TargetFile.Close();
		DeleteFileW(ToPath.c_str());
		return false;
	}

	// Make file sparse so we don't end up allocating space when we change the file size
	if (!DeviceIoControl(TargetFile, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &dwBytesReturned, nullptr))
	{
		return false;
	}

	// Copy integrity checking information
	FSCTL_SET_INTEGRITY_INFORMATION_BUFFER IntegritySet = {GetIntegrityInfoBuffer.ChecksumAlgorithm,
														   GetIntegrityInfoBuffer.Reserved,
														   GetIntegrityInfoBuffer.Flags};
	if (!DeviceIoControl(TargetFile, FSCTL_SET_INTEGRITY_INFORMATION, &IntegritySet, sizeof IntegritySet, nullptr, 0, nullptr, nullptr))
	{
		return false;
	}

	// Resize file - note that the file is sparse at this point so no additional data will be written
	if (!SetFileInformationByHandle(TargetFile, FileEndOfFileInfo, &FileSize, sizeof FileSize))
	{
		return false;
	}

	constexpr auto RoundToClusterSize = [](LONG64 FileSize, ULONG ClusterSize) -> LONG64 {
		return (FileSize + ClusterSize - 1) / ClusterSize * ClusterSize;
	};
	static_assert(RoundToClusterSize(5678, 4 * 1024) == 8 * 1024);

	// Loop for cloning file contents. This is necessary as the API has a 32-bit size
	// limit for some reason

	const LONG64 SplitThreshold = (1LL << 32) - GetIntegrityInfoBuffer.ClusterSizeInBytes;

	DUPLICATE_EXTENTS_DATA DuplicateExtentsData{.FileHandle = FromFile};

	for (LONG64 CurrentByteOffset = 0,
				RemainingBytes	  = RoundToClusterSize(FileSize.EndOfFile.QuadPart, GetIntegrityInfoBuffer.ClusterSizeInBytes);
		 RemainingBytes > 0;
		 CurrentByteOffset += SplitThreshold, RemainingBytes -= SplitThreshold)
	{
		DuplicateExtentsData.SourceFileOffset.QuadPart = CurrentByteOffset;
		DuplicateExtentsData.TargetFileOffset.QuadPart = CurrentByteOffset;
		DuplicateExtentsData.ByteCount.QuadPart		   = std::min(SplitThreshold, RemainingBytes);

		if (!DeviceIoControl(TargetFile,
							 FSCTL_DUPLICATE_EXTENTS_TO_FILE,
							 &DuplicateExtentsData,
							 sizeof DuplicateExtentsData,
							 nullptr,
							 0,
							 &dwBytesReturned,
							 nullptr))
		{
			return false;
		}
	}

	// Make the file not sparse again now that we have populated the contents
	if (!(BasicInfo.FileAttributes & FILE_ATTRIBUTE_SPARSE_FILE))
	{
		FILE_SET_SPARSE_BUFFER SetSparse = {FALSE};

		if (!DeviceIoControl(TargetFile, FSCTL_SET_SPARSE, &SetSparse, sizeof SetSparse, nullptr, 0, &dwBytesReturned, nullptr))
		{
			return false;
		}
	}

	// Update timestamps (but don't lie about the creation time)
	BasicInfo.CreationTime.QuadPart = 0;
	if (!SetFileInformationByHandle(TargetFile, FileBasicInfo, &BasicInfo, sizeof BasicInfo))
	{
		return false;
	}

	if (!FlushFileBuffers(TargetFile))
	{
		return false;
	}

	// Finally now everything is done - make sure the file is not deleted on close!

	FileDisposition = {FALSE};

	const bool AllOk = (TRUE == SetFileInformationByHandle(TargetFile, FileDispositionInfo, &FileDisposition, sizeof FileDisposition));

	return AllOk;
}

bool
CopyFile(std::filesystem::path FromPath, std::filesystem::path ToPath, const CopyFileOptions& Options)
{
	bool Success = false;

	if (Options.EnableClone)
	{
		Success = CloneFile(FromPath.native(), ToPath.native());

		if (Success)
		{
			return true;
		}
	}

	if (Options.MustClone)
	{
		return false;
	}

	BOOL CancelFlag = FALSE;
	Success			= !!::CopyFileExW(FromPath.c_str(),
							  ToPath.c_str(),
							  /* lpProgressRoutine */ nullptr,
							  /* lpData */ nullptr,
							  &CancelFlag,
							  /* dwCopyFlags */ 0);

	if (!Success)
	{
		throw std::system_error(std::error_code(::GetLastError(), std::system_category()), "file copy failed");
	}

	return Success;
}

bool
WriteFile(std::filesystem::path Path, const IoBuffer* const* Data, size_t BufferCount)
{
	using namespace fmt::literals;

	CAtlFile Outfile;
	HRESULT	 hRes = Outfile.Create(Path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS);
	if (FAILED(hRes))
	{
		zen::ThrowIfFailed(hRes, "File open failed for '{}'"_format(Path).c_str());
	}

	// TODO: this could be block-enlightened

	for (size_t i = 0; i < BufferCount; ++i)
	{
		uint64_t	WriteSize = Data[i]->Size();
		const void* DataPtr	  = Data[i]->Data();

		while (WriteSize)
		{
			uint64_t ChunkSize = zen::Min<uint64_t>(WriteSize, uint64_t(2) * 1024 * 1024 * 1024);

			hRes = Outfile.Write(DataPtr, gsl::narrow_cast<uint32_t>(WriteSize));

			if (FAILED(hRes))
			{
				zen::ThrowIfFailed(hRes, "File write failed for '{}'"_format(Path).c_str());
			}

			WriteSize -= ChunkSize;
			DataPtr = reinterpret_cast<const uint8_t*>(DataPtr) + ChunkSize;
		}
	}

	return true;
}

FileContents
ReadFile(std::filesystem::path Path)
{
	ATL::CHandle FromFile(CreateFileW(Path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
	if (FromFile == INVALID_HANDLE_VALUE)
	{
		FromFile.Detach();
		return FileContents{.ErrorCode = std::error_code(::GetLastError(), std::system_category())};
	}

	FILE_END_OF_FILE_INFO FileSize;
	if (!GetFileSizeEx(FromFile, &FileSize.EndOfFile))
	{
		return FileContents{.ErrorCode = std::error_code(::GetLastError(), std::system_category())};
	}

	const uint64_t FileSizeBytes = FileSize.EndOfFile.QuadPart;

	FileContents Contents;

	Contents.Data.emplace_back(IoBuffer(IoBuffer::File, FromFile.Detach(), 0, FileSizeBytes));

	return Contents;
}

bool
ScanFile(std::filesystem::path Path, const uint64_t ChunkSize, std::function<void(const void* Data, size_t Size)>&& ProcessFunc)
{
	ATL::CHandle FromFile(CreateFileW(Path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
	if (FromFile == INVALID_HANDLE_VALUE)
	{
		FromFile.Detach();
		return false;
	}

	std::vector<uint8_t> ReadBuffer(ChunkSize);

	for (;;)
	{
		DWORD dwBytesRead = 0;
		BOOL  Success	  = ::ReadFile(FromFile, ReadBuffer.data(), (DWORD)ReadBuffer.size(), &dwBytesRead, nullptr);

		if (!Success)
		{
			throw std::system_error(std::error_code(::GetLastError(), std::system_category()), "file scan failed");
		}

		if (dwBytesRead == 0)
			break;

		ProcessFunc(ReadBuffer.data(), dwBytesRead);
	}

	return true;
}

std::string
ToUtf8(const std::filesystem::path& Path)
{
	return WideToUtf8(Path.native().c_str());
}

void
FileSystemTraversal::TraverseFileSystem(const std::filesystem::path& RootDir, TreeVisitor& Visitor)
{
	uint64_t FileInfoBuffer[8 * 1024];

	FILE_INFO_BY_HANDLE_CLASS FibClass = FileIdBothDirectoryRestartInfo;
	bool					  Continue = true;

	std::wstring RootDirPath = RootDir.native();

	CAtlFile RootDirHandle;
	HRESULT	 hRes = RootDirHandle.Create(RootDirPath.c_str(),
										 GENERIC_READ,
										 FILE_SHARE_READ | FILE_SHARE_WRITE,
										 OPEN_EXISTING,
										 FILE_FLAG_BACKUP_SEMANTICS);

	zen::ThrowIfFailed(hRes, "Failed to open handle to volume root");

	while (Continue)
	{
		BOOL Success = GetFileInformationByHandleEx(RootDirHandle, FibClass, FileInfoBuffer, sizeof FileInfoBuffer);
		FibClass	 = FileIdBothDirectoryInfo;	 // Set up for next iteration

		uint64_t EntryOffset = 0;

		if (!Success)
		{
			DWORD LastError = GetLastError();

			if (LastError == ERROR_NO_MORE_FILES)
			{
				break;
			}

			throw std::system_error(std::error_code(LastError, std::system_category()), "file system traversal error");
		}

		for (;;)
		{
			const FILE_ID_BOTH_DIR_INFO* DirInfo =
				reinterpret_cast<const FILE_ID_BOTH_DIR_INFO*>(reinterpret_cast<const uint8_t*>(FileInfoBuffer) + EntryOffset);

			std::wstring_view FileName(DirInfo->FileName, DirInfo->FileNameLength / sizeof(wchar_t));

			if (DirInfo->FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				if (FileName == L"."sv || FileName == L".."sv)
				{
					// Not very interesting
				}
				else
				{
					const bool ShouldDescend = Visitor.VisitDirectory(RootDir, FileName);

					if (ShouldDescend)
					{
						// Note that this recursion combined with the buffer could
						// blow the stack, we should consider a different strategy

						std::filesystem::path FullPath = RootDir / FileName;

						TraverseFileSystem(FullPath, Visitor);
					}
				}
			}
			else if (DirInfo->FileAttributes & FILE_ATTRIBUTE_DEVICE)
			{
				spdlog::warn("encountered device node during file system traversal: {} found in {}", WideToUtf8(FileName), RootDir);
			}
			else
			{
				std::filesystem::path FullPath = RootDir / FileName;

				Visitor.VisitFile(RootDir, FileName, DirInfo->EndOfFile.QuadPart);
			}

			const uint64_t NextOffset = DirInfo->NextEntryOffset;

			if (NextOffset == 0)
			{
				break;
			}

			EntryOffset += NextOffset;
		}
	}
}

std::filesystem::path
PathFromHandle(void* NativeHandle)
{
	if (NativeHandle == nullptr || NativeHandle == INVALID_HANDLE_VALUE)
	{
		return std::filesystem::path();
	}

	const DWORD RequiredLengthIncludingNul = GetFinalPathNameByHandleW(NativeHandle, nullptr, 0, FILE_NAME_OPENED);

	std::wstring FullPath;
	FullPath.resize(RequiredLengthIncludingNul - 1);

	const DWORD FinalLength = GetFinalPathNameByHandleW(NativeHandle, FullPath.data(), RequiredLengthIncludingNul, FILE_NAME_OPENED);

	return FullPath;
}

}  // namespace zen

