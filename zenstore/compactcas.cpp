// Copyright Noah Games, Inc. All Rights Reserved.

#include <zenstore/cas.h>

#include "CompactCas.h"

#include <zencore/except.h>
#include <zencore/memory.h>
#include <zencore/string.h>
#include <zencore/thread.h>
#include <zencore/uid.h>

#include <gsl/gsl-lite.hpp>

#include <functional>

struct IUnknown;  // Workaround for "combaseapi.h(229): error C2187: syntax error: 'identifier' was unexpected here" when using /permissive-
#include <atlfile.h>
#include <filesystem>

//////////////////////////////////////////////////////////////////////////

namespace zen {

void
CasContainerStrategy::Initialize(const std::string_view ContainerBaseName, uint64_t Alignment, bool IsNewStore)
{
	ZEN_ASSERT(IsPow2(Alignment));
	ZEN_ASSERT(!m_IsInitialized);

	m_PayloadAlignment = Alignment;
	std::string			  BaseName(ContainerBaseName);
	std::filesystem::path SobsPath = m_Config.RootDirectory / (BaseName + ".ucas");
	std::filesystem::path SidxPath = m_Config.RootDirectory / (BaseName + ".uidx");
	std::filesystem::path SlogPath = m_Config.RootDirectory / (BaseName + ".ulog");

	m_SmallObjectFile.Open(SobsPath, IsNewStore);
	m_SmallObjectIndex.Open(SidxPath, IsNewStore);
	m_CasLog.Open(SlogPath, IsNewStore);

	// TODO: should validate integrity of container files here

	uint64_t MaxFileOffset = 0;

	{
		// This is not technically necessary but may help future static analysis
		zen::RwLock::ExclusiveLockScope _(m_LocationMapLock);

		m_CasLog.Replay([&](const CasDiskIndexEntry& Record) {
			m_LocationMap[Record.Key] = Record.Location;

			MaxFileOffset = std::max<uint64_t>(MaxFileOffset, Record.Location.Offset + Record.Location.Size);
		});
	}

	m_CurrentInsertOffset = (MaxFileOffset + m_PayloadAlignment - 1) & ~(m_PayloadAlignment - 1);
	m_CurrentIndexOffset  = m_SmallObjectIndex.FileSize();
	m_IsInitialized		  = true;
}

CasStore::InsertResult
CasContainerStrategy::InsertChunk(const void* ChunkData, size_t ChunkSize, const IoHash& ChunkHash)
{
	{
		RwLock::SharedLockScope _(m_LocationMapLock);
		auto					KeyIt = m_LocationMap.find(ChunkHash);

		if (KeyIt != m_LocationMap.end())
		{
			return CasStore::InsertResult{.New = false};
		}
	}

	// New entry

	RwLock::ExclusiveLockScope _(m_InsertLock);

	const uint64_t InsertOffset = m_CurrentInsertOffset;
	m_SmallObjectFile.Write(ChunkData, ChunkSize, InsertOffset);

	m_CurrentInsertOffset = (m_CurrentInsertOffset + ChunkSize + m_PayloadAlignment - 1) & ~(m_PayloadAlignment - 1);

	RwLock::ExclusiveLockScope __(m_LocationMapLock);

	CasDiskLocation Location{.Offset = InsertOffset, .Size = /* TODO FIX */ uint32_t(ChunkSize)};

	m_LocationMap[ChunkHash] = Location;

	CasDiskIndexEntry IndexEntry{.Key = ChunkHash, .Location = Location};

	m_CasLog.Append(IndexEntry);

	return CasStore::InsertResult{.New = true};
}

CasStore::InsertResult
CasContainerStrategy::InsertChunk(IoBuffer Chunk, const IoHash& ChunkHash)
{
	return InsertChunk(Chunk.Data(), Chunk.Size(), ChunkHash);
}

IoBuffer
CasContainerStrategy::FindChunk(const IoHash& ChunkHash)
{
	RwLock::SharedLockScope _(m_LocationMapLock);
	auto					KeyIt = m_LocationMap.find(ChunkHash);

	if (KeyIt != m_LocationMap.end())
	{
		const CasDiskLocation& Location = KeyIt->second;
		return zen::IoBufferBuilder::MakeFromFileHandle(m_SmallObjectFile.Handle(), Location.Offset, Location.Size);
	}

	// Not found

	return IoBuffer();
}

}  // namespace zen

