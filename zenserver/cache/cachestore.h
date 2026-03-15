// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <zencore/IoBuffer.h>
#include <zencore/iohash.h>
#include <zencore/thread.h>
#include <zencore/uid.h>
#include <zenstore/cas.h>
#include <compare>
#include <filesystem>
#include <unordered_map>

namespace zen {

class WideStringBuilderBase;
class CasStore;

}  // namespace zen

struct CacheValue
{
	zen::IoBuffer Value;
};

/******************************************************************************

  /$$   /$$/$$    /$$        /$$$$$$                   /$$
 | $$  /$$| $$   | $$       /$$__  $$                 | $$
 | $$ /$$/| $$   | $$      | $$  \__/ /$$$$$$  /$$$$$$| $$$$$$$  /$$$$$$
 | $$$$$/ |  $$ / $$/      | $$      |____  $$/$$_____| $$__  $$/$$__  $$
 | $$  $$  \  $$ $$/       | $$       /$$$$$$| $$     | $$  \ $| $$$$$$$$
 | $$\  $$  \  $$$/        | $$    $$/$$__  $| $$     | $$  | $| $$_____/
 | $$ \  $$  \  $/         |  $$$$$$|  $$$$$$|  $$$$$$| $$  | $|  $$$$$$$
 |__/  \__/   \_/           \______/ \_______/\_______|__/  |__/\_______/

 Basic Key-Value cache. No restrictions on keys, and values are always opaque
 binary blobs.

******************************************************************************/

class CacheStore
{
public:
	virtual bool Get(std::string_view Key, CacheValue& OutValue)	= 0;
	virtual void Put(std::string_view Key, const CacheValue& Value) = 0;
};

/** File system based implementation

	Emulates the behaviour of UE4 with regards to file system structure,
	and also adds a file corruption trailer to remain compatible with
	the file-system based implementation (this should be made configurable)

  */
class FileCacheStore : public CacheStore
{
public:
	FileCacheStore(const char* RootDir, const char* ReadRootDir = nullptr);
	~FileCacheStore();

	virtual bool Get(std::string_view Key, CacheValue& OutValue) override;
	virtual void Put(std::string_view Key, const CacheValue& Value) override;

private:
	std::filesystem::path m_RootDir;
	std::filesystem::path m_ReadRootDir;
	bool				  m_IsOk			= true;
	bool				  m_ReadRootIsValid = false;
};

class MemoryCacheStore : public CacheStore
{
public:
	MemoryCacheStore();
	~MemoryCacheStore();

	virtual bool Get(std::string_view Key, CacheValue& OutValue) override;
	virtual void Put(std::string_view Key, const CacheValue& Value) override;

private:
	zen::RwLock									   m_Lock;
	std::unordered_map<std::string, zen::IoBuffer> m_CacheMap;
};

/******************************************************************************

  /$$$$$$$$                        /$$$$$$                   /$$
 |_____ $$                        /$$__  $$                 | $$
	  /$$/  /$$$$$$ /$$$$$$$     | $$  \__/ /$$$$$$  /$$$$$$| $$$$$$$  /$$$$$$
	 /$$/  /$$__  $| $$__  $$    | $$      |____  $$/$$_____| $$__  $$/$$__  $$
	/$$/  | $$$$$$$| $$  \ $$    | $$       /$$$$$$| $$     | $$  \ $| $$$$$$$$
   /$$/   | $$_____| $$  | $$    | $$    $$/$$__  $| $$     | $$  | $| $$_____/
  /$$$$$$$|  $$$$$$| $$  | $$    |  $$$$$$|  $$$$$$|  $$$$$$| $$  | $|  $$$$$$$
 |________/\_______|__/  |__/     \______/ \_______/\_______|__/  |__/\_______/

  Cache store for UE5. Restricts keys to "{bucket}/{hash}" pairs where the hash
  is 40 (hex) chars in size. Values may be opaque blobs or structured objects
  which can in turn contain references to other objects.

******************************************************************************/

class ZenCacheMemoryLayer
{
public:
	ZenCacheMemoryLayer();
	~ZenCacheMemoryLayer();

	bool Get(std::string_view Bucket, const zen::IoHash& HashKey, CacheValue& OutValue);
	void Put(std::string_view Bucket, const zen::IoHash& HashKey, const CacheValue& Value);

private:
	struct CacheBucket
	{
		zen::RwLock															m_bucketLock;
		std::unordered_map<zen::IoHash, zen::IoBuffer, zen::IoHash::Hasher> m_cacheMap;

		bool Get(const zen::IoHash& HashKey, CacheValue& OutValue);
		void Put(const zen::IoHash& HashKey, const CacheValue& Value);
	};

	zen::RwLock									 m_Lock;
	std::unordered_map<std::string, CacheBucket> m_Buckets;
};

class ZenCacheDiskLayer
{
public:
	ZenCacheDiskLayer(zen::CasStore& Cas, const std::filesystem::path& RootDir);
	~ZenCacheDiskLayer();

	bool Get(std::string_view Bucket, const zen::IoHash& HashKey, CacheValue& OutValue);
	void Put(std::string_view Bucket, const zen::IoHash& HashKey, const CacheValue& Value);

	void Flush();

private:
	/** A cache bucket manages a single directory containing
		metadata and data for that bucket
	  */
	struct CacheBucket;

	zen::CasStore&								 m_CasStore;
	std::filesystem::path						 m_RootDir;
	zen::RwLock									 m_Lock;
	std::unordered_map<std::string, CacheBucket> m_Buckets;	 // TODO: make this case insensitive
};

class ZenCacheStore
{
public:
	ZenCacheStore(zen::CasStore& Cas, const std::filesystem::path& RootDir);
	~ZenCacheStore();

	virtual bool Get(std::string_view Bucket, const zen::IoHash& HashKey, CacheValue& OutValue);
	virtual void Put(std::string_view Bucket, const zen::IoHash& HashKey, const CacheValue& Value);

private:
	std::filesystem::path m_RootDir;
	ZenCacheMemoryLayer	  m_MemLayer;
	ZenCacheDiskLayer	  m_DiskLayer;
};

/** Tracks cache entry access, stats and orchestrates cleanup activities
 */
class ZenCacheTracker
{
public:
	ZenCacheTracker(ZenCacheStore& CacheStore);
	~ZenCacheTracker();

	void TrackAccess(std::string_view Bucket, const zen::IoHash& HashKey);

private:
};

