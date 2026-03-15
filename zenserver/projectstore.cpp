// Copyright Noah Games, Inc. All Rights Reserved.

#include "projectstore.h"

#include <zencore/compactbinarybuilder.h>
#include <zencore/compactbinarypackage.h>
#include <zencore/compactbinaryvalidation.h>
#include <zencore/filesystem.h>
#include <zencore/fmtutils.h>
#include <zencore/stream.h>
#include <zencore/string.h>
#include <zencore/timer.h>
#include <zencore/windows.h>
#include <zenstore/cas.h>
#include <zenstore/caslog.h>

#pragma comment(lib, "Rpcrt4.lib")	// RocksDB made me do this
#include <rocksdb/db.h>

#include <lmdb.h>
#include <ppl.h>
#include <spdlog/spdlog.h>
#include <xxh3.h>
#include <asio.hpp>
#include <future>
#include <latch>

namespace zen {

namespace rocksdb = ROCKSDB_NAMESPACE;
using namespace fmt::literals;

//////////////////////////////////////////////////////////////////////////

struct ProjectStore::OplogStorage : public RefCounted
{
	OplogStorage(ProjectStore::Oplog* OwnerOplog, std::filesystem::path BasePath) : m_OwnerOplog(OwnerOplog), m_OplogStoragePath(BasePath)
	{
	}

	~OplogStorage()
	{
		Log().info("closing oplog storage at {}", m_OplogStoragePath);
		Flush();

		if (m_LmdbEnv)
		{
			mdb_env_close(m_LmdbEnv);
			m_LmdbEnv = nullptr;
		}

		if (m_RocksDb)
		{
			// Column families must be torn down before database is closed
			for (const auto& Handle : m_RocksDbColumnHandles)
			{
				m_RocksDb->DestroyColumnFamilyHandle(Handle);
			}

			rocksdb::Status Status = m_RocksDb->Close();

			if (!Status.ok())
			{
				Log().warn("db close error reported for '{}' : '{}'", m_OplogStoragePath, Status.getState());
			}
		}
	}

	[[nodiscard]] bool		  Exists() { return Exists(m_OplogStoragePath); }
	[[nodiscard]] static bool Exists(std::filesystem::path BasePath)
	{
		return std::filesystem::exists(BasePath / "ops.zlog") && std::filesystem::exists(BasePath / "ops.zdb") &&
			   std::filesystem::exists(BasePath / "ops.zops");
	}

	static bool Delete(std::filesystem::path BasePath) { return DeleteDirectories(BasePath); }

	void Open(bool IsCreate)
	{
		Log().info("initializing oplog storage at '{}'", m_OplogStoragePath);

		if (IsCreate)
		{
			DeleteDirectories(m_OplogStoragePath);
			CreateDirectories(m_OplogStoragePath);
		}

		m_Oplog.Open(m_OplogStoragePath / "ops.zlog", IsCreate);
		m_Oplog.Initialize();

		m_OpBlobs.Open(m_OplogStoragePath / "ops.zops", IsCreate);

		ZEN_ASSERT(IsPow2(m_OpsAlign));
		ZEN_ASSERT(!(m_NextOpsOffset & (m_OpsAlign - 1)));

		{
			std::string LmdbPath = WideToUtf8((m_OplogStoragePath / "ops.zdb").native().c_str());

			int rc = mdb_env_create(&m_LmdbEnv);
			rc	   = mdb_env_set_mapsize(m_LmdbEnv, 8 * 1024 * 1024);
			rc	   = mdb_env_set_maxreaders(m_LmdbEnv, 256);
			rc	   = mdb_env_open(m_LmdbEnv, LmdbPath.c_str(), MDB_NOSUBDIR | MDB_WRITEMAP | MDB_NOMETASYNC | MDB_NOSYNC, 0666);
		}

		{
			std::string RocksdbPath = WideToUtf8((m_OplogStoragePath / "ops.rdb").native().c_str());

			Log().debug("opening rocksdb db at '{}'", RocksdbPath);

			rocksdb::DB*	   Db;
			rocksdb::DBOptions Options;
			Options.create_if_missing = true;

			std::vector<std::string> ExistingColumnFamilies;
			rocksdb::Status			 Status = rocksdb::DB::ListColumnFamilies(Options, RocksdbPath, &ExistingColumnFamilies);

			std::vector<rocksdb::ColumnFamilyDescriptor> ColumnDescriptors;

			if (Status.IsPathNotFound())
			{
				ColumnDescriptors.emplace_back(rocksdb::ColumnFamilyDescriptor{rocksdb::kDefaultColumnFamilyName, {}});
			}
			else if (Status.ok())
			{
				for (const std::string& Column : ExistingColumnFamilies)
				{
					rocksdb::ColumnFamilyDescriptor ColumnFamily;
					ColumnFamily.name = Column;
					ColumnDescriptors.push_back(ColumnFamily);
				}
			}
			else
			{
				throw std::exception("column family iteration failed for '{}': '{}'"_format(RocksdbPath, Status.getState()).c_str());
			}

			Status = rocksdb::DB::Open(Options, RocksdbPath, ColumnDescriptors, &m_RocksDbColumnHandles, &Db);

			if (!Status.ok())
			{
				throw std::exception("database open failed for '{}': '{}'"_format(RocksdbPath, Status.getState()).c_str());
			}

			m_RocksDb.reset(Db);
		}
	}

	void ReplayLog(std::function<void(CbObject, const OplogEntry&)>&& Handler)
	{
		// This could use memory mapping or do something clever but for now it just reads the file sequentially

		spdlog::info("replaying log for '{}'", m_OplogStoragePath);

		Stopwatch Timer;

		m_Oplog.Replay([&](const zen::OplogEntry& LogEntry) {
			IoBuffer OpBuffer(LogEntry.OpCoreSize);

			const uint64_t OpFileOffset = LogEntry.OpCoreOffset * m_OpsAlign;

			m_OpBlobs.Read((void*)OpBuffer.Data(), LogEntry.OpCoreSize, OpFileOffset);

			// Verify checksum, ignore op data if incorrect
			const auto OpCoreHash = uint32_t(XXH3_64bits(OpBuffer.Data(), OpBuffer.Size()) & 0xffffFFFF);

			if (OpCoreHash != LogEntry.OpCoreHash)
			{
				Log().warn("skipping oplog entry with bad checksum!");
				return;
			}

			CbObject Op(SharedBuffer::MakeView(OpBuffer.Data(), OpBuffer.Size()));

			m_NextOpsOffset =
				Max(m_NextOpsOffset.load(std::memory_order::memory_order_relaxed), RoundUp(OpFileOffset + LogEntry.OpCoreSize, m_OpsAlign));
			m_MaxLsn = Max(m_MaxLsn.load(std::memory_order::memory_order_relaxed), LogEntry.OpLsn);

			Handler(Op, LogEntry);
		});

		spdlog::info("Oplog replay completed in {} - Max LSN# {}, Next offset: {}",
					 NiceTimeSpanMs(Timer.getElapsedTimeMs()),
					 m_MaxLsn,
					 m_NextOpsOffset);
	}

	OplogEntry AppendOp(CbObject Op)
	{
		SharedBuffer   Buffer	  = Op.GetBuffer();
		const uint64_t WriteSize  = Buffer.GetSize();
		const auto	   OpCoreHash = uint32_t(XXH3_64bits(Buffer.GetData(), WriteSize) & 0xffffFFFF);

		XXH3_128Stream KeyHasher;
		Op["key"].WriteToStream([&](const void* Data, size_t Size) { KeyHasher.Append(Data, Size); });
		XXH3_128 KeyHash = KeyHasher.GetHash();

		RwLock::ExclusiveLockScope _(m_RwLock);
		const uint64_t			   WriteOffset = m_NextOpsOffset;
		const uint32_t			   OpLsn	   = ++m_MaxLsn;

		m_NextOpsOffset = RoundUp(WriteOffset + WriteSize, m_OpsAlign);

		ZEN_ASSERT(IsMultipleOf(WriteOffset, m_OpsAlign));

		OplogEntry Entry = {.OpLsn		  = OpLsn,
							.OpCoreOffset = gsl::narrow_cast<uint32_t>(WriteOffset / m_OpsAlign),
							.OpCoreSize	  = uint32_t(Buffer.GetSize()),
							.OpCoreHash	  = OpCoreHash,
							.OpKeyHash	  = KeyHash};

		m_Oplog.Append(Entry);

		m_OpBlobs.Write(Buffer.GetData(), WriteSize, WriteOffset);

		return Entry;
	}

	void Flush()
	{
		m_Oplog.Flush();
		m_OpBlobs.Flush();
	}

	spdlog::logger& Log() { return m_OwnerOplog->Log(); }

private:
	ProjectStore::Oplog*					  m_OwnerOplog;
	std::filesystem::path					  m_OplogStoragePath;
	RwLock									  m_RwLock;
	TCasLogFile<OplogEntry>					  m_Oplog;
	CasBlobFile								  m_OpBlobs;
	std::atomic<uint64_t>					  m_NextOpsOffset{0};
	uint64_t								  m_OpsAlign = 32;
	std::atomic<uint32_t>					  m_MaxLsn{0};
	MDB_env*								  m_LmdbEnv = nullptr;
	std::unique_ptr<rocksdb::DB>			  m_RocksDb;
	std::vector<rocksdb::ColumnFamilyHandle*> m_RocksDbColumnHandles;
};

//////////////////////////////////////////////////////////////////////////

ProjectStore::Oplog::Oplog(std::string_view Id, Project* Outer, CasStore& Store, std::filesystem::path BasePath)
: m_OuterProject(Outer)
, m_CasStore(Store)
, m_OplogId(Id)
, m_BasePath(BasePath)
{
	m_Storage			   = new OplogStorage(this, m_BasePath);
	const bool StoreExists = m_Storage->Exists();
	m_Storage->Open(/* IsCreate */ !StoreExists);

	m_TempPath = m_BasePath / "temp";

	zen::CleanDirectory(m_TempPath);
}

ProjectStore::Oplog::~Oplog() = default;

bool
ProjectStore::Oplog::ExistsAt(std::filesystem::path BasePath)
{
	return OplogStorage::Exists(BasePath);
}

void
ProjectStore::Oplog::ReplayLog()
{
	m_Storage->ReplayLog([&](CbObject Op, const OplogEntry& OpEntry) { RegisterOplogEntry(Op, OpEntry, kUpdateReplay); });
}

IoBuffer
ProjectStore::Oplog::FindChunk(Oid ChunkId)
{
	if (auto ChunkIt = m_ChunkMap.find(ChunkId); ChunkIt != m_ChunkMap.end())
	{
		return m_CasStore.FindChunk(ChunkIt->second);
	}

	if (auto FileIt = m_ServerFileMap.find(ChunkId); FileIt != m_ServerFileMap.end())
	{
		std::filesystem::path FilePath = m_OuterProject->RootDir / FileIt->second;

		return IoBufferBuilder::MakeFromFile(FilePath.native().c_str());
	}

	if (auto MetaIt = m_MetaMap.find(ChunkId); MetaIt != m_MetaMap.end())
	{
		return m_CasStore.FindChunk(MetaIt->second);
	}

	return {};
}

void
ProjectStore::Oplog::IterateFileMap(std::function<void(const Oid&, const std::string_view&)>&& Fn)
{
	for (const auto& Kv : m_FileMap)
	{
		Fn(Kv.first, Kv.second);
	}
}

void
ProjectStore::Oplog::AddFileMapping(Oid FileId, std::string_view Path)
{
	m_FileMap.emplace(FileId, Path);
}

void
ProjectStore::Oplog::AddServerFileMapping(Oid FileId, std::string_view Path)
{
	m_ServerFileMap.emplace(FileId, Path);
}

void
ProjectStore::Oplog::AddChunkMapping(Oid ChunkId, IoHash Hash)
{
	m_ChunkMap.emplace(ChunkId, Hash);
}

void
ProjectStore::Oplog::AddMetaMapping(Oid ChunkId, IoHash Hash)
{
	m_MetaMap.emplace(ChunkId, Hash);
}

uint32_t
ProjectStore::Oplog::RegisterOplogEntry(CbObject Core, const OplogEntry& OpEntry, UpdateType TypeOfUpdate)
{
	ZEN_UNUSED(TypeOfUpdate);

	using namespace std::literals;

	// Update chunk id maps

	if (Core["package"sv])
	{
		CbObjectView PkgObj		 = Core["package"sv].AsObjectView();
		Oid			 PackageId	 = PkgObj["id"sv].AsObjectId();
		IoHash		 PackageHash = PkgObj["data"sv].AsBinaryAttachment();

		AddChunkMapping(PackageId, PackageHash);

		Log().debug("package data {} -> {}", PackageId, PackageHash);
	}

	for (CbFieldView& Entry : Core["bulkdata"sv])
	{
		CbObjectView BulkObj = Entry.AsObjectView();

		Oid	   BulkDataId	= BulkObj["id"sv].AsObjectId();
		IoHash BulkDataHash = BulkObj["data"sv].AsBinaryAttachment();

		AddChunkMapping(BulkDataId, BulkDataHash);

		Log().debug("bulkdata {} -> {}", BulkDataId, BulkDataHash);
	}

	if (CbFieldView FilesArray = Core["files"sv])
	{
		int FileCount		= 0;
		int ServerFileCount = 0;

		std::atomic<bool> InvalidOp{false};

		Stopwatch Timer;

		std::future<void> f0 = std::async(std::launch::async, [&] {
			for (CbFieldView& Entry : FilesArray)
			{
				CbObjectView FileObj = Entry.AsObjectView();
				const Oid	 FileId	 = FileObj["id"sv].AsObjectId();

				if (auto PathField = FileObj["path"sv])
				{
					AddFileMapping(FileId, PathField.AsString());

					// Log().debug("file {} -> {}", FileId, PathString);

					++FileCount;
				}
				else
				{
					// Every file entry needs to specify a path
					InvalidOp = true;
					break;
				}

				if (InvalidOp.load(std::memory_order::relaxed))
				{
					break;
				}
			}
		});

		std::future<void> f1 = std::async(std::launch::async, [&] {
			CbArrayView ServerFilesArray = Core["serverfiles"sv].AsArrayView();

			for (CbFieldView& Entry : ServerFilesArray)
			{
				CbObjectView FileObj = Entry.AsObjectView();
				const Oid	 FileId	 = FileObj["id"sv].AsObjectId();

				if (auto PathField = FileObj["path"sv])
				{
					AddServerFileMapping(FileId, PathField.AsString());

					// m_log.debug("file {} -> {}", FileId, PathString);

					++ServerFileCount;
				}
				else
				{
					// Every file entry needs to specify a path
					InvalidOp = true;
					break;
				}

				if (InvalidOp.load(std::memory_order::relaxed))
				{
					break;
				}
			}
		});

		f0.wait();
		f1.wait();

		if (InvalidOp)
		{
			return kInvalidOp;
		}

		if (FileCount || ServerFileCount)
		{
			Log().debug("{} files registered, {} server files (took {})",
						FileCount,
						ServerFileCount,
						NiceTimeSpanMs(Timer.getElapsedTimeMs()));

			if (FileCount != ServerFileCount)
			{
				Log().warn("client/server file list mismatch: {} vs {}", FileCount, ServerFileCount);
			}
		}
	}

	for (CbFieldView& Entry : Core["meta"sv])
	{
		CbObjectView MetaObj	  = Entry.AsObjectView();
		const Oid	 MetaId		  = MetaObj["id"sv].AsObjectId();
		auto		 NameString	  = MetaObj["name"sv].AsString();
		IoHash		 MetaDataHash = MetaObj["data"sv].AsBinaryAttachment();

		AddMetaMapping(MetaId, MetaDataHash);

		Log().debug("meta data ({}) {} -> {}", NameString, MetaId, MetaDataHash);
	}

	m_OpAddressMap.emplace(OpEntry.OpLsn, OplogEntryAddress{.Offset = OpEntry.OpCoreOffset, .Size = OpEntry.OpCoreSize});
	m_LatestOpMap[OpEntry.OpKeyAsOId()] = OpEntry.OpLsn;

	return OpEntry.OpLsn;
}

uint32_t
ProjectStore::Oplog::AppendNewOplogEntry(CbPackage OpPackage)
{
	using namespace std::literals;

	const CbObject&	 Core	 = OpPackage.GetObject();
	const OplogEntry OpEntry = m_Storage->AppendOp(Core);

	// Persist attachments

	auto Attachments = OpPackage.GetAttachments();

	for (const auto& Attach : Attachments)
	{
		SharedBuffer BinaryView = Attach.AsBinaryView();
		m_CasStore.InsertChunk(BinaryView.GetData(), BinaryView.GetSize(), Attach.GetHash());
	}

	return RegisterOplogEntry(Core, OpEntry, kUpdateNewEntry);
}

//////////////////////////////////////////////////////////////////////////

ProjectStore::Project::Project(ProjectStore* PrjStore, CasStore& Store, std::filesystem::path BasePath)
: m_ProjectStore(PrjStore)
, m_CasStore(Store)
, m_OplogStoragePath(BasePath)
{
}

ProjectStore::Project::~Project()
{
}

bool
ProjectStore::Project::Exists(std::filesystem::path BasePath)
{
	return std::filesystem::exists(BasePath / "Project.zcb");
}

void
ProjectStore::Project::Read()
{
	std::filesystem::path ProjectStateFilePath = m_OplogStoragePath / "Project.zcb";

	spdlog::info("reading config for project '{}' from {}", Identifier, ProjectStateFilePath);

	CasBlobFile Blob;
	Blob.Open(ProjectStateFilePath, false);

	IoBuffer		Obj				= Blob.ReadAll();
	CbValidateError ValidationError = ValidateCompactBinary(MemoryView(Obj.Data(), Obj.Size()), CbValidateMode::All);

	if (ValidationError == CbValidateError::None)
	{
		CbObject Cfg = LoadCompactBinaryObject(Obj);

		Identifier	   = Cfg["id"].AsString();
		RootDir		   = Cfg["root"].AsString();
		ProjectRootDir = Cfg["project"].AsString();
		EngineRootDir  = Cfg["engine"].AsString();
	}
	else
	{
		spdlog::error("validation error {} hit for '{}'", int(ValidationError), ProjectStateFilePath);
	}
}

void
ProjectStore::Project::Write()
{
	MemoryOutStream Mem;
	BinaryWriter	Writer(Mem);

	CbObjectWriter Cfg;
	Cfg << "id" << Identifier;
	Cfg << "root" << WideToUtf8(RootDir.c_str());
	Cfg << "project" << ProjectRootDir;
	Cfg << "engine" << EngineRootDir;

	Cfg.Save(Writer);

	CreateDirectories(m_OplogStoragePath);

	std::filesystem::path ProjectStateFilePath = m_OplogStoragePath / "Project.zcb";

	spdlog::info("persisting config for project '{}' to {}", Identifier, ProjectStateFilePath);

	CasBlobFile Blob;
	Blob.Open(ProjectStateFilePath, true);
	Blob.Write(Mem.Data(), Mem.Size(), 0);
	Blob.Flush();
}

spdlog::logger&
ProjectStore::Project::Log()
{
	return m_ProjectStore->Log();
}

std::filesystem::path
ProjectStore::Project::BasePathForOplog(std::string_view OplogId)
{
	return m_OplogStoragePath / OplogId;
}

ProjectStore::Oplog*
ProjectStore::Project::NewOplog(std::string_view OplogId)
{
	RwLock::ExclusiveLockScope _(m_ProjectLock);

	std::filesystem::path OplogBasePath = BasePathForOplog(OplogId);

	try
	{
		Oplog& Log = m_Oplogs.try_emplace(std::string{OplogId}, OplogId, this, m_CasStore, OplogBasePath).first->second;

		return &Log;
	}
	catch (std::exception&)
	{
		// In case of failure we need to ensure there's no half constructed entry around
		//
		// (This is probably already ensured by the try_emplace implementation?)

		m_Oplogs.erase(std::string{OplogId});

		return nullptr;
	}
}

ProjectStore::Oplog*
ProjectStore::Project::OpenOplog(std::string_view OplogId)
{
	{
		RwLock::SharedLockScope _(m_ProjectLock);

		auto OplogIt = m_Oplogs.find(std::string(OplogId));

		if (OplogIt != m_Oplogs.end())
		{
			return &OplogIt->second;
		}
	}

	RwLock::ExclusiveLockScope _(m_ProjectLock);

	std::filesystem::path OplogBasePath = BasePathForOplog(OplogId);

	if (Oplog::ExistsAt(OplogBasePath))
	{
		// Do open of existing oplog

		try
		{
			Oplog& Log = m_Oplogs.try_emplace(std::string{OplogId}, OplogId, this, m_CasStore, OplogBasePath).first->second;

			Log.ReplayLog();

			return &Log;
		}
		catch (std::exception& ex)
		{
			spdlog::error("failed to open oplog '{}' @ '{}': {}", OplogId, OplogBasePath, ex.what());

			m_Oplogs.erase(std::string{OplogId});
		}
	}

	return nullptr;
}

void
ProjectStore::Project::DeleteOplog(std::string_view OplogId)
{
	bool Exists = false;

	{
		RwLock::ExclusiveLockScope _(m_ProjectLock);

		auto OplogIt = m_Oplogs.find(std::string(OplogId));

		if (OplogIt != m_Oplogs.end())
		{
			Exists = true;

			m_Oplogs.erase(OplogIt);
		}
	}

	// Actually erase

	std::filesystem::path OplogBasePath = BasePathForOplog(OplogId);

	OplogStorage::Delete(OplogBasePath);
}

void
ProjectStore::Project::IterateOplogs(std::function<void(const Oplog&)>&& Fn) const
{
	// TODO: should iterate over oplogs which are present on disk but not yet loaded

	RwLock::SharedLockScope _(m_ProjectLock);

	for (auto& Kv : m_Oplogs)
	{
		Fn(Kv.second);
	}
}

//////////////////////////////////////////////////////////////////////////

ProjectStore::ProjectStore(CasStore& Store, std::filesystem::path BasePath)
: m_Log("project", begin(spdlog::default_logger()->sinks()), end(spdlog::default_logger()->sinks()))
, m_ProjectBasePath(BasePath)
, m_CasStore(Store)
{
	m_Log.info("initializing project store at '{}'", BasePath);
	m_Log.set_level(spdlog::level::debug);
}

ProjectStore::~ProjectStore()
{
	m_Log.info("closing project store ('{}')", m_ProjectBasePath);
}

std::filesystem::path
ProjectStore::BasePathForProject(std::string_view ProjectId)
{
	return m_ProjectBasePath / ProjectId;
}

ProjectStore::Project*
ProjectStore::OpenProject(std::string_view ProjectId)
{
	{
		RwLock::SharedLockScope _(m_ProjectsLock);

		auto ProjIt = m_Projects.find(std::string{ProjectId});

		if (ProjIt != m_Projects.end())
		{
			return &(ProjIt->second);
		}
	}

	RwLock::ExclusiveLockScope _(m_ProjectsLock);

	std::filesystem::path ProjectBasePath = BasePathForProject(ProjectId);

	if (Project::Exists(ProjectBasePath))
	{
		try
		{
			Log().info("opening project {} @ {}", ProjectId, ProjectBasePath);

			ProjectStore::Project& Prj = m_Projects.try_emplace(std::string{ProjectId}, this, m_CasStore, ProjectBasePath).first->second;
			Prj.Read();
			return &Prj;
		}
		catch (std::exception& e)
		{
			Log().warn("failed to open {} @ {} ({})", ProjectId, ProjectBasePath, e.what());
			m_Projects.erase(std::string{ProjectId});
		}
	}

	return nullptr;
}

ProjectStore::Project*
ProjectStore::NewProject(std::filesystem::path BasePath,
						 std::string_view	   ProjectId,
						 std::string_view	   RootDir,
						 std::string_view	   EngineRootDir,
						 std::string_view	   ProjectRootDir)
{
	RwLock::ExclusiveLockScope _(m_ProjectsLock);

	ProjectStore::Project& Prj = m_Projects.try_emplace(std::string{ProjectId}, this, m_CasStore, BasePath).first->second;
	Prj.Identifier			   = ProjectId;
	Prj.RootDir				   = RootDir;
	Prj.EngineRootDir		   = EngineRootDir;
	Prj.ProjectRootDir		   = ProjectRootDir;
	Prj.Write();

	return &Prj;
}

void
ProjectStore::DeleteProject(std::string_view ProjectId)
{
	std::filesystem::path ProjectBasePath = BasePathForProject(ProjectId);

	Log().info("deleting project {} @ {}", ProjectId, ProjectBasePath);

	m_Projects.erase(std::string{ProjectId});

	DeleteDirectories(ProjectBasePath);
}

bool
ProjectStore::Exists(std::string_view ProjectId)
{
	return Project::Exists(BasePathForProject(ProjectId));
}

ProjectStore::Oplog*
ProjectStore::OpenProjectOplog(std::string_view ProjectId, std::string_view OplogId)
{
	if (Project* ProjectIt = OpenProject(ProjectId))
	{
		return ProjectIt->OpenOplog(OplogId);
	}

	return nullptr;
}

//////////////////////////////////////////////////////////////////////////

HttpProjectService::HttpProjectService(CasStore& Store, ProjectStore* Projects)
: m_CasStore(Store)
, m_Log("project", begin(spdlog::default_logger()->sinks()), end(spdlog::default_logger()->sinks()))
, m_ProjectStore(Projects)
{
	using namespace std::literals;

	m_Router.AddPattern("project", "([[:alnum:]_.]+)");
	m_Router.AddPattern("log", "([[:alnum:]_.]+)");
	m_Router.AddPattern("op", "([[:digit:]]+?)");
	m_Router.AddPattern("chunk", "([[:xdigit:]]{24})");

	m_Router.RegisterRoute(
		"{project}/oplog/{log}/batch",
		[this](HttpRouterRequest& Req) {
			HttpServerRequest& HttpReq = Req.ServerRequest();

			const auto& ProjectId = Req.GetCapture(1);
			const auto& OplogId	  = Req.GetCapture(2);

			m_Log.info("batch - {} / {}", ProjectId, OplogId);

			ProjectStore::Oplog* FoundLog = m_ProjectStore->OpenProjectOplog(ProjectId, OplogId);

			if (FoundLog == nullptr)
			{
				return HttpReq.WriteResponse(HttpResponse::NotFound);
			}

			ProjectStore::Oplog& Log = *FoundLog;

			// Parse Request

			IoBuffer	   Payload = HttpReq.ReadPayload();
			MemoryInStream MemIn(Payload.Data(), Payload.Size());
			BinaryReader   Reader(MemIn);

			struct RequestHeader
			{
				enum
				{
					kMagic = 0xAAAA'77AC
				};
				uint32_t Magic;
				uint32_t ChunkCount;
				uint32_t Reserved1;
				uint32_t Reserved2;
			};

			struct RequestChunkEntry
			{
				Oid		 ChunkId;
				uint32_t CorrelationId;
				uint64_t Offset;
				uint64_t RequestBytes;
			};

			if (Payload.Size() <= sizeof(RequestHeader))
			{
				HttpReq.WriteResponse(HttpResponse::BadRequest);
			}

			RequestHeader Hdr;
			Reader.Read(&Hdr, sizeof Hdr);

			if (Hdr.Magic != RequestHeader::kMagic)
			{
				HttpReq.WriteResponse(HttpResponse::BadRequest);
			}

			// Make Response

			MemoryOutStream MemOut;
			BinaryWriter	MemWriter(MemOut);

			struct ResponseHeader
			{
				uint32_t Magic = 0xbada'b00f;
				uint32_t ChunkCount;
				uint32_t Reserved1 = 0;
				uint32_t Reserved2 = 0;
			};

			struct ResponseChunkEntry
			{
				uint32_t CorrelationId;
				uint32_t Flags = 0;
				uint64_t ChunkSize;
			};

			return HttpReq.WriteResponse(HttpResponse::NotFound);
		},
		HttpVerb::kPost);

	m_Router.RegisterRoute(
		"{project}/oplog/{log}/files",
		[this](HttpRouterRequest& Req) {
			HttpServerRequest& HttpReq = Req.ServerRequest();

			// File manifest fetch, returns the client file list

			const auto& ProjectId = Req.GetCapture(1);
			const auto& OplogId	  = Req.GetCapture(2);

			ProjectStore::Oplog* FoundLog = m_ProjectStore->OpenProjectOplog(ProjectId, OplogId);

			if (FoundLog == nullptr)
			{
				return HttpReq.WriteResponse(HttpResponse::NotFound);
			}

			ProjectStore::Oplog& Log = *FoundLog;

			CbObjectWriter Response;
			Response.BeginArray("files");

			Log.IterateFileMap([&](const Oid& Id, const std::string_view& Path) {
				Response.BeginObject();
				Response << "id" << Id;
				Response << "path" << Path;
				Response.EndObject();
			});

			Response.EndArray();

			return HttpReq.WriteResponse(HttpResponse::OK, Response.Save());
		},
		HttpVerb::kGet);

	m_Router.RegisterRoute(
		"{project}/oplog/{log}/{chunk}/info",
		[this](HttpRouterRequest& Req) {
			HttpServerRequest& HttpReq = Req.ServerRequest();

			const auto& ProjectId = Req.GetCapture(1);
			const auto& OplogId	  = Req.GetCapture(2);
			const auto& ChunkId	  = Req.GetCapture(3);

			ProjectStore::Oplog* FoundLog = m_ProjectStore->OpenProjectOplog(ProjectId, OplogId);

			if (FoundLog == nullptr)
			{
				return HttpReq.WriteResponse(HttpResponse::NotFound);
			}

			ProjectStore::Oplog& Log = *FoundLog;

			Oid Obj = Oid::FromHexString(ChunkId);

			IoBuffer Value = Log.FindChunk(Obj);

			if (Value)
			{
				CbObjectWriter Response;
				Response << "size" << Value.Size();
				return HttpReq.WriteResponse(HttpResponse::OK, Response.Save());
			}

			return HttpReq.WriteResponse(HttpResponse::NotFound);
		},
		HttpVerb::kGet);

	m_Router.RegisterRoute(
		"{project}/oplog/{log}/{chunk}",
		[this](HttpRouterRequest& Req) {
			HttpServerRequest& HttpReq = Req.ServerRequest();

			const auto& ProjectId = Req.GetCapture(1);
			const auto& OplogId	  = Req.GetCapture(2);
			const auto& ChunkId	  = Req.GetCapture(3);

			bool	 IsOffset = false;
			uint64_t Offset	  = 0;
			uint64_t Size	  = ~(0ull);

			auto QueryParms = Req.ServerRequest().GetQueryParams();

			if (auto OffsetParm = QueryParms.GetValue("offset"); OffsetParm.empty() == false)
			{
				if (auto OffsetVal = ParseInt<uint64_t>(OffsetParm))
				{
					Offset	 = OffsetVal.value();
					IsOffset = true;
				}
				else
				{
					return HttpReq.WriteResponse(HttpResponse::BadRequest);
				}
			}

			if (auto SizeParm = QueryParms.GetValue("size"); SizeParm.empty() == false)
			{
				if (auto SizeVal = ParseInt<uint64_t>(SizeParm))
				{
					Size	 = SizeVal.value();
					IsOffset = true;
				}
				else
				{
					return HttpReq.WriteResponse(HttpResponse::BadRequest);
				}
			}

			m_Log.debug("chunk - {} / {} / {}", ProjectId, OplogId, ChunkId);

			ProjectStore::Oplog* FoundLog = m_ProjectStore->OpenProjectOplog(ProjectId, OplogId);

			if (FoundLog == nullptr)
			{
				return HttpReq.WriteResponse(HttpResponse::NotFound);
			}

			ProjectStore::Oplog& Log = *FoundLog;

			Oid Obj = Oid::FromHexString(ChunkId);

			IoBuffer Value = Log.FindChunk(Obj);

			switch (HttpVerb Verb = HttpReq.RequestVerb())
			{
				case HttpVerb::kHead:
				case HttpVerb::kGet:
					if (!Value)
					{
						return HttpReq.WriteResponse(HttpResponse::NotFound);
					}

					if (Verb == HttpVerb::kHead)
					{
						HttpReq.SetSuppressResponseBody();
					}

					if (IsOffset)
					{
						if (Offset > Value.Size())
						{
							Offset = Value.Size();
						}

						if ((Offset + Size) > Value.Size())
						{
							Size = Value.Size() - Offset;
						}

						// Send only a subset of data
						IoBuffer InnerValue(Value, Offset, Size);

						return HttpReq.WriteResponse(HttpResponse::OK, HttpContentType::kBinary, InnerValue);
					}

					return HttpReq.WriteResponse(HttpResponse::OK, HttpContentType::kBinary, Value);
			}
		},
		HttpVerb::kGet | HttpVerb::kHead);

	m_Router.RegisterRoute(
		"{project}/oplog/{log}/new",
		[this](HttpRouterRequest& Req) {
			HttpServerRequest& HttpReq = Req.ServerRequest();

			const auto& ProjectId = Req.GetCapture(1);
			const auto& OplogId	  = Req.GetCapture(2);

			ProjectStore::Oplog* FoundLog = m_ProjectStore->OpenProjectOplog(ProjectId, OplogId);

			if (FoundLog == nullptr)
			{
				return HttpReq.WriteResponse(HttpResponse::NotFound);
			}

			ProjectStore::Oplog& Log = *FoundLog;

			IoBuffer Payload = HttpReq.ReadPayload();

			CbPackage::AttachmentResolver Resolver = [&](const IoHash& Hash) -> SharedBuffer {
				std::filesystem::path AttachmentPath = Log.TempPath() / Hash.ToHexString();

				if (IoBuffer Data = IoBufferBuilder::MakeFromFile(AttachmentPath.native().c_str()))
				{
					return SharedBuffer::Clone(MemoryView(Data.Data(), Data.Size()));
				}
				else
				{
					return {};
				}
			};

			CbPackage Package;
			Package.Load(Payload, &UniqueBuffer::Alloc, &Resolver);

			CbObject Core = Package.GetObject();

			if (!Core["key"sv])
			{
				return HttpReq.WriteResponse(HttpResponse::BadRequest, HttpContentType::kText, "No oplog entry key specified");
			}

			// Write core to oplog

			const uint32_t OpLsn = Log.AppendNewOplogEntry(Package);

			if (OpLsn == ProjectStore::Oplog::kInvalidOp)
			{
				return HttpReq.WriteResponse(HttpResponse::BadRequest);
			}

			m_Log.info("new op #{:4} - {}/{} ({:>6}) {}", OpLsn, ProjectId, OplogId, NiceBytes(Payload.Size()), Core["key"sv].AsString());

			HttpReq.WriteResponse(HttpResponse::Created);
		},
		HttpVerb::kPost);

	m_Router.RegisterRoute(
		"{project}/oplog/{log}/{op}",
		[this](HttpRouterRequest& Req) {
			HttpServerRequest& HttpReq = Req.ServerRequest();

			// TODO: look up op and respond with the payload!

			HttpReq.WriteResponse(HttpResponse::Accepted, HttpContentType::kText, u8"yeee"sv);
		},
		HttpVerb::kGet);

	using namespace fmt::literals;

	m_Router.RegisterRoute(
		"{project}/oplog/{log}",
		[this](HttpRouterRequest& Req) {
			const auto& ProjectId = Req.GetCapture(1);
			const auto& OplogId	  = Req.GetCapture(2);

			ProjectStore::Project* ProjectIt = m_ProjectStore->OpenProject(ProjectId);

			if (!ProjectIt)
			{
				return Req.ServerRequest().WriteResponse(HttpResponse::NotFound,
														 HttpContentType::kText,
														 "project {} not found"_format(ProjectId));
			}

			ProjectStore::Project& Prj = *ProjectIt;

			switch (Req.ServerRequest().RequestVerb())
			{
				case HttpVerb::kGet:
					{
						ProjectStore::Oplog* OplogIt = Prj.OpenOplog(OplogId);

						if (!OplogIt)
						{
							return Req.ServerRequest().WriteResponse(HttpResponse::NotFound,
																	 HttpContentType::kText,
																	 "oplog {} not found in project {}"_format(OplogId, ProjectId));
						}

						ProjectStore::Oplog& Log = *OplogIt;

						CbObjectWriter Cb;
						Cb << "id"sv << Log.OplogId() << "project"sv << Prj.Identifier << "tempdir"sv << Log.TempDir();

						Req.ServerRequest().WriteResponse(HttpResponse::OK, Cb.Save());
					}
					break;

				case HttpVerb::kPost:
					{
						ProjectStore::Oplog* OplogIt = Prj.OpenOplog(OplogId);

						if (!OplogIt)
						{
							if (!Prj.NewOplog(OplogId))
							{
								// TODO: indicate why the operation failed!
								return Req.ServerRequest().WriteResponse(HttpResponse::InternalServerError);
							}

							m_Log.info("established oplog {} / {}", ProjectId, OplogId);

							return Req.ServerRequest().WriteResponse(HttpResponse::Created);
						}

						// I guess this should ultimately be used to execute RPCs but for now, it
						// does absolutely nothing

						return Req.ServerRequest().WriteResponse(HttpResponse::BadRequest);
					}
					break;

				case HttpVerb::kDelete:
					{
						spdlog::info("deleting oplog {}/{}", ProjectId, OplogId);

						ProjectIt->DeleteOplog(OplogId);

						return Req.ServerRequest().WriteResponse(HttpResponse::OK);
					}
					break;
			}
		},
		HttpVerb::kPost | HttpVerb::kGet | HttpVerb::kDelete);

	m_Router.RegisterRoute(
		"{project}",
		[this](HttpRouterRequest& Req) {
			const std::string ProjectId = Req.GetCapture(1);

			switch (Req.ServerRequest().RequestVerb())
			{
				case HttpVerb::kPost:
					{
						IoBuffer		 Payload	 = Req.ServerRequest().ReadPayload();
						CbObject		 Params		 = LoadCompactBinaryObject(Payload);
						std::string_view Id			 = Params["id"sv].AsString();
						std::string_view Root		 = Params["root"sv].AsString();
						std::string_view EngineRoot	 = Params["engine"sv].AsString();
						std::string_view ProjectRoot = Params["project"sv].AsString();

						const std::filesystem::path BasePath = m_ProjectStore->BasePath() / ProjectId;
						m_ProjectStore->NewProject(BasePath, ProjectId, Root, EngineRoot, ProjectRoot);

						m_Log.info("established project - {} (id: '{}', roots: '{}', '{}', '{}')",
								   ProjectId,
								   Id,
								   Root,
								   EngineRoot,
								   ProjectRoot);

						Req.ServerRequest().WriteResponse(HttpResponse::Created);
					}
					break;

				case HttpVerb::kGet:
					{
						ProjectStore::Project* ProjectIt = m_ProjectStore->OpenProject(ProjectId);

						if (!ProjectIt)
						{
							return Req.ServerRequest().WriteResponse(HttpResponse::NotFound,
																	 HttpContentType::kText,
																	 "project {} not found"_format(ProjectId));
						}

						const ProjectStore::Project& Prj = *ProjectIt;

						CbObjectWriter Response;
						Response << "id" << Prj.Identifier << "root" << WideToUtf8(Prj.RootDir.c_str());

						Response.BeginArray("oplogs"sv);
						Prj.IterateOplogs([&](const ProjectStore::Oplog& I) { Response << "id"sv << I.OplogId(); });
						Response.EndArray();  // oplogs

						Req.ServerRequest().WriteResponse(HttpResponse::OK, Response.Save());
					}
					break;

				case HttpVerb::kDelete:
					{
						ProjectStore::Project* ProjectIt = m_ProjectStore->OpenProject(ProjectId);

						if (!ProjectIt)
						{
							return Req.ServerRequest().WriteResponse(HttpResponse::NotFound,
																	 HttpContentType::kText,
																	 "project {} not found"_format(ProjectId));
						}

						m_ProjectStore->DeleteProject(ProjectId);
					}
					break;
			}
		},
		HttpVerb::kGet | HttpVerb::kPost | HttpVerb::kDelete);
}

HttpProjectService::~HttpProjectService()
{
}

const char*
HttpProjectService::BaseUri() const
{
	return "/prj/";
}

void
HttpProjectService::HandleRequest(HttpServerRequest& Request)
{
	if (m_Router.HandleRequest(Request) == false)
	{
		m_Log.warn("No route found for {0}", Request.RelativeUri());
	}
}

//////////////////////////////////////////////////////////////////////////

class SecurityAttributes
{
public:
	inline SECURITY_ATTRIBUTES* Attributes() { return &m_Attributes; }

protected:
	SECURITY_ATTRIBUTES m_Attributes{};
	SECURITY_DESCRIPTOR m_Sd{};
};

// Security attributes which allows any user access

class AnyUserSecurityAttributes : public SecurityAttributes
{
public:
	AnyUserSecurityAttributes()
	{
		m_Attributes.nLength		= sizeof m_Attributes;
		m_Attributes.bInheritHandle = false;  // Disable inheritance

		const BOOL success = InitializeSecurityDescriptor(&m_Sd, SECURITY_DESCRIPTOR_REVISION);

		if (success)
		{
			const BOOL bSetOk = SetSecurityDescriptorDacl(&m_Sd, TRUE, (PACL)NULL, FALSE);
			if (bSetOk)
			{
				m_Attributes.lpSecurityDescriptor = &m_Sd;
			}
		}
	}
};

//////////////////////////////////////////////////////////////////////////

struct LocalProjectService::LocalProjectImpl
{
	LocalProjectImpl() : m_WorkerThreadPool(ServiceThreadCount) {}
	~LocalProjectImpl() { Stop(); }

	void Start()
	{
		ZEN_ASSERT(!m_IsStarted);

		for (int i = 0; i < 32; ++i)
		{
			PipeConnection* NewPipe = new PipeConnection(this);
			m_ServicePipes.push_back(NewPipe);
			m_IoContext.post([NewPipe] { NewPipe->Accept(); });
		}

		for (int i = 0; i < ServiceThreadCount; ++i)
		{
			asio::post(m_WorkerThreadPool, [this] {
				try
				{
					m_IoContext.run();
				}
				catch (std::exception& ex)
				{
					spdlog::error("exception caught in pipe project service loop: {}", ex.what());
				}

				m_ShutdownLatch.count_down();
			});
		}

		m_IsStarted = true;
	}

	void Stop()
	{
		if (!m_IsStarted)
		{
			return;
		}

		for (PipeConnection* Pipe : m_ServicePipes)
		{
			Pipe->Disconnect();
		}

		m_IoContext.stop();
		m_ShutdownLatch.wait();

		for (PipeConnection* Pipe : m_ServicePipes)
		{
			delete Pipe;
		}

		m_ServicePipes.clear();
	}

private:
	asio::io_context& IoContext() { return m_IoContext; }
	auto			  PipeSecurityAttributes() { return m_AnyUserSecurityAttributes.Attributes(); }
	static const int  ServiceThreadCount = 4;

	std::latch		  m_ShutdownLatch{ServiceThreadCount};
	asio::thread_pool m_WorkerThreadPool;
	asio::io_context  m_IoContext;

	class PipeConnection
	{
		enum PipeState
		{
			kUninitialized,
			kConnecting,
			kReading,
			kWriting,
			kDisconnected,
			kInvalid
		};

		LocalProjectImpl*			 m_Outer;
		asio::windows::stream_handle m_PipeHandle;
		std::atomic<PipeState>		 m_PipeState{kUninitialized};

	public:
		PipeConnection(LocalProjectImpl* Outer) : m_Outer(Outer), m_PipeHandle{m_Outer->IoContext()} {}
		~PipeConnection() {}

		void Disconnect()
		{
			m_PipeState = kDisconnected;
			DisconnectNamedPipe(m_PipeHandle.native_handle());
		}

		void Accept()
		{
			StringBuilder<64> PipeName;
			PipeName << "\\\\.\\pipe\\zenprj";	// TODO: this should use an instance-specific identifier!

			HANDLE hPipe = CreateNamedPipeA(PipeName.c_str(),
											PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
											PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
											PIPE_UNLIMITED_INSTANCES,		   // Max instance count
											65536,							   // Output buffer size
											65536,							   // Input buffer size
											10'000,							   // Default timeout (ms)
											m_Outer->PipeSecurityAttributes()  // Security attributes
			);

			if (hPipe == INVALID_HANDLE_VALUE)
			{
				spdlog::warn("failed while creating named pipe {}", PipeName.c_str());

				// TODO: error - how to best handle?
			}

			m_PipeHandle.assign(hPipe);	 // This now owns the handle and will close it

			m_PipeState = kConnecting;

			asio::windows::overlapped_ptr OverlappedPtr(
				m_PipeHandle.get_executor(),
				std::bind(&PipeConnection::OnClientConnect, this, std::placeholders::_1, std::placeholders::_2));

			OVERLAPPED* Overlapped = OverlappedPtr.get();
			BOOL		Ok		   = ConnectNamedPipe(hPipe, Overlapped);
			DWORD		LastError  = GetLastError();

			if (!Ok && LastError != ERROR_IO_PENDING)
			{
				m_PipeState = kInvalid;

				// The operation completed immediately, so a completion notification needs
				// to be posted. When complete() is called, ownership of the OVERLAPPED-
				// derived object passes to the io_service.
				std::error_code Ec(LastError, asio::error::get_system_category());
				OverlappedPtr.complete(Ec, 0);
			}
			else
			{
				// The operation was successfully initiated, so ownership of the
				// OVERLAPPED-derived object has now passed to the io_service.
				OverlappedPtr.release();
			}
		}

	private:
		void OnClientConnect(const std::error_code& Ec, size_t BytesTransferred)
		{
			ZEN_UNUSED(BytesTransferred);

			if (Ec)
			{
				if (m_PipeState == kDisconnected)
				{
					return;
				}

				spdlog::warn("pipe connection error: {}", Ec.message());

				// TODO: should disconnect and issue a new connect
				return;
			}

			spdlog::debug("pipe connection established");

			IssueRead();
		}

		void IssueRead()
		{
			m_PipeState = kReading;

			m_PipeHandle.async_read_some(asio::mutable_buffer(m_MsgBuffer, sizeof m_MsgBuffer),
										 std::bind(&PipeConnection::OnClientRead, this, std::placeholders::_1, std::placeholders::_2));
		}

		void OnClientRead(const std::error_code& Ec, size_t Bytes)
		{
			if (Ec)
			{
				if (m_PipeState == kDisconnected)
				{
					return;
				}

				spdlog::warn("pipe read error: {}", Ec.message());

				// TODO: should disconnect and issue a new connect
				return;
			}

			spdlog::debug("received message: {} bytes", Bytes);

			// TODO: Actually process request

			m_PipeState = kWriting;

			asio::async_write(m_PipeHandle,
							  asio::buffer(m_MsgBuffer, Bytes),
							  std::bind(&PipeConnection::OnWriteCompletion, this, std::placeholders::_1, std::placeholders::_2));
		}

		void OnWriteCompletion(const std::error_code& Ec, size_t Bytes)
		{
			ZEN_UNUSED(Bytes);

			if (Ec)
			{
				if (m_PipeState == kDisconnected)
				{
					return;
				}

				spdlog::warn("pipe write error: {}", Ec.message());

				// TODO: should disconnect and issue a new connect
				return;
			}

			// Go back to reading
			IssueRead();
		}

		uint8_t m_MsgBuffer[16384];
	};

	AnyUserSecurityAttributes	 m_AnyUserSecurityAttributes;
	std::vector<PipeConnection*> m_ServicePipes;
	bool						 m_IsStarted = false;
};

LocalProjectService::LocalProjectService(CasStore& Store, ProjectStore* Projects) : m_CasStore(Store), m_ProjectStore(Projects)
{
	m_Impl = std::make_unique<LocalProjectImpl>();
	m_Impl->Start();
}

LocalProjectService::~LocalProjectService()
{
	m_Impl->Stop();
}

//////////////////////////////////////////////////////////////////////////

}  // namespace zen