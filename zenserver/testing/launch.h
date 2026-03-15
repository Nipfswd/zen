// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <zencore/httpserver.h>

#include <spdlog/spdlog.h>

namespace zen {

class CasStore;

/**
 * Process launcher for test executables
 */
class HttpLaunchService : public HttpService
{
public:
	HttpLaunchService(CasStore& Store);
	~HttpLaunchService();

	virtual const char* BaseUri() const override;
	virtual void		HandleRequest(HttpServerRequest& Request) override;

private:
	spdlog::logger	  m_Log;
	HttpRequestRouter m_Router;
	CasStore&		  m_CasStore;
};

}  // namespace zen

