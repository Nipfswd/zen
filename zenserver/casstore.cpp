// Copyright Noah Games, Inc. All Rights Reserved.

#include "casstore.h"

#include <zencore/streamutil.h>

#include <spdlog/spdlog.h>
#include <gsl/gsl-lite.hpp>

namespace zen {

HttpCasService::HttpCasService(CasStore& Store) : m_CasStore(Store)
{
	m_Router.AddPattern("cas", "([0-9A-Fa-f]{40})");

	m_Router.RegisterRoute(
		"batch",
		[this](HttpRouterRequest& Req) {
			HttpServerRequest& ServerRequest = Req.ServerRequest();

			IoBuffer Payload	= ServerRequest.ReadPayload();
			uint64_t EntryCount = Payload.Size() / sizeof(IoHash);

			if ((EntryCount * sizeof(IoHash)) != Payload.Size())
			{
				return ServerRequest.WriteResponse(HttpResponse::BadRequest);
			}

			const IoHash*		  Hashes = reinterpret_cast<const IoHash*>(Payload.Data());
			std::vector<IoBuffer> Values;

			MemoryOutStream HeaderStream;
			BinaryWriter	HeaderWriter(HeaderStream);

			Values.emplace_back();	// Placeholder for header

			// Build response header
			HeaderWriter << uint32_t(0x12340000) << uint32_t(0);

			for (uint64_t i = 0; i < EntryCount; ++i)
			{
				IoHash	 ChunkHash = Hashes[i];
				IoBuffer Value	   = m_CasStore.FindChunk(ChunkHash);

				if (Value)
				{
					Values.emplace_back(std::move(Value));
					HeaderWriter << ChunkHash << uint64_t(Value.Size());
				}
			}

			// Make real header

			const_cast<uint32_t*>(reinterpret_cast<const uint32_t*>(HeaderStream.Data()))[1] = uint32_t(Values.size() - 1);

			Values[0] = IoBufferBuilder::MakeCloneFromMemory(HeaderStream.Data(), HeaderStream.Size());

			ServerRequest.WriteResponse(HttpResponse::OK, HttpContentType::kBinary, Values);
		},
		HttpVerb::kPost);

	m_Router.RegisterRoute(
		"{cas}",
		[this](HttpRouterRequest& Req) {
			IoHash Hash = IoHash::FromHexString(Req.GetCapture(1));
			spdlog::debug("CAS request for {}", Hash);

			HttpServerRequest& ServerRequest = Req.ServerRequest();

			switch (ServerRequest.RequestVerb())
			{
				case HttpVerb::kGet:
				case HttpVerb::kHead:
					{
						if (IoBuffer Value = m_CasStore.FindChunk(Hash))
						{
							return ServerRequest.WriteResponse(HttpResponse::OK, HttpContentType::kBinary, Value);
						}

						return ServerRequest.WriteResponse(HttpResponse::NotFound);
					}
					break;

				case HttpVerb::kPut:
					{
						IoBuffer Payload	 = ServerRequest.ReadPayload();
						IoHash	 PayloadHash = IoHash::HashMemory(Payload.Data(), Payload.Size());

						// URI hash must match content hash
						if (PayloadHash != Hash)
						{
							return ServerRequest.WriteResponse(HttpResponse::BadRequest);
						}

						m_CasStore.InsertChunk(Payload.Data(), Payload.Size(), PayloadHash);

						return ServerRequest.WriteResponse(HttpResponse::OK);
					}
					break;
			}
		},
		HttpVerb::kGet | HttpVerb::kPut | HttpVerb::kHead);
}

const char*
HttpCasService::BaseUri() const
{
	return "/cas/";
}

void
HttpCasService::HandleRequest(zen::HttpServerRequest& Request)
{
	if (Request.RelativeUri().empty())
	{
		// Root URI request

		switch (Request.RequestVerb())
		{
			case HttpVerb::kPut:
			case HttpVerb::kPost:
				{
					IoBuffer Payload	 = Request.ReadPayload();
					IoHash	 PayloadHash = IoHash::HashMemory(Payload.Data(), Payload.Size());

					spdlog::debug("CAS POST request for {} ({} bytes)", PayloadHash, Payload.Size());

					auto InsertResult = m_CasStore.InsertChunk(Payload.Data(), Payload.Size(), PayloadHash);

					if (InsertResult.New)
					{
						return Request.WriteResponse(HttpResponse::Created);
					}
					else
					{
						return Request.WriteResponse(HttpResponse::OK);
					}
				}
				break;

			case HttpVerb::kGet:
			case HttpVerb::kHead:
				break;

			default:
				break;
		}
	}
	else
	{
		m_Router.HandleRequest(Request);
	}
}

}  // namespace zen

