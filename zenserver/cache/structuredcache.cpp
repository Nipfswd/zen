// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <zencore/fmtutils.h>
#include <zencore/httpserver.h>

#include "cachestore.h"
#include "structuredcache.h"
#include "upstream/jupiter.h"

#include <spdlog/spdlog.h>
#include <filesystem>

namespace zen {

HttpStructuredCacheService::HttpStructuredCacheService(std::filesystem::path RootPath, zen::CasStore& InStore)
: m_CasStore(InStore)
, m_CacheStore(InStore, RootPath)
{
	spdlog::info("initializing structured cache at '{}'", RootPath);
}

HttpStructuredCacheService::~HttpStructuredCacheService()
{
	spdlog::info("closing structured cache");
}

const char*
HttpStructuredCacheService::BaseUri() const
{
	return "/z$/";
}

void
HttpStructuredCacheService::HandleRequest(zen::HttpServerRequest& Request)
{
	CacheRef Ref;

	if (!ValidateUri(Request, /* out */ Ref))
	{
		return Request.WriteResponse(zen::HttpResponse::BadRequest);  // invalid URL
	}

	switch (auto Verb = Request.RequestVerb())
	{
		using enum zen::HttpVerb;

		case kHead:
		case kGet:
			{
				CacheValue Value;
				bool	   Success = m_CacheStore.Get(Ref.BucketSegment, Ref.HashKey, /* out */ Value);

				if (!Success)
				{
					Request.WriteResponse(zen::HttpResponse::NotFound);
				}
				else
				{
					if (Verb == kHead)
					{
						Request.SetSuppressResponseBody();
						Request.WriteResponse(zen::HttpResponse::OK, zen::HttpContentType::kBinary, Value.Value);
					}
					else
					{
						Request.WriteResponse(zen::HttpResponse::OK, zen::HttpContentType::kBinary, Value.Value);
					}
				}
			}
			break;

		case kPut:
			{
				if (zen::IoBuffer Body = Request.ReadPayload())
				{
					CacheValue Value;
					Value.Value = Body;

					m_CacheStore.Put(Ref.BucketSegment, Ref.HashKey, Value);

					Request.WriteResponse(zen::HttpResponse::Created);
				}
				else
				{
					return;
				}
			}
			break;

		case kPost:
			break;

		default:
			break;
	}
}

[[nodiscard]] bool
HttpStructuredCacheService::ValidateUri(zen::HttpServerRequest& Request, CacheRef& OutRef)
{
	std::string_view			Key				  = Request.RelativeUri();
	std::string_view::size_type BucketSplitOffset = Key.find_last_of('/');

	if (BucketSplitOffset == std::string_view::npos)
	{
		return false;
	}

	OutRef.BucketSegment		 = Key.substr(0, BucketSplitOffset);
	std::string_view HashSegment = Key.substr(BucketSplitOffset + 1);

	if (HashSegment.size() != (2 * sizeof OutRef.HashKey.Hash))
	{
		return false;
	}

	bool IsOk = zen::ParseHexBytes(HashSegment.data(), HashSegment.size(), OutRef.HashKey.Hash);

	if (!IsOk)
	{
		return false;
	}

	return true;
}

}  // namespace zen

