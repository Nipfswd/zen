// Copyright Noah Games, Inc. All Rights Reserved.

#include "FileCas.h"

#include <zencore/except.h>
#include <zencore/memory.h>
#include <zencore/string.h>
#include <zencore/thread.h>
#include <zencore/uid.h>

#include <gsl/gsl-lite.hpp>

#include <functional>
#include <unordered_map>

struct IUnknown;  // Workaround for "combaseapi.h(229): error C2187: syntax error: 'identifier' was unexpected here" when using /permissive-
#include <atlfile.h>
#include <filesystem>

// Used for getting My Documents for default CAS
#include <ShlObj.h>
#pragma comment(lib, "shell32.lib")

//////////////////////////////////////////////////////////////////////////

namespace zen {

WideStringBuilderBase&
FileCasStrategy::MakeShardedPath(WideStringBuilderBase& ShardedPath, const IoHash& ChunkHash, size_t& OutShard2len)
{
	ExtendableStringBuilder<96> HashString;
	ChunkHash.ToHexString(HashString);

	const char* str = HashString.c_str();

	// Shard into a path with two directory levels containing 12 bits and 8 bits
	// respectively.
	//
	// This results in a maximum of 4096 * 256 directories
	//
	// The numbers have been chosen somewhat arbitrarily but are large to scale
	// to very large chunk repositories. It may or may not make sense to make
	// this a configurable policy, and it would probably be a good idea to
	// measure performance for different policies and chunk counts

	ShardedPath.AppendAsciiRange(str, str + 3);

	ShardedPath.Append('\\');
	ShardedPath.AppendAsciiRange(str + 3, str + 5);
	OutShard2len = ShardedPath.Size();

	ShardedPath.Append('\\');
	ShardedPath.AppendAsciiRange(str + 6, str + 64);

	return ShardedPath;
}

CasStore::InsertResult
FileCasStrategy::InsertChunk(IoBuffer Chunk, const IoHash& ChunkHash)
{
	return InsertChunk(Chunk.Data(), Chunk.Size(), ChunkHash);
}

CasStore::InsertResult
FileCasStrategy::InsertChunk(const void* const ChunkData, const size_t ChunkSize, const IoHash& ChunkHash)
{
	size_t							 Shard2len = 0;
	ExtendableWideStringBuilder<128> ShardedPath;
	ShardedPath.Append(m_Config.RootDirectory.c_str());
	ShardedPath.Append(std::filesystem::path::preferred_separator);
	MakeShardedPath(ShardedPath, ChunkHash, /* out */ Shard2len);

	// See if file already exists
	//
	// Future improvement: maintain Bloom filter to avoid expensive file system probes?

	CAtlFile PayloadFile;

	HRESULT hRes = PayloadFile.Create(ShardedPath.c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING);

	if (SUCCEEDED(hRes))
	{
		// If we succeeded in opening the file then we don't need to do anything else because it already exists and should contain the
		// content we were about to insert
		return CasStore::InsertResult{.New = false};
	}

	PayloadFile.Close();

	RwLock::ExclusiveLockScope _(LockForHash(ChunkHash));

	// For now, use double-checked locking to see if someone else was first

	hRes = PayloadFile.Create(ShardedPath.c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING);

	if (SUCCEEDED(hRes))
	{
		// If we succeeded in opening the file then we don't need to do anything
		// else because someone else managed to create the file before we did. Just return.
		return {.New = false};
	}

	auto InternalCreateFile = [&] { return PayloadFile.Create(ShardedPath.c_str(), GENERIC_WRITE, FILE_SHARE_DELETE, CREATE_ALWAYS); };

	hRes = InternalCreateFile();

	if (hRes == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND))
	{
		// Ensure parent directories exist

		std::filesystem::create_directories(std::wstring_view(ShardedPath.c_str(), Shard2len));

		hRes = InternalCreateFile();
	}

	if (FAILED(hRes))
	{
		throw WindowsException(hRes, "Failed to open shard file");
	}

	size_t ChunkRemain = ChunkSize;
	auto   ChunkCursor = reinterpret_cast<const uint8_t*>(ChunkData);

	while (ChunkRemain != 0)
	{
		uint32_t ByteCount = uint32_t(std::min<size_t>(1024 * 1024ull, ChunkRemain));

		PayloadFile.Write(ChunkCursor, ByteCount);

		ChunkCursor += ByteCount;
		ChunkRemain -= ByteCount;
	}

	AtomicIncrement(m_Stats.PutCount);
	AtomicAdd(m_Stats.PutBytes, ChunkSize);

	return {.New = true};
}

IoBuffer
FileCasStrategy::FindChunk(const IoHash& ChunkHash)
{
	size_t							 Shard2len = 0;
	ExtendableWideStringBuilder<128> ShardedPath;
	ShardedPath.Append(m_Config.RootDirectory.c_str());
	ShardedPath.Append(std::filesystem::path::preferred_separator);
	MakeShardedPath(ShardedPath, ChunkHash, /* out */ Shard2len);

	RwLock::SharedLockScope _(LockForHash(ChunkHash));

	auto Chunk = IoBufferBuilder::MakeFromFile(ShardedPath.c_str());

	if (Chunk)
	{
		AtomicIncrement(m_Stats.GetCount);
		AtomicAdd(m_Stats.GetBytes, Chunk.Size());
	}

	return Chunk;
}

/**
 * Straightforward file-per-chunk CAS store implementation
 */
class FileCasImpl : public CasStore
{
public:
	FileCasImpl() : m_Strategy(m_Config, m_Stats) {}
	virtual ~FileCasImpl() = default;

	void PickDefaultDirectory()
	{
		if (m_Config.RootDirectory.empty())
		{
			// Pick sensible default

			WCHAR	myDocumentsDir[MAX_PATH];
			HRESULT hRes = SHGetFolderPathW(NULL,
											CSIDL_PERSONAL /*  My Documents */,
											NULL,
											SHGFP_TYPE_CURRENT,
											/* out */ myDocumentsDir);

			if (SUCCEEDED(hRes))
			{
				wcscat_s(myDocumentsDir, L"\\zen\\DefaultCAS");

				m_Config.RootDirectory = myDocumentsDir;
			}
		}
	}

	virtual void Initialize(const CasStoreConfiguration& InConfig) override
	{
		m_Config = InConfig;

		if (m_Config.RootDirectory.empty())
		{
			PickDefaultDirectory();
		}

		// Ensure root directory exists - create if it doesn't exist already

		std::filesystem::create_directories(m_Config.RootDirectory);

		std::filesystem::path filepath = m_Config.RootDirectory;
		filepath /= ".cas_root";

		CAtlFile marker;
		HRESULT	 hRes = marker.Create(filepath.c_str(), GENERIC_READ, 0, OPEN_EXISTING);

		if (FAILED(hRes))
		{
			ExtendableStringBuilder<128> manifest;
			manifest.Append("CAS_ROOT");
			hRes = marker.Create(filepath.c_str(), GENERIC_WRITE, 0, CREATE_ALWAYS);

			if (SUCCEEDED(hRes))
				marker.Write(manifest.c_str(), (DWORD)manifest.Size());
		}
	}

	virtual CasStore::InsertResult InsertChunk(const void* chunkData, size_t chunkSize, const IoHash& chunkHash) override
	{
		return m_Strategy.InsertChunk(chunkData, chunkSize, chunkHash);
	}
	virtual CasStore::InsertResult InsertChunk(IoBuffer Chunk, const IoHash& chunkHash) override
	{
		return m_Strategy.InsertChunk(Chunk, chunkHash);
	}
	virtual IoBuffer FindChunk(const IoHash& chunkHash) override { return m_Strategy.FindChunk(chunkHash); }

private:
	FileCasStrategy m_Strategy;
};

}  // namespace zen

