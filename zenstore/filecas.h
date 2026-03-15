// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <zencore/zencore.h>

#include <zencore/iobuffer.h>
#include <zencore/iohash.h>
#include <zencore/string.h>
#include <zencore/thread.h>
#include <zenstore/cas.h>

namespace zen {

struct FileCasStrategy
{
	FileCasStrategy(const CasStoreConfiguration& Config, CasStore::Stats& Stats) : m_Config(Config), m_Stats(Stats) {}
	CasStore::InsertResult InsertChunk(const void* chunkData, size_t chunkSize, const IoHash& chunkHash);
	CasStore::InsertResult InsertChunk(IoBuffer Chunk, const IoHash& chunkHash);
	IoBuffer			   FindChunk(const IoHash& chunkHash);

private:
	const CasStoreConfiguration& m_Config;
	CasStore::Stats&			 m_Stats;
	RwLock						 m_Lock;
	RwLock						 m_ShardLocks[256];	 // TODO: these should be spaced out so they don't share cache lines

	inline RwLock&				  LockForHash(const IoHash& Hash) { return m_ShardLocks[Hash.Hash[19]]; }
	static WideStringBuilderBase& MakeShardedPath(WideStringBuilderBase& ShardedPath, const IoHash& ChunkHash, size_t& OutShard2len);
};

}  // namespace zen

