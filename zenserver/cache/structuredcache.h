// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <zencore/httpserver.h>

#include "cachestore.h"
#include "upstream/jupiter.h"

namespace zen {

/**
 * New-style cache service. Imposes constraints on keys, supports blobs and
 * structured values
 */

class HttpStructuredCacheService : public zen::HttpService
{
public:
	HttpStructuredCacheService(std::filesystem::path RootPath, zen::CasStore& InStore);
	~HttpStructuredCacheService();

	virtual const char* BaseUri() const override;

	virtual void HandleRequest(zen::HttpServerRequest& Request) override;

private:
	struct CacheRef
	{
		std::string BucketSegment;
		IoHash		HashKey;
	};

	[[nodiscard]] bool ValidateUri(zen::HttpServerRequest& Request, CacheRef& OutRef);

	zen::CasStore& m_CasStore;
	ZenCacheStore  m_CacheStore;
};

}  // namespace zen

