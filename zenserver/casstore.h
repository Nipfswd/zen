// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <zencore/httpserver.h>
#include <zenstore/cas.h>

namespace zen {

/**
 * Simple CAS store HTTP endpoint
 *
 * Note that since this does not end up pinning any of the chunks it's only really useful for a small subset of use cases where you know a
 * chunk exists in the underlying CAS store. Thus it's mainly useful for internal use when communicating between Zen store instances
 *
 * Using this interface for adding CAS chunks makes little sense except for testing purposes as garbage collection may reap anything you add
 * before anything ever gets to access it
 */

class HttpCasService : public HttpService
{
public:
	explicit HttpCasService(CasStore& Store);
	~HttpCasService() = default;

	virtual const char* BaseUri() const override;
	virtual void		HandleRequest(zen::HttpServerRequest& Request) override;

private:
	CasStore&		  m_CasStore;
	HttpRequestRouter m_Router;
};

}  // namespace zen

