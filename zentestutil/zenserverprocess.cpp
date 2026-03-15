// Copyright Noah Games, Inc. All Rights Reserved.

#include "zenserverprocess.h"

#include <zencore/filesystem.h>
#include <zencore/fmtutils.h>
#include <zencore/string.h>

#include <spdlog/spdlog.h>

//////////////////////////////////////////////////////////////////////////

std::atomic<int> TestCounter{0};

ZenTestEnvironment::ZenTestEnvironment()
{
}

ZenTestEnvironment::~ZenTestEnvironment()
{
}

void
ZenTestEnvironment::Initialize(std::filesystem::path ProgramBaseDir, std::filesystem::path TestBaseDir)
{
	m_ProgramBaseDir = ProgramBaseDir;
	m_TestBaseDir	 = TestBaseDir;

	spdlog::info("Cleaning '{}'", TestBaseDir);
	zen::DeleteDirectories(TestBaseDir.c_str());

	m_IsInitialized = true;
}

std::filesystem::path
ZenTestEnvironment::CreateNewTestDir()
{
	using namespace std::literals;

	zen::ExtendableWideStringBuilder<256> TestDir;
	TestDir << "test"sv << int64_t(++TestCounter);

	std::filesystem::path TestPath = m_TestBaseDir / TestDir.c_str();

	zen::CreateDirectories(TestPath.c_str());

	return TestPath;
}

//////////////////////////////////////////////////////////////////////////

std::atomic<int> ChildIdCounter{0};

ZenServerInstance::ZenServerInstance(ZenTestEnvironment& TestEnvironment) : m_Env(TestEnvironment)
{
	ZEN_ASSERT(TestEnvironment.IsInitialized());
}

ZenServerInstance::~ZenServerInstance()
{
	if (m_Process.IsValid())
	{
		if (m_Terminate)
		{
			spdlog::info("Terminating zenserver process");
			m_Process.Terminate(111);
		}
		else
		{
			SignalShutdown();
			m_Process.Wait();
		}
	}
}

void
ZenServerInstance::SpawnServer(int BasePort)
{
	ZEN_ASSERT(!m_Process.IsValid());  // Only spawn once

	const std::filesystem::path BaseDir	   = m_Env.ProgramBaseDir();
	const std::filesystem::path Executable = BaseDir / "zenserver.exe";

	const int MyPid	  = _getpid();
	const int ChildId = ++ChildIdCounter;

	zen::ExtendableStringBuilder<32> ChildEventName;
	ChildEventName << "Zen_Child_" << ChildId;
	zen::NamedEvent ChildEvent{ChildEventName};

	zen::ExtendableStringBuilder<32> ChildShutdownEventName;
	ChildShutdownEventName << "Zen_Child_" << ChildId;
	ChildShutdownEventName << "_Shutdown";
	zen::NamedEvent ChildShutdownEvent{ChildShutdownEventName};

	zen::ExtendableStringBuilder<32> LogId;
	LogId << "Zen" << ChildId;

	zen::ExtendableWideStringBuilder<128> CommandLine;
	CommandLine << "\"";
	CommandLine.Append(Executable.c_str());
	CommandLine << "\" --test --owner-pid ";
	CommandLine << MyPid;
	CommandLine << " ";
	CommandLine << "--port " << BasePort;
	CommandLine << " --child-id " << ChildEventName;
	CommandLine << " --log-id " << LogId;

	if (!m_TestDir.empty())
	{
		CommandLine << " --data-dir ";
		CommandLine << m_TestDir.c_str();
	}

	std::filesystem::path CurrentDirectory = std::filesystem::current_path();

	spdlog::debug("Spawning server");

	PROCESS_INFORMATION ProcessInfo{};
	STARTUPINFO			Sinfo{.cb = sizeof(STARTUPINFO)};

	DWORD				  CreationFlags		= 0;  // CREATE_NEW_CONSOLE;
	const bool			  InheritHandles	= false;
	void*				  Environment		= nullptr;
	LPSECURITY_ATTRIBUTES ProcessAttributes = nullptr;
	LPSECURITY_ATTRIBUTES ThreadAttributes	= nullptr;

	BOOL Success = CreateProcessW(Executable.c_str(),
								  (LPWSTR)CommandLine.c_str(),
								  ProcessAttributes,
								  ThreadAttributes,
								  InheritHandles,
								  CreationFlags,
								  Environment,
								  CurrentDirectory.c_str(),
								  &Sinfo,
								  &ProcessInfo);

	if (Success == FALSE)
	{
		std::error_code err(::GetLastError(), std::system_category());

		spdlog::error("Server spawn failed: {}", err.message());

		throw std::system_error(err, "failed to create server process");
	}

	spdlog::debug("Server spawned OK");

	CloseHandle(ProcessInfo.hThread);

	m_Process.Initialize(ProcessInfo.hProcess);
	m_ReadyEvent	= std::move(ChildEvent);
	m_ShutdownEvent = std::move(ChildShutdownEvent);
}

