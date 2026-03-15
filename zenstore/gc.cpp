// Copyright Noah Games, Inc. All Rights Reserved.

#include <zenstore/gc.h>

namespace zen {

CasGc::CasGc(CasStore& Store) : m_CasStore(Store)
{
}

CasGc::~CasGc()
{
}

void
CasGc::CollectGarbage()
{
}

void
CasGc::OnNewReferences(std::span<IoHash> Hashes)
{
	ZEN_UNUSED(Hashes);
}

}  // namespace zen

