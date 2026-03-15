// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <zencore/zencore.h>

#include <zencore/blake3.h>
#include <zencore/iobuffer.h>
#include <zencore/iohash.h>
#include <zencore/refcount.h>
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>

namespace zen {

struct CasStoreConfiguration
{
	// Root directory for CAS store -- if not specified a default folder will be assigned in 'Documents\zen'
	std::filesystem::path RootDirectory;

	// Threshold below which values are considered 'tiny' and managed using the 'tiny values' strategy
	uint64_t TinyValueThreshold = 1024;

	// Threshold above which values are considered 'tiny' and managed using the 'huge values' strategy
	uint64_t HugeValueThreshold = 1024 * 1024;
};

class CasStore
{
public:
	virtual ~CasStore() = default;

	struct Stats
	{
		uint64_t PutBytes = 0;
		uint64_t PutCount = 0;

		uint64_t GetBytes = 0;
		uint64_t GetCount = 0;
	};

	const CasStoreConfiguration& Config() { return m_Config; }
	const Stats&				 GetStats() const { return m_Stats; }

	struct InsertResult
	{
		bool New = false;
	};

	virtual void		 Initialize(const CasStoreConfiguration& Config)							   = 0;
	virtual InsertResult InsertChunk(const void* ChunkData, size_t ChunkSize, const IoHash& ChunkHash) = 0;
	virtual InsertResult InsertChunk(IoBuffer Data, const IoHash& ChunkHash)						   = 0;
	virtual IoBuffer	 FindChunk(const IoHash& ChunkHash)											   = 0;

protected:
	CasStoreConfiguration m_Config;
	Stats				  m_Stats;
};

ZENCORE_API CasStore* CreateCasStore();

void CAS_forcelink();

}  // namespace zen

