// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <zencore/httpserver.h>

class HttpAdminService : public zen::HttpService
{
public:
	HttpAdminService()	= default;
	~HttpAdminService() = default;

	virtual const char* BaseUri() const override { return "/admin/"; }

	virtual void HandleRequest(zen::HttpServerRequest& Request) override { ZEN_UNUSED(Request); }

private:
};

