// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <zencore/httpserver.h>
#include <zencore/uid.h>
#include <zencore/xxhash.h>
#include <zenstore/cas.h>
#include <zenstore/caslog.h>

#include <spdlog/spdlog.h>
#include <tsl/robin_map.h>
#include <filesystem>
#include <map>
#include <string>

namespace zen {

class CbPackage;

struct OplogEntry
{
	uint32_t OpLsn;
	uint32_t OpCoreOffset;	// note: Multiple of alignment!
	uint32_t OpCoreSize;
	uint32_t OpCoreHash;  // Used as checksum
	XXH3_128 OpKeyHash;	  // XXH128_canonical_t

	inline Oid OpKeyAsOId() const
	{
		Oid Id;
		memcpy(Id.OidBits, &OpKeyHash, sizeof Id.OidBits);
		return Id;
	}
};

static_assert(IsPow2(sizeof(OplogEntry)));

/** Project Store
 */
class ProjectStore : public RefCounted
{
	struct OplogStorage;

public:
	ProjectStore(CasStore& Store, std::filesystem::path BasePath);
	~ProjectStore();

	struct Project;

	struct Oplog
	{
		Oplog(std::string_view Id, Project* Outer, CasStore& Store, std::filesystem::path BasePath);
		~Oplog();

		[[nodiscard]] static bool ExistsAt(std::filesystem::path BasePath);

		void IterateFileMap(std::function<void(const Oid&, const std::string_view&)>&& Fn);

		IoBuffer FindChunk(Oid ChunkId);

		inline static const uint32_t kInvalidOp = ~0u;

		/** Persist a new oplog entry
		 *
		 * Returns the oplog LSN assigned to the new entry, or kInvalidOp if the entry is rejected
		 */
		uint32_t AppendNewOplogEntry(CbPackage Op);

		enum UpdateType
		{
			kUpdateNewEntry,
			kUpdateReplay
		};

		/** Update tracking metadata for a new oplog entry
		 *
		 * This is used during replay (and gets called as part of new op append)
		 *
		 * Returns the oplog LSN assigned to the new entry, or kInvalidOp if the entry is rejected
		 */
		uint32_t RegisterOplogEntry(CbObject Core, const OplogEntry& OpEntry, UpdateType TypeOfUpdate);

		/** Scan oplog and register each entry, thus updating the in-memory tracking tables
		 */
		void ReplayLog();

		const std::string& OplogId() const { return m_OplogId; }

		const std::wstring& TempDir() const { return m_TempPath.native(); }

		spdlog::logger& Log() { return m_OuterProject->Log(); }

	private:
		struct OplogEntryAddress
		{
			uint64_t Offset;
			uint64_t Size;
		};

		template<class V>
		using OidMap = tsl::robin_map<Oid, V, Oid::Hasher>;

		Project*			  m_OuterProject = nullptr;
		RwLock				  m_OplogLock;
		CasStore&			  m_CasStore;
		std::filesystem::path m_BasePath;
		std::filesystem::path m_TempPath;

		OidMap<IoHash>					 m_ChunkMap;	   // output data chunk id -> CAS address
		OidMap<IoHash>					 m_MetaMap;		   // meta chunk id -> CAS address
		OidMap<std::string>				 m_FileMap;		   // file id -> client file
		OidMap<std::string>				 m_ServerFileMap;  // file id -> server file
		std::map<int, OplogEntryAddress> m_OpAddressMap;   // Index LSN -> op data in ops blob file
		OidMap<int>						 m_LatestOpMap;	   // op key -> latest op LSN for key

		RefPtr<OplogStorage> m_Storage;
		std::string			 m_OplogId;

		void AddFileMapping(Oid FileId, std::string_view Path);
		void AddServerFileMapping(Oid FileId, std::string_view Path);
		void AddChunkMapping(Oid ChunkId, IoHash Hash);
		void AddMetaMapping(Oid ChunkId, IoHash Hash);
	};

	struct Project
	{
		std::string			  Identifier;
		std::filesystem::path RootDir;
		std::string			  EngineRootDir;
		std::string			  ProjectRootDir;

		Oplog* NewOplog(std::string_view OplogId);
		Oplog* OpenOplog(std::string_view OplogId);
		void   DeleteOplog(std::string_view OplogId);
		void   IterateOplogs(std::function<void(const Oplog&)>&& Fn) const;

		Project(ProjectStore* PrjStore, CasStore& Store, std::filesystem::path BasePath);
		~Project();

		void					  Read();
		void					  Write();
		[[nodiscard]] static bool Exists(std::filesystem::path BasePath);

		spdlog::logger& Log();

	private:
		ProjectStore*				 m_ProjectStore;
		CasStore&					 m_CasStore;
		mutable RwLock				 m_ProjectLock;
		std::map<std::string, Oplog> m_Oplogs;
		std::filesystem::path		 m_OplogStoragePath;

		std::filesystem::path BasePathForOplog(std::string_view OplogId);
	};

	Oplog* OpenProjectOplog(std::string_view ProjectId, std::string_view OplogId);

	Project* OpenProject(std::string_view ProjectId);
	Project* NewProject(std::filesystem::path BasePath,
						std::string_view	  ProjectId,
						std::string_view	  RootDir,
						std::string_view	  EngineRootDir,
						std::string_view	  ProjectRootDir);
	void	 DeleteProject(std::string_view ProjectId);
	bool	 Exists(std::string_view ProjectId);

	spdlog::logger&				 Log() { return m_Log; }
	const std::filesystem::path& BasePath() const { return m_ProjectBasePath; }

private:
	spdlog::logger				   m_Log;
	CasStore&					   m_CasStore;
	std::filesystem::path		   m_ProjectBasePath;
	RwLock						   m_ProjectsLock;
	std::map<std::string, Project> m_Projects;

	std::filesystem::path BasePathForProject(std::string_view ProjectId);
};

//////////////////////////////////////////////////////////////////////////
//
//  {ns}		a root namespace, should be associated with the project which owns it
//  {target}	a variation of the project, typically a build target
//  {lsn}		oplog entry sequence number
//
//  /prj/{ns}
//  /prj/{ns}/oplog/{target}
//  /prj/{ns}/oplog/{target}/{lsn}
//
// oplog entry
//
// id: {id}
// key: {}
// meta: {}
// data: []
// refs:
//

class HttpProjectService : public HttpService
{
public:
	HttpProjectService(CasStore& Store, ProjectStore* Projects);
	~HttpProjectService();

	virtual const char* BaseUri() const override;
	virtual void		HandleRequest(HttpServerRequest& Request) override;

private:
	CasStore&		  m_CasStore;
	spdlog::logger	  m_Log;
	HttpRequestRouter m_Router;
	Ref<ProjectStore> m_ProjectStore;
};

/** Project store interface for local clients
 *
 * This provides the same functionality as the HTTP interface but with
 * some optimizations which are only possible for clients running on the
 * same host as the Zen Store instance
 *
 */

class LocalProjectService : public RefCounted
{
protected:
	LocalProjectService(CasStore& Store, ProjectStore* Projects);
	~LocalProjectService();

public:
	static inline Ref<LocalProjectService> New(CasStore& Store, ProjectStore* Projects) { return new LocalProjectService(Store, Projects); }

private:
	struct LocalProjectImpl;

	CasStore&						  m_CasStore;
	Ref<ProjectStore>				  m_ProjectStore;
	std::unique_ptr<LocalProjectImpl> m_Impl;
};

}  // namespace zen

