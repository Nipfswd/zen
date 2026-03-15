// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include "kvcache.h"

#include <zencore/httpserver.h>
#include <zencore/memory.h>
#include <zencore/timer.h>
#include "cachestore.h"
#include "upstream/jupiter.h"

#include <rocksdb/db.h>
#include <spdlog/spdlog.h>

namespace zen {

namespace rocksdb = ROCKSDB_NAMESPACE;
using namespace fmt::literals;
using namespace std::literals;

//////////////////////////////////////////////////////////////////////////

struct HttpKvCacheService::AccessTracker
{
	AccessTracker();
	~AccessTracker();

	void TrackAccess(std::string_view Key);
	void Flush();

private:
	RwLock					m_Lock;
	ChunkingLinearAllocator m_AccessRecordAllocator{8192};
};

HttpKvCacheService::AccessTracker::AccessTracker()
{
}

HttpKvCacheService::AccessTracker::~AccessTracker()
{
	RwLock::ExclusiveLockScope _(m_Lock);
}

void
HttpKvCacheService::AccessTracker::Flush()
{
	RwLock::ExclusiveLockScope _(m_Lock);

	m_AccessRecordAllocator.Reset();
}

void
HttpKvCacheService::AccessTracker::TrackAccess(std::string_view Key)
{
	// Once it matters, this should use a thread-local means of updating this data,
	// like Concurrency::combinable or similar

	RwLock::ExclusiveLockScope _(m_Lock);

	const uint64_t KeySize = Key.size();
	void*		   Ptr	   = m_AccessRecordAllocator.Alloc(KeySize + 1);
	memcpy(Ptr, Key.data(), KeySize);
	reinterpret_cast<uint8_t*>(Ptr)[KeySize] = 0;
}

//////////////////////////////////////////////////////////////////////////

HttpKvCacheService::HttpKvCacheService()
{
	m_Cloud = new CloudCacheClient("https://jupiter.devtools.noahgames.com"sv,
								   "ue4.ddc"sv /* namespace */,
								   "https://noahgames.okta.com/oauth2/auso645ojjWVdRI3d0x7/v1/token"sv /* provider */,
								   "0oao91lrhqPiAlaGD0x7"sv /* client id */,
								   "-GBWjjenhCgOwhxL5yBKNJECVIoDPH0MK4RDuN7d"sv /* oauth secret */);

	m_AccessTracker = std::make_unique<AccessTracker>();
}

HttpKvCacheService::~HttpKvCacheService()
{
}

const char*
HttpKvCacheService::BaseUri() const
{
	return "/cache/";
}

void
HttpKvCacheService::HandleRequest(zen::HttpServerRequest& Request)
{
	using namespace std::literals;

	std::string_view Key = Request.RelativeUri();

	switch (auto Verb = Request.RequestVerb())
	{
		using enum zen::HttpVerb;

		case kHead:
		case kGet:
			{
				m_AccessTracker->TrackAccess(Key);

				CacheValue Value;
				bool	   Success = m_cache.Get(Key, Value);

				if (!Success)
				{
					// Success = m_cache_.Get(Key, Value);

					if (!Success)
					{
						CloudCacheSession Session(m_Cloud);

						zen::Stopwatch Timer;

						if (IoBuffer CloudValue = Session.Get("default", Key))
						{
							Success = true;

							spdlog::debug("upstream HIT after {:5} {:6}! {}",
										  zen::NiceTimeSpanMs(Timer.getElapsedTimeMs()),
										  NiceBytes(CloudValue.Size()),
										  Key);

							Value.Value = CloudValue;
						}
						else
						{
							spdlog::debug("upstream miss after {:5}! {}", zen::NiceTimeSpanMs(Timer.getElapsedTimeMs()), Key);
						}
					}

					if (Success && (Value.Value.Size() <= m_InMemoryBlobSizeThreshold))
					{
						m_cache.Put(Key, Value);
					}
				}

				if (!Success)
				{
					Request.WriteResponse(zen::HttpResponse::NotFound);
				}
				else
				{
					if (Verb == zen::HttpVerb::kHead)
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

					if (Value.Value.Size() <= m_InMemoryBlobSizeThreshold)
					{
						m_cache.Put(Key, Value);
					}

					// m_cache_.Put(Key, Value);

					CloudCacheSession Session(m_Cloud);

					zen::Stopwatch Timer;

					Session.Put("default", Key, Value.Value);

					spdlog::debug("upstream PUT took {:5} {:6}! {}",
								  zen::NiceTimeSpanMs(Timer.getElapsedTimeMs()),
								  NiceBytes(Value.Value.Size()),
								  Key);

					Request.WriteResponse(zen::HttpResponse::Created);
				}
				else
				{
					return;
				}
			}
			break;

		case kDelete:
			// should this do anything?
			return Request.WriteResponse(zen::HttpResponse::OK);

		case kPost:
			break;

		default:
			break;
	}
}

}  // namespace zen

