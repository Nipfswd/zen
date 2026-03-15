// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <zencore/iohash.h>

#include <span>

namespace zen {

class CasStore;
struct IoHash;

class CasScrubber
{
public:
	CasScrubber(CasStore& Store);
	~CasScrubber();

private:
	CasStore& m_CasStore;
};

}  // namespace zen

