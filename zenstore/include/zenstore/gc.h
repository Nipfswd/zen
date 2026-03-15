// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <zencore/iohash.h>

#include <span>

namespace zen {

class CasStore;
struct IoHash;

class CasGc
{
public:
	CasGc(CasStore& Store);
	~CasGc();

	void CollectGarbage();

	void OnNewReferences(std::span<IoHash> Hashes);

private:
	CasStore& m_CasStore;
};

}  // namespace zen

