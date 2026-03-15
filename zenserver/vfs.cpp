// Copyright Noah Games, Inc. All Rights Reserved.

#include "vfs.h"

#include <zencore/except.h>
#include <zencore/filesystem.h>
#include <zencore/snapshot_manifest.h>
#include <zencore/stream.h>
#include <zencore/windows.h>

#include <map>

#include <atlfile.h>
#include <projectedfslib.h>
#include <spdlog/spdlog.h>

#pragma comment(lib, "projectedfslib.lib")

namespace zen {

//////////////////////////////////////////////////////////////////////////

struct ProjFsCliOptions
{
	bool		IsDebug = false;
	bool		IsClean = false;
	std::string CasSpec;
	std::string ManifestSpec;
	std::string MountPoint;
};

struct GuidHasher
{
	size_t operator()(const GUID& Guid) const
	{
		static_assert(sizeof(GUID) == (sizeof(size_t) * 2));

		const size_t* Ptr = reinterpret_cast<const size_t*>(&Guid);

		return Ptr[0] ^ Ptr[1];
	}
};

class ProjfsNamespace
{
public:
	HRESULT Initialize(const char* SnapshotSpec, const char* CasSpec)
	{
		std::filesystem::path ManifestSpec = zen::ManifestSpecToPath(SnapshotSpec);

		CAtlFile ManifestFile;
		HRESULT	 hRes = ManifestFile.Create(ManifestSpec.c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING);
		if (FAILED(hRes))
		{
			spdlog::error("MANIFEST NOT FOUND!");  // TODO: add context

			return hRes;
		}

		ULONGLONG FileLength = 0;
		ManifestFile.GetSize(FileLength);

		std::vector<uint8_t> Data;
		Data.resize(FileLength);

		ManifestFile.Read(Data.data(), (DWORD)Data.size());

		zen::MemoryInStream MemoryStream(Data.data(), Data.size());

		ReadManifest(/* out */ m_Manifest, MemoryStream);

		uint64_t TotalBytes = 0;
		uint64_t TotalFiles = 0;

		m_Manifest.Root.VisitFiles([&](const zen::LeafNode& Node) {
			TotalBytes += Node.FileSize;
			TotalFiles++;
		});

		m_FileByteCount = TotalBytes;
		m_FileCount		= TotalFiles;

		// CAS root

		zen::CasStoreConfiguration Config;
		Config.RootDirectory = CasSpec;
		m_CasStore->Initialize(Config);

		return S_OK;
	}

	struct LookupResult
	{
		const zen::TreeNode* TreeNode = nullptr;
		const zen::LeafNode* LeafNode = nullptr;
	};

	bool IsOnCasDrive(const char* Path)
	{
		ZEN_UNUSED(Path);

		// TODO: programmatically determine of CAS and workspace path is on same drive!
		return true;
	}

	LookupResult LookupNode(const std::wstring& Name) const
	{
		if (Name.empty())
			return {nullptr};

		zen::ExtendableWideStringBuilder<MAX_PATH> LocalName;
		LocalName.Append(Name.c_str());

		// Split components

		const wchar_t* PathComponents[MAX_PATH / 2];
		size_t		   PathComponentCount = 0;

		const size_t Length = Name.length();

		wchar_t* Base	 = LocalName.Data();
		wchar_t* itStart = Base;

		for (int i = 0; i < Length; ++i)
		{
			if (Base[i] == '\\')
			{
				// Component separator

				Base[i] = L'\0';

				PathComponents[PathComponentCount++] = itStart;

				itStart = Base + i + 1;
			}
		}

		// Push final component
		if (Name.back() != L'\\')
			PathComponents[PathComponentCount++] = itStart;

		const zen::TreeNode* Node = &m_Manifest.Root;

		if (PathComponentCount == 1)
		{
			if (PrjFileNameCompare(L"root", Name.c_str()) == 0)
				return {Node};
			else
				return {nullptr};
		}

		for (size_t i = 1; i < PathComponentCount; ++i)
		{
			const auto& part = PathComponents[i];

			const zen::TreeNode* NextNode = nullptr;

			for (const zen::TreeNode& ChildNode : Node->Children)
			{
				if (PrjFileNameCompare(part, ChildNode.Name.c_str()) == 0)
				{
					NextNode = &ChildNode;
					break;
				}
			}

			if (NextNode)
			{
				Node = NextNode;

				continue;
			}

			if (i == PathComponentCount - 1)
			{
				for (const zen::LeafNode& Leaf : Node->Leaves)
				{
					if (PrjFileNameCompare(part, Leaf.Name.c_str()) == 0)
						return {nullptr, &Leaf};
				}
			}

			return {nullptr};
		}

		return {Node};
	}

	const zen::SnapshotManifest& Manifest() const { return m_Manifest; }
	zen::CasStore&				 CasStore() { return *m_CasStore; }

	uint64_t FileCount() const { return m_FileCount; }
	uint64_t FileByteCount() const { return m_FileByteCount; }

private:
	zen::SnapshotManifest		   m_Manifest;
	std::unique_ptr<zen::CasStore> m_CasStore;

	size_t m_FileCount	   = 0;
	size_t m_FileByteCount = 0;
};

/** Projected File System Provider
 */

class ProjfsProvider
{
public:
	HRESULT ReadManifest(const char* ManifestSpec, const char* CasSpec);
	HRESULT Initialize(std::filesystem::path RootPath, bool Clean);
	void	Cleanup();

	struct Callbacks;

private:
	static void DebugPrint(const char* Format, ...);

	HRESULT StartDirEnum(const PRJ_CALLBACK_DATA* CallbackData, LPCGUID EnumerationId);
	HRESULT EndDirEnum(const PRJ_CALLBACK_DATA* CallbackData, LPCGUID EnumerationId);
	HRESULT GetDirEnum(const PRJ_CALLBACK_DATA*	   CallbackData,
					   LPCGUID					   EnumerationId,
					   LPCWSTR					   SearchExpression,
					   PRJ_DIR_ENTRY_BUFFER_HANDLE DirEntryBufferHandle);
	HRESULT GetPlaceholderInformation(const PRJ_CALLBACK_DATA* CallbackData);
	HRESULT GetFileStream(const PRJ_CALLBACK_DATA* CallbackData, UINT64 ByteOffset, UINT32 Length);
	HRESULT QueryFileName(const PRJ_CALLBACK_DATA* CallbackData);
	HRESULT NotifyOperation(const PRJ_CALLBACK_DATA*	 CallbackData,
							BOOLEAN						 IsDirectory,
							PRJ_NOTIFICATION			 NotificationType,
							LPCWSTR						 DestinationFileName,
							PRJ_NOTIFICATION_PARAMETERS* OperationParameters);
	void	CancelCommand(const PRJ_CALLBACK_DATA* CallbackData);

	class DirectoryEnumeration;

	zen::RwLock																	m_Lock;
	std::unordered_map<GUID, std::unique_ptr<DirectoryEnumeration>, GuidHasher> m_DirectoryEnumerators;
	ProjfsNamespace																m_Namespace;
	PRJ_NAMESPACE_VIRTUALIZATION_CONTEXT										m_PrjContext		= nullptr;
	bool																		m_GenerateFullFiles = false;
};

class ProjfsProvider::DirectoryEnumeration
{
public:
	DirectoryEnumeration(ProjfsProvider* Outer, LPCGUID EnumerationGuid, const wchar_t* RelativePath)
	: m_Outer(Outer)
	, m_EnumerationId(*EnumerationGuid)
	, m_Path(RelativePath)
	{
		ResetScan();
	}

	~DirectoryEnumeration() {}

	void ResetScan()
	{
		// Restart enumeration from beginning

		m_InfoIterator = m_Infos.end();

		const ProjfsNamespace::LookupResult Lookup = m_Outer->m_Namespace.LookupNode(m_Path);

		if (Lookup.TreeNode == nullptr && Lookup.LeafNode == nullptr)
			return;

		if (Lookup.TreeNode)
		{
			const zen::TreeNode* RootNode = Lookup.TreeNode;

			// Populate info array

			FILETIME FileTime;
			GetSystemTimeAsFileTime(&FileTime);

			for (const zen::TreeNode& ChildNode : RootNode->Children)
			{
				PRJ_FILE_BASIC_INFO Fbi{0};

				Fbi.IsDirectory	 = TRUE;
				Fbi.FileSize	 = 0;
				Fbi.CreationTime = Fbi.LastAccessTime = Fbi.LastWriteTime = Fbi.ChangeTime = *((LARGE_INTEGER*)&FileTime);
				Fbi.FileAttributes														   = FILE_ATTRIBUTE_DIRECTORY;

				m_Infos.insert({ChildNode.Name, Fbi});
			}

			for (const zen::LeafNode& Leaf : RootNode->Leaves)
			{
				PRJ_FILE_BASIC_INFO Fbi{0};

				Fbi.IsDirectory	   = FALSE;
				Fbi.FileSize	   = Leaf.FileSize;
				Fbi.FileAttributes = FILE_ATTRIBUTE_NORMAL;
				Fbi.CreationTime = Fbi.LastAccessTime = Fbi.LastWriteTime = Fbi.ChangeTime =
					*reinterpret_cast<const LARGE_INTEGER*>(&Leaf.FileModifiedTime);

				m_Infos.insert({Leaf.Name, Fbi});
			}
		}

		m_InfoIterator = m_Infos.begin();
	}

	HRESULT HandleRequest(_In_ const PRJ_CALLBACK_DATA*	   CallbackData,
						  _In_opt_z_ LPCWSTR			   SearchExpression,
						  _In_ PRJ_DIR_ENTRY_BUFFER_HANDLE DirEntryBufferHandle)
	{
		int EnumLimit = INT_MAX;

		DebugPrint("ENUM '%S' -> pattern %S\n", CallbackData->FilePathName, SearchExpression);

		HRESULT hRes = S_OK;

		if (CallbackData->Flags & PRJ_CB_DATA_FLAG_ENUM_RESTART_SCAN)
			ResetScan();

		if (m_InfoIterator == m_Infos.end())
			return S_OK;

		if (CallbackData->Flags & PRJ_CB_DATA_FLAG_ENUM_RETURN_SINGLE_ENTRY)
			EnumLimit = 1;

		if (!m_Predicate)
		{
			if (SearchExpression)
			{
				bool isWild = PrjDoesNameContainWildCards(SearchExpression);

				if (isWild)
				{
					if (SearchExpression[0] == L'*' && SearchExpression[1] == L'\0')
					{
						// Trivial accept -- no need to change predicate from the default
					}
					else
					{
						m_SearchExpression = SearchExpression;

						m_Predicate = [this](LPCWSTR name) { return PrjFileNameMatch(name, m_SearchExpression.c_str()); };
					}
				}
				else
				{
					if (SearchExpression[0])
					{
						// Look for specific name match (does this ever happen?)

						m_SearchExpression = SearchExpression;

						m_Predicate = [this](LPCWSTR name) { return PrjFileNameCompare(name, m_SearchExpression.c_str()) == 0; };
					}
				}
			}
		}

		if (!m_Predicate)
			m_Predicate = [](LPCWSTR) { return true; };

		while (EnumLimit && m_InfoIterator != m_Infos.end())
		{
			auto& ThisNode = *m_InfoIterator;

			auto& Name = ThisNode.first;
			auto& Info = ThisNode.second;

			if (m_Predicate(Name.c_str()))
			{
				hRes = PrjFillDirEntryBuffer(Name.c_str(), &Info, DirEntryBufferHandle);

				if (hRes == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER))
					return S_OK;

				if (FAILED(hRes))
					break;

				--EnumLimit;
			}

			++m_InfoIterator;
		}

		return hRes;
	}

private:
	ProjfsProvider*	   m_Outer = nullptr;
	const std::wstring m_Path;
	const GUID		   m_EnumerationId;

	// We need to maintain an ordered list of directory items since the
	// ProjFS enumeration code gets confused otherwise and ends up producing
	// multiple entries for the same file if there's a 'hydrated' version
	// present.

	struct FilenameLess
	{
		bool operator()(const std::wstring& Lhs, const std::wstring& Rhs) const { return PrjFileNameCompare(Lhs.c_str(), Rhs.c_str()) < 0; }
	};

	typedef std::map<std::wstring, PRJ_FILE_BASIC_INFO, FilenameLess> FileInfoMap_t;

	FileInfoMap_t			m_Infos;
	FileInfoMap_t::iterator m_InfoIterator;

	std::wstring					  m_SearchExpression;
	std::function<bool(LPCWSTR name)> m_Predicate;
};

//////////////////////////////////////////////////////////////////////////
// Callback forwarding functions
//

struct ProjfsProvider::Callbacks
{
	static HRESULT CALLBACK StartDirEnum(_In_ const PRJ_CALLBACK_DATA* CallbackData, _In_ const GUID* EnumerationId)
	{
		return reinterpret_cast<ProjfsProvider*>(CallbackData->InstanceContext)->StartDirEnum(CallbackData, EnumerationId);
	}

	static HRESULT CALLBACK EndDirEnum(_In_ const PRJ_CALLBACK_DATA* CallbackData, _In_ LPCGUID EnumerationId)
	{
		return reinterpret_cast<ProjfsProvider*>(CallbackData->InstanceContext)->EndDirEnum(CallbackData, EnumerationId);
	}

	static HRESULT CALLBACK GetDirEnum(_In_ const PRJ_CALLBACK_DATA*	CallbackData,
									   _In_ LPCGUID						EnumerationId,
									   _In_opt_z_ LPCWSTR				SearchExpression,
									   _In_ PRJ_DIR_ENTRY_BUFFER_HANDLE DirEntryBufferHandle)
	{
		return reinterpret_cast<ProjfsProvider*>(CallbackData->InstanceContext)
			->GetDirEnum(CallbackData, EnumerationId, SearchExpression, DirEntryBufferHandle);
	}

	static HRESULT CALLBACK GetPlaceholderInformation(_In_ const PRJ_CALLBACK_DATA* CallbackData)
	{
		return reinterpret_cast<ProjfsProvider*>(CallbackData->InstanceContext)->GetPlaceholderInformation(CallbackData);
	}

	static HRESULT CALLBACK GetFileStream(_In_ const PRJ_CALLBACK_DATA* CallbackData, _In_ UINT64 ByteOffset, _In_ UINT32 Length)
	{
		return reinterpret_cast<ProjfsProvider*>(CallbackData->InstanceContext)->GetFileStream(CallbackData, ByteOffset, Length);
	}

	static HRESULT CALLBACK QueryFileName(_In_ const PRJ_CALLBACK_DATA* CallbackData)
	{
		return reinterpret_cast<ProjfsProvider*>(CallbackData->InstanceContext)->QueryFileName(CallbackData);
	}

	static HRESULT CALLBACK NotifyOperation(_In_ const PRJ_CALLBACK_DATA* CallbackData,
											_In_ BOOLEAN				  IsDirectory,
											_In_ PRJ_NOTIFICATION		  NotificationType,
											_In_opt_ LPCWSTR			  DestinationFileName,
											_Inout_ PRJ_NOTIFICATION_PARAMETERS* OperationParameters)
	{
		return reinterpret_cast<ProjfsProvider*>(CallbackData->InstanceContext)
			->NotifyOperation(CallbackData, IsDirectory, NotificationType, DestinationFileName, OperationParameters);
	}

	static VOID CALLBACK CancelCommand(_In_ const PRJ_CALLBACK_DATA* CallbackData)
	{
		return reinterpret_cast<ProjfsProvider*>(CallbackData->InstanceContext)->CancelCommand(CallbackData);
	}
};

// {6EEB94E4-3EF3-4C1C-AF15-D7FF64C19A4F}
static const GUID ProviderGuid = {0x6eeb94e4, 0x3ef3, 0x4c1c, {0xaf, 0x15, 0xd7, 0xff, 0x64, 0xc1, 0x9a, 0x4f}};

void
ProjfsProvider::DebugPrint(const char* FmtString, ...)
{
	va_list vl;
	va_start(vl, FmtString);

#if 0
	vprintf(FmtString, vl);
#endif

	va_end(vl);
}

HRESULT
ProjfsProvider::Initialize(std::filesystem::path RootPath, bool Clean)
{
	PRJ_PLACEHOLDER_VERSION_INFO Pvi = {};
	Pvi.ContentID[0]				 = 1;

	if (Clean && std::filesystem::exists(RootPath))
	{
		printf("Cleaning '%S'...", RootPath.c_str());

		bool success = zen::DeleteDirectories(RootPath);

		if (!success)
		{
			printf(" retrying...");

			success = zen::DeleteDirectories(RootPath);

			// Failed?
		}

		printf(" done!\n");
	}

	bool RootDirectoryCreated = false;

retry:
	if (!std::filesystem::exists(RootPath))
	{
		zen::CreateDirectories(RootPath);
	}

	{
		HRESULT hRes = PrjMarkDirectoryAsPlaceholder(RootPath.c_str(), nullptr, &Pvi, &ProviderGuid);

		if (FAILED(hRes))
		{
			if (hRes == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) && !RootDirectoryCreated)
			{
				printf("Creating '%S'...", RootPath.c_str());

				std::filesystem::create_directories(RootPath.c_str());

				RootDirectoryCreated = true;

				printf("done!\n");

				goto retry;
			}
			else if (hRes == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
			{
				throw zen::WindowsException(hRes, "Failed to initialize root placeholder");
			}

			// Ignore error, problems will be reported below anyway
		}
	}

	// Callbacks

	PRJ_CALLBACKS cbs = {};

	cbs.StartDirectoryEnumerationCallback = Callbacks::StartDirEnum;
	cbs.EndDirectoryEnumerationCallback	  = Callbacks::EndDirEnum;
	cbs.GetDirectoryEnumerationCallback	  = Callbacks::GetDirEnum;
	cbs.GetPlaceholderInfoCallback		  = Callbacks::GetPlaceholderInformation;
	cbs.GetFileDataCallback				  = Callbacks::GetFileStream;
	cbs.QueryFileNameCallback			  = Callbacks::QueryFileName;
	cbs.NotificationCallback			  = Callbacks::NotifyOperation;
	cbs.CancelCommandCallback			  = Callbacks::CancelCommand;

	// Parameters

	const PRJ_NOTIFY_TYPES dwNotifications = PRJ_NOTIFY_FILE_OPENED | PRJ_NOTIFY_NEW_FILE_CREATED | PRJ_NOTIFY_FILE_OVERWRITTEN |
											 PRJ_NOTIFY_PRE_DELETE | PRJ_NOTIFY_PRE_RENAME | PRJ_NOTIFY_PRE_SET_HARDLINK |
											 PRJ_NOTIFY_FILE_RENAMED | PRJ_NOTIFY_HARDLINK_CREATED |
											 PRJ_NOTIFY_FILE_HANDLE_CLOSED_NO_MODIFICATION | PRJ_NOTIFY_FILE_HANDLE_CLOSED_FILE_MODIFIED |
											 PRJ_NOTIFY_FILE_HANDLE_CLOSED_FILE_DELETED | PRJ_NOTIFY_FILE_PRE_CONVERT_TO_FULL;

	PRJ_NOTIFICATION_MAPPING Mappings[] = {{dwNotifications, L"root"}};

	PRJ_STARTVIRTUALIZING_OPTIONS SvOptions = {};

	SvOptions.Flags						= PRJ_FLAG_NONE;
	SvOptions.PoolThreadCount			= 8;
	SvOptions.ConcurrentThreadCount		= 8;
	SvOptions.NotificationMappings		= Mappings;
	SvOptions.NotificationMappingsCount = 1;

	HRESULT hRes = PrjStartVirtualizing(RootPath.c_str(), &cbs, this, &SvOptions, &m_PrjContext);

	if (SUCCEEDED(hRes))
	{
		// Create dummy 'root' directory for now until I figure out how to
		// invalidate entire trees (ProjFS won't allow invalidation of the
		// entire provider tree).

		PRJ_PLACEHOLDER_INFO pli{};
		pli.FileBasicInfo.IsDirectory	 = TRUE;
		pli.FileBasicInfo.FileAttributes = FILE_ATTRIBUTE_DIRECTORY;
		pli.VersionInfo					 = Pvi;

		hRes = PrjWritePlaceholderInfo(m_PrjContext, L"root", &pli, sizeof pli);
	}

	if (SUCCEEDED(hRes))
	{
		spdlog::info("Successfully mounted snapshot at '{}'!", WideToUtf8(RootPath.c_str()));
	}
	else
	{
		spdlog::info("Failed mounting snapshot at '{}'!", WideToUtf8(RootPath.c_str()));
	}

	return hRes;
}

void
ProjfsProvider::Cleanup()
{
	PrjStopVirtualizing(m_PrjContext);
}

HRESULT
ProjfsProvider::ReadManifest(const char* ManifestSpec, const char* CasSpec)
{
	printf("Initializing from manifest '%s'\n", ManifestSpec);

	m_Namespace.Initialize(ManifestSpec, CasSpec);

	return S_OK;
}

HRESULT
ProjfsProvider::StartDirEnum(const PRJ_CALLBACK_DATA* CallbackData, LPCGUID EnumerationId)
{
	zen::RwLock::ExclusiveLockScope _(m_Lock);

	m_DirectoryEnumerators[*EnumerationId] = std::make_unique<DirectoryEnumeration>(this, EnumerationId, CallbackData->FilePathName);

	return S_OK;
}

HRESULT
ProjfsProvider::EndDirEnum(const PRJ_CALLBACK_DATA* CallbackData, LPCGUID EnumerationId)
{
	ZEN_UNUSED(CallbackData);
	ZEN_UNUSED(EnumerationId);

	zen::RwLock::ExclusiveLockScope _(m_Lock);

	m_DirectoryEnumerators.erase(*EnumerationId);

	return S_OK;
}

HRESULT
ProjfsProvider::GetDirEnum(const PRJ_CALLBACK_DATA*	   CallbackData,
						   LPCGUID					   EnumerationId,
						   LPCWSTR					   SearchExpression,
						   PRJ_DIR_ENTRY_BUFFER_HANDLE DirEntryBufferHandle)
{
	DirectoryEnumeration* directoryEnumerator;

	{
		zen::RwLock::SharedLockScope _(m_Lock);

		auto it = m_DirectoryEnumerators.find(*EnumerationId);

		if (it == m_DirectoryEnumerators.end())
			return E_FAIL;	// No enumerator associated with specified GUID

		directoryEnumerator = (*it).second.get();
	}

	return directoryEnumerator->HandleRequest(CallbackData, SearchExpression, DirEntryBufferHandle);
}

HRESULT
ProjfsProvider::GetPlaceholderInformation(const PRJ_CALLBACK_DATA* CallbackData)
{
	ProjfsNamespace::LookupResult result = m_Namespace.LookupNode(CallbackData->FilePathName);

	if (auto Leaf = result.LeafNode)
	{
		PRJ_PLACEHOLDER_INFO PlaceholderInfo = {};

		LARGE_INTEGER FileTime;
		FileTime.QuadPart = Leaf->FileModifiedTime;

		PlaceholderInfo.FileBasicInfo.ChangeTime	 = FileTime;
		PlaceholderInfo.FileBasicInfo.CreationTime	 = FileTime;
		PlaceholderInfo.FileBasicInfo.LastAccessTime = FileTime;
		PlaceholderInfo.FileBasicInfo.LastWriteTime	 = FileTime;
		PlaceholderInfo.FileBasicInfo.FileSize		 = Leaf->FileSize;
		PlaceholderInfo.FileBasicInfo.IsDirectory	 = 0;
		PlaceholderInfo.FileBasicInfo.FileAttributes = FILE_ATTRIBUTE_NORMAL;

		HRESULT hRes = PrjWritePlaceholderInfo(m_PrjContext, CallbackData->FilePathName, &PlaceholderInfo, sizeof PlaceholderInfo);

		return hRes;
	}

	if (auto node = result.TreeNode)
	{
		PRJ_PLACEHOLDER_INFO PlaceholderInfo = {};

		FILETIME ft;
		GetSystemTimeAsFileTime(&ft);

		LARGE_INTEGER FileTime;
		FileTime.QuadPart = UINT64(ft.dwHighDateTime) << 32 | ft.dwLowDateTime;

		PlaceholderInfo.FileBasicInfo.ChangeTime	 = FileTime;
		PlaceholderInfo.FileBasicInfo.CreationTime	 = FileTime;
		PlaceholderInfo.FileBasicInfo.LastAccessTime = FileTime;
		PlaceholderInfo.FileBasicInfo.LastWriteTime	 = FileTime;
		PlaceholderInfo.FileBasicInfo.IsDirectory	 = TRUE;
		PlaceholderInfo.FileBasicInfo.FileAttributes = FILE_ATTRIBUTE_DIRECTORY;

		HRESULT hRes = PrjWritePlaceholderInfo(m_PrjContext, CallbackData->FilePathName, &PlaceholderInfo, sizeof PlaceholderInfo);

		return hRes;
	}

	return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
}

HRESULT
ProjfsProvider::GetFileStream(const PRJ_CALLBACK_DATA* CallbackData, UINT64 ByteOffset, UINT32 Length)
{
	ProjfsNamespace::LookupResult result = m_Namespace.LookupNode(CallbackData->FilePathName);

	if (const zen::LeafNode* leaf = result.LeafNode)
	{
		zen::CasStore& casStore = m_Namespace.CasStore();

		const zen::IoHash& ChunkHash = leaf->ChunkHash;

		zen::IoBuffer Chunk = casStore.FindChunk(ChunkHash);

		if (!Chunk)
			return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

		if (m_GenerateFullFiles)
		{
			DWORD chunkSize = (DWORD)Chunk.Size();

			zen::StringBuilder<66> b3string;
			DebugPrint("GET FILE STREAM: %s -> %d '%S'\n", ChunkHash.ToHexString(b3string).c_str(), chunkSize, CallbackData->FilePathName);

			// TODO: implement support for chunks > 4GB
			ZEN_ASSERT(chunkSize == Chunk.Size());

			HRESULT hRes = PrjWriteFileData(m_PrjContext, &CallbackData->DataStreamId, (PVOID)Chunk.Data(), 0, chunkSize);

			return hRes;
		}
		else
		{
			HRESULT hRes = PrjWriteFileData(m_PrjContext,
											&CallbackData->DataStreamId,
											(PVOID)(reinterpret_cast<const uint8_t*>(Chunk.Data()) + ByteOffset),
											ByteOffset,
											Length);

			return hRes;
		}
	}

	return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
}

HRESULT
ProjfsProvider::QueryFileName(const PRJ_CALLBACK_DATA* CallbackData)
{
	ProjfsNamespace::LookupResult result = m_Namespace.LookupNode(CallbackData->FilePathName);

	if (result.LeafNode || result.TreeNode)
		return S_OK;

	return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
}

HRESULT
ProjfsProvider::NotifyOperation(const PRJ_CALLBACK_DATA*	 CallbackData,
								BOOLEAN						 IsDirectory,
								PRJ_NOTIFICATION			 NotificationType,
								LPCWSTR						 DestinationFileName,
								PRJ_NOTIFICATION_PARAMETERS* OperationParameters)
{
	ZEN_UNUSED(DestinationFileName);

	switch (NotificationType)
	{
		case PRJ_NOTIFICATION_FILE_OPENED:
			{
				auto& pc = OperationParameters->PostCreate;

				DebugPrint("*** OPEN: %s %08x '%S'\n", IsDirectory ? "(DIR)" : "-FILE", pc.NotificationMask, CallbackData->FilePathName);
			}
			break;

		case PRJ_NOTIFICATION_NEW_FILE_CREATED:
			{
				auto& pc = OperationParameters->PostCreate;

				DebugPrint("*** NEW : %s %08x '%S'\n", IsDirectory ? "(DIR)" : "-FILE", pc.NotificationMask, CallbackData->FilePathName);
			}
			break;

		case PRJ_NOTIFICATION_FILE_OVERWRITTEN:
			{
				auto& pc = OperationParameters->PostCreate;

				DebugPrint("*** OVER: %s %08x '%S'\n", IsDirectory ? "(DIR)" : "-FILE", pc.NotificationMask, CallbackData->FilePathName);
			}
			break;

		case PRJ_NOTIFICATION_PRE_DELETE:
			{
				if (wcsstr(CallbackData->FilePathName, L"en-us"))
					DebugPrint("*** PRE DELETE '%S'\n", CallbackData->FilePathName);

				DebugPrint("*** PRE DELETE '%S'\n", CallbackData->FilePathName);
			}
			break;

		case PRJ_NOTIFICATION_PRE_RENAME:
			DebugPrint("*** PRE RENAME '%S'\n", CallbackData->FilePathName);
			break;

		case PRJ_NOTIFICATION_PRE_SET_HARDLINK:
			DebugPrint("*** PRE SET HARDLINK '%S'\n", CallbackData->FilePathName);
			break;

		case PRJ_NOTIFICATION_FILE_RENAMED:
			DebugPrint("*** FILE RENAMED '%S'\n", CallbackData->FilePathName);
			break;

		case PRJ_NOTIFICATION_HARDLINK_CREATED:
			DebugPrint("*** HARDLINK RENAMED '%S'\n", CallbackData->FilePathName);
			break;

		case PRJ_NOTIFICATION_FILE_HANDLE_CLOSED_NO_MODIFICATION:
			DebugPrint("*** FILE CLOSED NO CHANGE '%S'\n", CallbackData->FilePathName);
			break;

		case PRJ_NOTIFICATION_FILE_HANDLE_CLOSED_FILE_MODIFIED:
			{
				// const auto& handleClose = OperationParameters->FileDeletedOnHandleClose;

				DebugPrint("*** FILE CLOSED MODIFIED '%S'\n", CallbackData->FilePathName);
			}
			break;

		case PRJ_NOTIFICATION_FILE_HANDLE_CLOSED_FILE_DELETED:
			{
				// const auto& handleClose = OperationParameters->FileDeletedOnHandleClose;

				DebugPrint("*** FILE CLOSED DELETED '%S'\n", CallbackData->FilePathName);
			}
			break;

		case PRJ_NOTIFICATION_FILE_PRE_CONVERT_TO_FULL:
			DebugPrint("*** FILE PRE CONVERT FULL '%S'\n", CallbackData->FilePathName);
			break;
	}

	return S_OK;
}

void
ProjfsProvider::CancelCommand(const PRJ_CALLBACK_DATA* CallbackData)
{
	ZEN_UNUSED(CallbackData);
}

//////////////////////////////////////////////////////////////////////////

struct Vfs::VfsImpl
{
	void Initialize() { m_PrjProvider.Initialize("E:\\VFS_Test", /* clean */ true); }
	void Start() {}
	void Stop() {}

private:
	ProjfsProvider m_PrjProvider;
};

//////////////////////////////////////////////////////////////////////////

Vfs::Vfs() : m_Impl(new VfsImpl)
{
}

Vfs::~Vfs()
{
}

void
Vfs::Initialize()
{
	m_Impl->Initialize();
}

void
Vfs::Start()
{
}

void
Vfs::Stop()
{
}

}  // namespace zen

