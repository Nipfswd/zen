// Copyright Noah Games, Inc. All Rights Reserved.

#include "launch.h"

#include <zencore/compactbinary.h>
#include <zencore/compactbinarybuilder.h>
#include <zencore/filesystem.h>
#include <zencore/fmtutils.h>
#include <zencore/iobuffer.h>
#include <zencore/iohash.h>
#include <zencore/windows.h>
#include <zenstore/CAS.h>

#include <AccCtrl.h>
#include <AclAPI.h>
#include <sddl.h>

#include <UserEnv.h>
#pragma comment(lib, "UserEnv.lib")

#include <atlbase.h>
#include <filesystem>
#include <span>

using namespace std::literals;

namespace zen {

struct BasicJob
{
public:
	BasicJob() = default;
	~BasicJob();

	void SetWorkingDirectory(const std::filesystem::path& WorkingDirectory) { m_WorkingDirectory = WorkingDirectory; }
	bool SpawnJob(std::filesystem::path ExePath, std::wstring CommandLine);
	bool Wait(uint32_t TimeoutMs = ~0);

private:
	std::filesystem::path m_WorkingDirectory;
	int					  m_ProcessId = 0;
	CHandle				  m_ProcessHandle;
};

BasicJob::~BasicJob()
{
	Wait();
}

bool
BasicJob::SpawnJob(std::filesystem::path ExePath, std::wstring CommandLine)
{
	using namespace fmt::literals;

	STARTUPINFOEX		StartupInfo = {sizeof(STARTUPINFOEX)};
	PROCESS_INFORMATION ProcessInfo{};

	std::wstring ExePathNative	  = ExePath.native();
	std::wstring WorkingDirNative = m_WorkingDirectory.native();

	BOOL Created = ::CreateProcess(ExePathNative.data() /* ApplicationName */,
								   CommandLine.data() /* Command Line */,
								   nullptr /* Process Attributes */,
								   nullptr /* Security Attributes */,
								   FALSE /* InheritHandles */,
								   0 /* Flags */,
								   nullptr /* Environment */,
								   WorkingDirNative.data() /* Current Directory */,
								   (LPSTARTUPINFO)&StartupInfo,
								   &ProcessInfo);

	if (!Created)
	{
		throw std::system_error(::GetLastError(), std::system_category(), "Failed to create process '{}'"_format(ExePath).c_str());
	}

	m_ProcessId = ProcessInfo.dwProcessId;
	m_ProcessHandle.Attach(ProcessInfo.hProcess);
	::CloseHandle(ProcessInfo.hThread);

	spdlog::info("Created process {}", m_ProcessId);

	return true;
}

bool
BasicJob::Wait(uint32_t TimeoutMs)
{
	if (!m_ProcessHandle)
	{
		return true;
	}

	DWORD WaitResult = WaitForSingleObject(m_ProcessHandle, TimeoutMs);

	if (WaitResult == WAIT_TIMEOUT)
	{
		return false;
	}

	if (WaitResult == WAIT_OBJECT_0)
	{
		return true;
	}

	throw std::exception("Failed wait on process handle");
}

struct SandboxedJob
{
	SandboxedJob()	= default;
	~SandboxedJob() = default;

	void SetWorkingDirectory(const std::filesystem::path& WorkingDirectory) { m_WorkingDirectory = WorkingDirectory; }
	void Initialize(std::string_view AppContainerId);
	bool SpawnJob(std::filesystem::path ExePath);
	void AddWhitelistFile(const std::filesystem::path& FilePath) { m_WhitelistFiles.push_back(FilePath); }

private:
	bool GrantNamedObjectAccess(PWSTR Name, SE_OBJECT_TYPE Type, ACCESS_MASK AccessMask, bool Recursive);

	std::filesystem::path			   m_WorkingDirectory;
	std::vector<std::filesystem::path> m_WhitelistFiles;
	std::vector<std::wstring>		   m_WhitelistRegistryKeys;
	PSID							   m_AppContainerSid = nullptr;
	bool							   m_IsInitialized	 = false;
};

bool
SandboxedJob::GrantNamedObjectAccess(PWSTR ObjectName, SE_OBJECT_TYPE ObjectType, ACCESS_MASK AccessMask, bool Recursive)
{
	DWORD Status;
	PACL  NewAcl = nullptr;

	DWORD grfInhericance = 0;

	if (Recursive)
	{
		grfInhericance = OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE;
	}

	EXPLICIT_ACCESS Access{.grfAccessPermissions = AccessMask,
						   .grfAccessMode		 = GRANT_ACCESS,
						   .grfInheritance		 = grfInhericance,
						   .Trustee				 = {.pMultipleTrustee		  = nullptr,
										.MultipleTrusteeOperation = NO_MULTIPLE_TRUSTEE,
										.TrusteeForm			  = TRUSTEE_IS_SID,
										.TrusteeType			  = TRUSTEE_IS_GROUP,
										.ptstrName				  = (PWSTR)m_AppContainerSid}};

	PACL OldAcl = nullptr;

	Status = GetNamedSecurityInfo(ObjectName /* ObjectName */,
								  ObjectType /* ObjectType */,
								  DACL_SECURITY_INFORMATION /* SecurityInfo */,
								  nullptr /* ppsidOwner */,
								  nullptr /* ppsidGroup */,
								  &OldAcl /* ppDacl */,
								  nullptr /* ppSacl */,
								  nullptr /* ppSecurityDescriptor */);
	if (Status != ERROR_SUCCESS)
		return false;

	Status = SetEntriesInAcl(1 /* CountOfExplicitEntries */, &Access /* pListOfExplicitEntries */, OldAcl, &NewAcl);
	if (Status != ERROR_SUCCESS)
		return false;

	Status = SetNamedSecurityInfo(ObjectName /* ObjectName */,
								  ObjectType /* ObjectType */,
								  DACL_SECURITY_INFORMATION /*SecurityInfo */,
								  nullptr /* psidOwner */,
								  nullptr /* psidGroup */,
								  NewAcl /* pDacl */,
								  nullptr /* pSacl */);
	if (NewAcl)
		::LocalFree(NewAcl);

	return Status == ERROR_SUCCESS;
}

void
SandboxedJob::Initialize(std::string_view AppContainerId)
{
	if (m_IsInitialized)
	{
		return;
	}

	std::wstring ContainerName = zen::Utf8ToWide(AppContainerId);

	HRESULT hRes = ::CreateAppContainerProfile(ContainerName.c_str(),
											   ContainerName.c_str() /* Display Name */,
											   ContainerName.c_str() /* Description */,
											   nullptr /* Capabilities */,
											   0 /* Capability Count */,
											   &m_AppContainerSid);

	if (FAILED(hRes))
	{
		hRes = ::DeriveAppContainerSidFromAppContainerName(ContainerName.c_str(), &m_AppContainerSid);

		if (FAILED(hRes))
		{
			spdlog::error("Failed creating app container SID");
		}
	}

	// Debugging context

	PWSTR Str = nullptr;
	::ConvertSidToStringSid(m_AppContainerSid, &Str);

	spdlog::info("AppContainer SID : '{}'", WideToUtf8(Str));

	PWSTR Path = nullptr;
	if (SUCCEEDED(::GetAppContainerFolderPath(Str, &Path)))
	{
		spdlog::info("AppContainer folder: '{}'", WideToUtf8(Path));

		::CoTaskMemFree(Path);
	}
	::LocalFree(Str);

	m_IsInitialized = true;
}

bool
SandboxedJob::SpawnJob(std::filesystem::path ExePath)
{
	// Build process attributes

	SECURITY_CAPABILITIES Sc = {0};
	Sc.AppContainerSid		 = m_AppContainerSid;

	STARTUPINFOEX		StartupInfo = {sizeof(STARTUPINFOEX)};
	PROCESS_INFORMATION ProcessInfo{};
	SIZE_T				Size = 0;

	::InitializeProcThreadAttributeList(nullptr, 1, 0, &Size);

	auto AttrBuffer				= std::make_unique<uint8_t[]>(Size);
	StartupInfo.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(AttrBuffer.get());

	if (!::InitializeProcThreadAttributeList(StartupInfo.lpAttributeList, 1, 0, &Size))
	{
		return false;
	}

	if (!::UpdateProcThreadAttribute(StartupInfo.lpAttributeList,
									 0,
									 PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
									 &Sc,
									 sizeof Sc,
									 nullptr,
									 nullptr))
	{
		return false;
	}

	// Set up security for files/folders/registry

	for (const std::filesystem::path& File : m_WhitelistFiles)
	{
		std::wstring NativeFileName = File.native();
		GrantNamedObjectAccess(NativeFileName.data(), SE_FILE_OBJECT, FILE_ALL_ACCESS, true);
	}

	for (std::wstring& RegKey : m_WhitelistRegistryKeys)
	{
		GrantNamedObjectAccess(RegKey.data(), SE_REGISTRY_WOW64_32KEY, KEY_ALL_ACCESS, true);
	}

	std::wstring ExePathNative	  = ExePath.native();
	std::wstring WorkingDirNative = m_WorkingDirectory.native();

	BOOL Created = ::CreateProcess(nullptr /* ApplicationName */,
								   ExePathNative.data() /* Command line */,
								   nullptr /* Process Attributes */,
								   nullptr /* Security Attributes */,
								   FALSE /* InheritHandles */,
								   EXTENDED_STARTUPINFO_PRESENT | CREATE_NEW_CONSOLE /* Flags */,
								   nullptr /* Environment */,
								   WorkingDirNative.data() /* Current Directory */,
								   (LPSTARTUPINFO)&StartupInfo,
								   &ProcessInfo);

	DeleteProcThreadAttributeList(StartupInfo.lpAttributeList);

	if (!Created)
	{
		return false;
	}

	spdlog::info("Created process {}", ProcessInfo.dwProcessId);

	return true;
}

HttpLaunchService::HttpLaunchService(CasStore& Store)
: m_Log("exec", begin(spdlog::default_logger()->sinks()), end(spdlog::default_logger()->sinks()))
, m_CasStore(Store)
{
	m_Router.AddPattern("job", "([[:digit:]]+)");

	m_Router.RegisterRoute(
		"jobs/{job}",
		[this](HttpRouterRequest& Req) {
			HttpServerRequest& HttpReq = Req.ServerRequest();

			switch (HttpReq.RequestVerb())
			{
				case HttpVerb::kGet:
					break;

				case HttpVerb::kPost:
					break;
			}
		},
		HttpVerb::kGet | HttpVerb::kPost);

	// Experimental

#if 0
	m_Router.RegisterRoute(
		"jobs/sandbox",
		[this](HttpRouterRequest& Req) {
			HttpServerRequest& HttpReq = Req.ServerRequest();

			switch (HttpReq.RequestVerb())
			{
				case HttpVerb::kGet:
					break;

				case HttpVerb::kPost:
					{
						SandboxedJob Job;
						Job.Initialize("zen_test");
						Job.SetWorkingDirectory("c:\\temp\\sandbox1");
						Job.AddWhitelistFile("c:\\temp\\sandbox1");
						Job.SpawnJob("c:\\windows\\system32\\cmd.exe");
					}
					break;
			}
		},
		HttpVerb::kGet | HttpVerb::kPost);
#endif

	m_Router.RegisterRoute(
		"jobs/prep",
		[this](HttpRouterRequest& Req) {
			HttpServerRequest& HttpReq = Req.ServerRequest();

			switch (HttpReq.RequestVerb())
			{
				case HttpVerb::kPost:
					{
						// This operation takes the proposed job spec and identifies which
						// chunks are not present on this server. This list is then returned in
						// the "need" list in the response

						IoBuffer Payload	   = HttpReq.ReadPayload();
						CbObject RequestObject = LoadCompactBinaryObject(Payload);

						std::vector<IoHash> NeedList;

						for (auto Entry : RequestObject["files"sv])
						{
							CbObjectView Ob = Entry.AsObjectView();

							const IoHash FileHash = Ob["hash"sv].AsHash();

							if (!m_CasStore.FindChunk(FileHash))
							{
								spdlog::debug("NEED: {} {} {}", FileHash, Ob["file"sv].AsString(), Ob["size"sv].AsUInt64());

								NeedList.push_back(FileHash);
							}
						}

						CbObjectWriter Cbo;
						Cbo.BeginArray("need");

						for (const IoHash& Hash : NeedList)
						{
							Cbo << Hash;
						}

						Cbo.EndArray();
						CbObject Response = Cbo.Save();

						return HttpReq.WriteResponse(HttpResponse::OK, Response);
					}
					break;
			}
		},
		HttpVerb::kPost);

	m_Router.RegisterRoute(
		"jobs",
		[this](HttpRouterRequest& Req) {
			HttpServerRequest& HttpReq = Req.ServerRequest();

			switch (HttpReq.RequestVerb())
			{
				case HttpVerb::kGet:
					break;

				case HttpVerb::kPost:
					{
						IoBuffer Payload	   = HttpReq.ReadPayload();
						CbObject RequestObject = LoadCompactBinaryObject(Payload);

						bool AllOk = true;

						std::vector<IoHash> NeedList;

						// TODO: auto-generate!
						std::filesystem::path SandboxDir{"c:\\temp\\sandbox1"};
						zen::DeleteDirectories(SandboxDir);
						zen::CreateDirectories(SandboxDir);

						for (auto Entry : RequestObject["files"sv])
						{
							CbObjectView Ob = Entry.AsObjectView();

							std::string_view FileName = Ob["file"sv].AsString();
							const IoHash	 FileHash = Ob["hash"sv].AsHash();
							uint64_t		 FileSize = Ob["size"sv].AsUInt64();

							if (IoBuffer Chunk = m_CasStore.FindChunk(FileHash); !Chunk)
							{
								spdlog::debug("MISSING: {} {} {}", FileHash, FileName, FileSize);
								AllOk = false;

								NeedList.push_back(FileHash);
							}
							else
							{
								std::filesystem::path FullPath = SandboxDir / FileName;

								const IoBuffer* Chunks[] = {&Chunk};

								zen::WriteFile(FullPath, Chunks, 1);
							}
						}

						if (!AllOk)
						{
							// TODO: Could report all the missing pieces in the response here
							return HttpReq.WriteResponse(HttpResponse::NotFound);
						}

						std::wstring Executable = Utf8ToWide(RequestObject["cmd"].AsString());
						std::wstring Args		= Utf8ToWide(RequestObject["args"].AsString());

						std::filesystem::path ExeName = SandboxDir / Executable;

						BasicJob Job;
						Job.SetWorkingDirectory(SandboxDir);
						Job.SpawnJob(ExeName, Args);
						Job.Wait();

						return HttpReq.WriteResponse(HttpResponse::OK);
					}
					break;
			}
		},
		HttpVerb::kGet | HttpVerb::kPost);
}

HttpLaunchService::~HttpLaunchService()
{
}

const char*
HttpLaunchService::BaseUri() const
{
	return "/exec/";
}

void
HttpLaunchService::HandleRequest(HttpServerRequest& Request)
{
	if (m_Router.HandleRequest(Request) == false)
	{
		m_Log.warn("No route found for {0}", Request.RelativeUri());
	}
}

}  // namespace zen

