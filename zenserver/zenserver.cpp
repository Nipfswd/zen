// Copyright Noah Games, Inc. All Rights Reserved.

#include <zencore/filesystem.h>
#include <zencore/fmtutils.h>
#include <zencore/httpserver.h>
#include <zencore/iobuffer.h>
#include <zencore/refcount.h>
#include <zencore/string.h>
#include <zencore/thread.h>
#include <zencore/timer.h>
#include <zencore/windows.h>
#include <zenstore/cas.h>

#include <fmt/format.h>
#include <mimalloc-new-delete.h>
#include <mimalloc.h>
#include <spdlog/spdlog.h>
#include <asio.hpp>
#include <list>
#include <lua.hpp>
#include <optional>
#include <regex>
#include <unordered_map>

//////////////////////////////////////////////////////////////////////////
// We don't have any doctest code in this file but this is needed to bring
// in some shared code into the executable

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#undef DOCTEST_CONFIG_IMPLEMENT

//////////////////////////////////////////////////////////////////////////

#include "casstore.h"
#include "config.h"
#include "diag/crashreport.h"
#include "diag/logging.h"

//////////////////////////////////////////////////////////////////////////
// Services
//

#include "admin/admin.h"
#include "cache/kvcache.h"
#include "cache/structuredcache.h"
#include "diag/diagsvcs.h"
#include "experimental/usnjournal.h"
#include "projectstore.h"
#include "testing/launch.h"
#include "upstream/jupiter.h"
#include "upstream/zen.h"
#include "zenstore/gc.h"
#include "zenstore/scrub.h"

#define ZEN_APP_NAME "Zen store"

class ZenServer
{
public:
	void Initialize(int BasePort, int ParentPid)
	{
		using namespace fmt::literals;
		spdlog::info(ZEN_APP_NAME " initializing");

		if (ParentPid)
		{
			m_Process.Initialize(ParentPid);
		}

		// Prototype config system, let's see how this pans out

		ZenServiceConfig ServiceConfig;
		ParseServiceConfig(m_DataRoot, /* out */ ServiceConfig);

		// Ok so now we're configured, let's kick things off

		zen::CasStoreConfiguration Config;
		Config.RootDirectory = m_DataRoot / "CAS";

		m_CasStore->Initialize(Config);

		spdlog::info("instantiating project service");

		m_ProjectStore = new zen::ProjectStore(*m_CasStore, m_DataRoot / "Builds");
		m_HttpProjectService.reset(new zen::HttpProjectService{*m_CasStore, m_ProjectStore});
		m_LocalProjectService = zen::LocalProjectService::New(*m_CasStore, m_ProjectStore);

		m_HttpLaunchService = std::make_unique<zen::HttpLaunchService>(*m_CasStore);

		if (ServiceConfig.LegacyCacheEnabled)
		{
			spdlog::info("instantiating legacy cache service");
			m_CacheService.reset(new zen::HttpKvCacheService());
		}
		else
		{
			spdlog::info("NOT instantiating legacy cache service");
		}

		if (ServiceConfig.StructuredCacheEnabled)
		{
			spdlog::info("instantiating structured cache service");
			m_StructuredCacheService.reset(new zen::HttpStructuredCacheService(m_DataRoot / "cache", *m_CasStore));
		}
		else
		{
			spdlog::info("NOT instantiating structured cache service");
		}

		m_Http.Initialize(BasePort);
		m_Http.AddEndpoint(m_HealthService);
		m_Http.AddEndpoint(m_TestService);
		m_Http.AddEndpoint(m_AdminService);

		if (m_HttpProjectService)
		{
			m_Http.AddEndpoint(*m_HttpProjectService);
		}

		m_Http.AddEndpoint(m_CasService);

		if (m_CacheService)
		{
			spdlog::info("instantiating legacy cache service");
			m_Http.AddEndpoint(*m_CacheService);
		}

		if (m_StructuredCacheService)
		{
			m_Http.AddEndpoint(*m_StructuredCacheService);
		}

		if (m_HttpLaunchService)
		{
			m_Http.AddEndpoint(*m_HttpLaunchService);
		}

		// Experimental
		//
		// m_ZenMesh.Start(1337);
	}

	void Run()
	{
		if (m_Process.IsValid())
		{
			EnqueueTimer();
		}

		if (!m_TestMode)
		{
			spdlog::info("__________                _________ __                        ");
			spdlog::info("\\____    /____   ____    /   _____//  |_  ___________   ____  ");
			spdlog::info("  /     // __ \\ /    \\   \\_____  \\\\   __\\/  _ \\_  __ \\_/ __ \\ ");
			spdlog::info(" /     /\\  ___/|   |  \\  /        \\|  | (  <_> )  | \\/\\  ___/ ");
			spdlog::info("/_______ \\___  >___|  / /_______  /|__|  \\____/|__|    \\___  >");
			spdlog::info("        \\/   \\/     \\/          \\/                         \\/ ");
		}

		spdlog::info(ZEN_APP_NAME " now running");

		m_Http.Run(m_TestMode);

		spdlog::info(ZEN_APP_NAME " exiting");

		m_IoContext.stop();
	}

	void RequestExit(int ExitCode)
	{
		RequestApplicationExit(ExitCode);
		m_Http.RequestExit();
	}

	void Cleanup() { spdlog::info(ZEN_APP_NAME " cleaning up"); }

	void SetTestMode(bool State) { m_TestMode = State; }
	void SetDataRoot(std::filesystem::path Root) { m_DataRoot = Root; }

	void EnqueueTimer()
	{
		m_PidCheckTimer.expires_after(std::chrono::seconds(1));
		m_PidCheckTimer.async_wait([this](const asio::error_code&) { CheckOwnerPid(); });
	}

	void CheckOwnerPid()
	{
		if (m_Process.IsRunning())
		{
			EnqueueTimer();
		}
		else
		{
			spdlog::info(ZEN_APP_NAME " exiting since parent process id {} is gone", m_Process.Pid());

			RequestExit(0);
		}
	}

private:
	bool				  m_TestMode = false;
	std::filesystem::path m_DataRoot;
	asio::io_context	  m_IoContext;
	asio::steady_timer	  m_PidCheckTimer{m_IoContext};
	zen::Process		  m_Process;

	zen::HttpServer									 m_Http;
	std::unique_ptr<zen::CasStore>					 m_CasStore{zen::CreateCasStore()};
	zen::CasGc										 m_Gc{*m_CasStore};
	zen::CasScrubber								 m_Scrubber{*m_CasStore};
	HttpTestService									 m_TestService;
	zen::HttpCasService								 m_CasService{*m_CasStore};
	std::unique_ptr<zen::HttpKvCacheService>		 m_CacheService;
	zen::RefPtr<zen::ProjectStore>					 m_ProjectStore;
	zen::Ref<zen::LocalProjectService>				 m_LocalProjectService;
	std::unique_ptr<zen::HttpLaunchService>			 m_HttpLaunchService;
	std::unique_ptr<zen::HttpProjectService>		 m_HttpProjectService;
	std::unique_ptr<zen::HttpStructuredCacheService> m_StructuredCacheService;
	HttpAdminService								 m_AdminService;
	HttpHealthService								 m_HealthService;
	zen::Mesh										 m_ZenMesh{m_IoContext};
};

int
main(int argc, char* argv[])
{
	mi_version();

	ZenServerOptions GlobalOptions;
	ParseGlobalCliOptions(argc, argv, GlobalOptions);
	InitializeCrashReporting(GlobalOptions.DataDir / "crashdumps");
	InitializeLogging(GlobalOptions);

	spdlog::info("zen cache server starting on port {}", GlobalOptions.BasePort);

	try
	{
		std::unique_ptr<std::thread>	 ShutdownThread;
		std::unique_ptr<zen::NamedEvent> ShutdownEvent;

		ZenServer Cache;
		Cache.SetDataRoot(GlobalOptions.DataDir);
		Cache.SetTestMode(GlobalOptions.IsTest);
		Cache.Initialize(GlobalOptions.BasePort, GlobalOptions.OwnerPid);

		if (!GlobalOptions.ChildId.empty())
		{
			zen::ExtendableStringBuilder<64> ShutdownEventName;
			ShutdownEventName << GlobalOptions.ChildId << "_Shutdown";
			ShutdownEvent.reset(new zen::NamedEvent{ShutdownEventName});

			zen::NamedEvent ParentEvent{GlobalOptions.ChildId};
			ParentEvent.Set();

			ShutdownThread.reset(new std::thread{[&] {
				ShutdownEvent->Wait();
				spdlog::info("shutdown signal received");
				Cache.RequestExit(0);
			}});
		}

		Cache.Run();
		Cache.Cleanup();

		if (ShutdownEvent)
		{
			ShutdownEvent->Set();
			ShutdownThread->join();
		}
	}
	catch (std::exception& e)
	{
		SPDLOG_CRITICAL("Caught exception in main: {}", e.what());
	}

	return 0;
}

