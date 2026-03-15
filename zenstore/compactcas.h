// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <zencore/zencore.h>

#include <zencore/iobuffer.h>
#include <zencore/iohash.h>
#include <zencore/string.h>
#include <zencore/thread.h>
#include <zencore/uid.h>
#include <zencore/windows.h>
#include <zenstore/cas.h>
#include <zenstore/caslog.h>

#include <atlfile.h>
#include <functional>

namespace zen {

//////////////////////////////////////////////////////////////////////////

#pragma pack(push)
#pragma pack(1)

struct CasDiskLocation
{
	uint64_t Offset;
	uint32_t Size;	// TODO: Make this more like the IoStore index so we can store larger chunks (should be five bytes)
};

struct CasDiskIndexEntry
{
	IoHash			Key;
	CasDiskLocation Location;
};

#pragma pack(pop)

static_assert(sizeof(CasDiskIndexEntry) == 32);

struct CasContainerStrategy
{
	CasContainerStrategy(const CasStoreConfiguration& Config, CasStore::Stats& Stats) : m_Config(Config), m_Stats(Stats) {}
	CasStore::InsertResult InsertChunk(const void* chunkData, size_t chunkSize, const IoHash& chunkHash);
	CasStore::InsertResult InsertChunk(IoBuffer Chunk, const IoHash& chunkHash);
	IoBuffer			   FindChunk(const IoHash& chunkHash);
	void				   Initialize(const std::string_view ContainerBaseName, uint64_t Alignment, bool IsNewStore);

private:
	const CasStoreConfiguration&   m_Config;
	CasStore::Stats&			   m_Stats;
	uint64_t					   m_PayloadAlignment = 1 << 4;
	bool						   m_IsInitialized	  = false;
	CasBlobFile					   m_SmallObjectFile;
	CasBlobFile					   m_SmallObjectIndex;
	TCasLogFile<CasDiskIndexEntry> m_CasLog;

	RwLock														m_LocationMapLock;
	std::unordered_map<IoHash, CasDiskLocation, IoHash::Hasher> m_LocationMap;
	RwLock														m_InsertLock;  // used to serialize inserts
	std::atomic<uint64_t>										m_CurrentInsertOffset = 0;
	std::atomic<uint64_t>										m_CurrentIndexOffset  = 0;
};

}  // namespace zen

