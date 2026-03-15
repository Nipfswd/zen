// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include "stream.h"
#include "zencore.h"

#include <filesystem>
#include <functional>

namespace zen {

class IoBuffer;

/** Delete directory (after deleting any contents)
 */
ZENCORE_API bool DeleteDirectories(const wchar_t* dir);
ZENCORE_API bool DeleteDirectories(const std::filesystem::path& dir);

/** Ensure directory exists.

	Will also create any required parent directories
 */
ZENCORE_API bool CreateDirectories(const wchar_t* dir);
ZENCORE_API bool CreateDirectories(const std::filesystem::path& dir);

/** Ensure directory exists and delete contents (if any) before returning
 */
ZENCORE_API bool CleanDirectory(const wchar_t* dir);
ZENCORE_API bool CleanDirectory(const std::filesystem::path& dir);

/** Map native file handle to a path
 */
ZENCORE_API std::filesystem::path PathFromHandle(void* NativeHandle);

struct FileContents
{
	std::vector<IoBuffer> Data;
	std::error_code		  ErrorCode;
};

ZENCORE_API FileContents ReadFile(std::filesystem::path Path);
ZENCORE_API bool ScanFile(std::filesystem::path Path, uint64_t ChunkSize, std::function<void(const void* Data, size_t Size)>&& ProcessFunc);
ZENCORE_API bool WriteFile(std::filesystem::path Path, const IoBuffer* const* Data, size_t BufferCount);

struct CopyFileOptions
{
	bool EnableClone = true;
	bool MustClone	 = false;
};

ZENCORE_API bool CopyFile(std::filesystem::path FromPath, std::filesystem::path ToPath, const CopyFileOptions& Options);
ZENCORE_API bool SupportsBlockRefCounting(std::filesystem::path Path);

ZENCORE_API std::string ToUtf8(const std::filesystem::path& Path);

/**
 * Efficient file system traversal
 *
 * Uses the best available mechanism for the platform in question and could take
 * advantage of any file system tracking mechanisms in the future
 *
 */
class FileSystemTraversal
{
public:
	struct TreeVisitor
	{
		virtual void VisitFile(const std::filesystem::path& Parent, const std::wstring_view& File, uint64_t FileSize) = 0;

		// This should return true if we should recurse into the directory
		virtual bool VisitDirectory(const std::filesystem::path& Parent, const std::wstring_view& DirectoryName) = 0;
	};

	void TraverseFileSystem(const std::filesystem::path& RootDir, TreeVisitor& Visitor);
};

}  // namespace zen

