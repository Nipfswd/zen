// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <zencore/httpserver.h>
#include <zencore/iobuffer.h>

//////////////////////////////////////////////////////////////////////////

class HttpTestService : public zen::HttpService
{
	uint32_t LogPoint = 0;

public:
	HttpTestService() {}
	~HttpTestService() = default;

	virtual const char* BaseUri() const override { return "/test/"; }

	virtual void HandleRequest(zen::HttpServerRequest& Request) override
	{
		using namespace std::literals;

		auto Uri = Request.RelativeUri();

		if (Uri == "hello"sv)
		{
			Request.WriteResponse(zen::HttpResponse::OK, zen::HttpContentType::kText, u8"hello world!"sv);

			// OutputLogMessageInternal(&LogPoint, 0, 0);
		}
		else if (Uri == "1K"sv)
		{
			Request.WriteResponse(zen::HttpResponse::OK, zen::HttpContentType::kBinary, m_1k);
		}
		else if (Uri == "1M"sv)
		{
			Request.WriteResponse(zen::HttpResponse::OK, zen::HttpContentType::kBinary, m_1m);
		}
		else if (Uri == "1M_1k"sv)
		{
			std::vector<zen::IoBuffer> Buffers;
			Buffers.reserve(1024);

			for (int i = 0; i < 1024; ++i)
			{
				Buffers.push_back(m_1k);
			}

			Request.WriteResponse(zen::HttpResponse::OK, zen::HttpContentType::kBinary, Buffers);
		}
		else if (Uri == "1G"sv)
		{
			std::vector<zen::IoBuffer> Buffers;
			Buffers.reserve(1024);

			for (int i = 0; i < 1024; ++i)
			{
				Buffers.push_back(m_1m);
			}

			Request.WriteResponse(zen::HttpResponse::OK, zen::HttpContentType::kBinary, Buffers);
		}
		else if (Uri == "1G_1k"sv)
		{
			std::vector<zen::IoBuffer> Buffers;
			Buffers.reserve(1024 * 1024);

			for (int i = 0; i < 1024 * 1024; ++i)
			{
				Buffers.push_back(m_1k);
			}

			Request.WriteResponse(zen::HttpResponse::OK, zen::HttpContentType::kBinary, Buffers);
		}
	}

private:
	zen::IoBuffer m_1m{1024 * 1024};
	zen::IoBuffer m_1k{m_1m, 0u, 1024};
};

class HttpHealthService : public zen::HttpService
{
public:
	HttpHealthService()	 = default;
	~HttpHealthService() = default;

	virtual const char* BaseUri() const override { return "/health/"; }

	virtual void HandleRequest(zen::HttpServerRequest& Request) override
	{
		using namespace std::literals;

		switch (Request.RequestVerb())
		{
			case zen::HttpVerb::kGet:
				return Request.WriteResponse(zen::HttpResponse::OK, zen::HttpContentType::kText, u8"OK!"sv);
		}
	}

private:
};

