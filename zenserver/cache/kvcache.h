// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <zencore/httpserver.h>

#include "cachestore.h"
#include "upstream/jupiter.h"

namespace zen {

/**
 * Generic HTTP K/V cache - can be consumed via legacy DDC interfaces, with
 * no key format conventions. Values are blobs
 */

class HttpKvCacheService : public zen::HttpService
{
public:
	HttpKvCacheService();
	~HttpKvCacheService();

	virtual const char* BaseUri() const override;
	virtual void		HandleRequest(zen::HttpServerRequest& Request) override;

private:
	MemoryCacheStore		 m_cache;
	FileCacheStore			 m_cache_{"E:\\Local-DDC-Write", "E:\\Local-DDC" /* Read */};
	RefPtr<CloudCacheClient> m_Cloud;
	uint64_t				 m_InMemoryBlobSizeThreshold = 16384;
	uint64_t				 m_FileBlobSizeThreshold	 = 16 * 1024 * 1024;

	struct AccessTracker;

	std::unique_ptr<AccessTracker> m_AccessTracker;
};

}  // namespace zen

