// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <zencore/thread.h>

#include <spdlog/spdlog.h>

#include <filesystem>

class ZenTestEnvironment
{
public:
	ZenTestEnvironment();
	~ZenTestEnvironment();

	void Initialize(std::filesystem::path ProgramBaseDir, std::filesystem::path TestBaseDir);

	std::filesystem::path CreateNewTestDir();
	std::filesystem::path ProgramBaseDir() const { return m_ProgramBaseDir; }
	bool				  IsInitialized() const { return m_IsInitialized; }

private:
	std::filesystem::path m_ProgramBaseDir;
	std::filesystem::path m_TestBaseDir;
	bool				  m_IsInitialized = false;
};

struct ZenServerInstance
{
	ZenServerInstance(ZenTestEnvironment& TestEnvironment);
	~ZenServerInstance();

	void SignalShutdown() { m_ShutdownEvent.Set(); }
	void WaitUntilReady() { m_ReadyEvent.Wait(); }
	void EnableTermination() { m_Terminate = true; }

	void SetTestDir(std::filesystem::path TestDir)
	{
		ZEN_ASSERT(!m_Process.IsValid());
		m_TestDir = TestDir;
	}

	void SpawnServer(int BasePort);

private:
	ZenTestEnvironment&	  m_Env;
	zen::Process		  m_Process;
	zen::Event			  m_ReadyEvent;
	zen::Event			  m_ShutdownEvent;
	bool				  m_Terminate = false;
	std::filesystem::path m_TestDir;
};

