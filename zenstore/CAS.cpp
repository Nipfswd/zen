// Copyright Noah Games, Inc. All Rights Reserved.

#include <zenstore/cas.h>

#include "compactcas.h"
#include "filecas.h"

#include <doctest/doctest.h>
#include <zencore/except.h>
#include <zencore/fmtutils.h>
#include <zencore/memory.h>
#include <zencore/string.h>
#include <zencore/thread.h>
#include <zencore/uid.h>

#include <spdlog/spdlog.h>

#include <gsl/gsl-lite.hpp>

#include <filesystem>
#include <functional>
#include <unordered_map>

struct IUnknown;  // Workaround for "combaseapi.h(229): error C2187: syntax error: 'identifier' was unexpected here" when using /permissive-
#include <atlfile.h>

//////////////////////////////////////////////////////////////////////////

namespace zen {

/**
 * Slightly less naive CAS store
 */
class CasImpl : public CasStore
{
public:
	CasImpl();
	virtual ~CasImpl();

	virtual void				   Initialize(const CasStoreConfiguration& InConfig) override;
	virtual CasStore::InsertResult InsertChunk(const void* ChunkData, size_t ChunkSize, const IoHash& ChunkHash) override;
	virtual CasStore::InsertResult InsertChunk(IoBuffer Chunk, const IoHash& ChunkHash) override;
	virtual IoBuffer			   FindChunk(const IoHash& ChunkHash) override;

private:
	void PickDefaultDirectory();

	CasContainerStrategy m_TinyStrategy;
	CasContainerStrategy m_SmallStrategy;
	FileCasStrategy		 m_LargeStrategy;
};

CasImpl::CasImpl() : m_TinyStrategy(m_Config, m_Stats), m_SmallStrategy(m_Config, m_Stats), m_LargeStrategy(m_Config, m_Stats)
{
}

CasImpl::~CasImpl()
{
}

void
CasImpl::Initialize(const CasStoreConfiguration& InConfig)
{
	m_Config = InConfig;

	spdlog::info("initializing CAS pool at {}", m_Config.RootDirectory);

	// Ensure root directory exists - create if it doesn't exist already

	std::filesystem::create_directories(m_Config.RootDirectory);

	// Open or create manifest

	bool IsNewStore = false;

	{
		std::filesystem::path ManifestPath = m_Config.RootDirectory;
		ManifestPath /= ".ucas_root";

		CAtlFile marker;
		HRESULT	 hRes = marker.Create(ManifestPath.c_str(), GENERIC_READ, 0, OPEN_EXISTING);

		if (FAILED(hRes))
		{
			IsNewStore = true;

			ExtendableStringBuilder<128> manifest;
			manifest.Append("#CAS_ROOT\n");	 // TODO: should write something meaningful here
			manifest.Append("ID=");
			zen::Oid id = zen::Oid::NewOid();
			id.ToString(manifest);

			hRes = marker.Create(ManifestPath.c_str(), GENERIC_WRITE, 0, CREATE_ALWAYS);

			if (SUCCEEDED(hRes))
				marker.Write(manifest.c_str(), (DWORD)manifest.Size());
		}
	}

	// Initialize payload storage

	m_TinyStrategy.Initialize("tobs", 16, IsNewStore);
	m_SmallStrategy.Initialize("sobs", 4096, IsNewStore);
}

CasStore::InsertResult
CasImpl::InsertChunk(const void* ChunkData, size_t ChunkSize, const IoHash& ChunkHash)
{
	if (ChunkSize < m_Config.TinyValueThreshold)
	{
		return m_TinyStrategy.InsertChunk(ChunkData, ChunkSize, ChunkHash);
	}
	else if (ChunkSize >= m_Config.HugeValueThreshold)
	{
		return m_LargeStrategy.InsertChunk(ChunkData, ChunkSize, ChunkHash);
	}
	else
	{
		return m_SmallStrategy.InsertChunk(ChunkData, ChunkSize, ChunkHash);
	}
}

CasStore::InsertResult
CasImpl::InsertChunk(IoBuffer Chunk, const IoHash& ChunkHash)
{
	const uint64_t ChunkSize = Chunk.Size();

	if (ChunkSize < m_Config.TinyValueThreshold)
	{
		return m_TinyStrategy.InsertChunk(Chunk, ChunkHash);
	}
	else if (Chunk.Size() >= m_Config.HugeValueThreshold)
	{
		return m_LargeStrategy.InsertChunk(Chunk, ChunkHash);
	}
	else
	{
		return m_SmallStrategy.InsertChunk(Chunk, ChunkHash);
	}
}

IoBuffer
CasImpl::FindChunk(const IoHash& ChunkHash)
{
	if (IoBuffer Found = m_SmallStrategy.FindChunk(ChunkHash))
	{
		return Found;
	}

	if (IoBuffer Found = m_TinyStrategy.FindChunk(ChunkHash))
	{
		return Found;
	}

	if (IoBuffer Found = m_LargeStrategy.FindChunk(ChunkHash))
	{
		return Found;
	}

	// Not found
	return IoBuffer{};
}

//////////////////////////////////////////////////////////////////////////

CasStore*
CreateCasStore()
{
	return new CasImpl();
	// return new FileCasImpl();
}

//////////////////////////////////////////////////////////////////////////
//
// Testing related code follows...
//

void
CAS_forcelink()
{
}

TEST_CASE("CasStore")
{
	zen::CasStoreConfiguration config;
	config.RootDirectory = "c:\\temp\\test";

	std::unique_ptr<zen::CasStore> store{CreateCasStore()};
	store->Initialize(config);
}

}  // namespace zen

