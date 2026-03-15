// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <memory>

#include <zencore/compactbinary.h>
#include <zencore/refcount.h>

namespace zen {

/**
 * Client for communication with local project service
 *
 * This is WIP and not yet functional!
 */

class LocalProjectClient : public RefCounted
{
public:
	LocalProjectClient(int BasePort = 0);
	~LocalProjectClient();

	CbObject MessageTransaction(CbObject Request);

private:
	struct ClientImpl;

	std::unique_ptr<ClientImpl> m_Impl;
};

}  // namespace zen

