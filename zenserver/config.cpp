// Copyright Noah Games, Inc. All Rights Reserved.

#include "config.h"

#include "diag/logging.h"

#include <zencore/fmtutils.h>
#include <zencore/iobuffer.h>
#include <zencore/string.h>

#pragma warning(push)
#pragma warning(disable : 4267)	 // warning C4267: '=': conversion from 'size_t' to 'US', possible loss of data
#include <cxxopts.hpp>
#pragma warning(pop)

#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <sol/sol.hpp>

#if ZEN_PLATFORM_WINDOWS

// Used for getting My Documents for default data directory
#	include <ShlObj.h>
#	pragma comment(lib, "shell32.lib")

std::filesystem::path
PickDefaultStateDirectory()
{
	// Pick sensible default

	WCHAR	myDocumentsDir[MAX_PATH];
	HRESULT hRes = SHGetFolderPathW(NULL,
									CSIDL_PERSONAL /*  My Documents */,
									NULL,
									SHGFP_TYPE_CURRENT,
									/* out */ myDocumentsDir);

	if (SUCCEEDED(hRes))
	{
		wcscat_s(myDocumentsDir, L"\\zen");

		return myDocumentsDir;
	}

	return L"";
}

#else

std::filesystem::path
PickDefaultStateDirectory()
{
	return std::filesystem::path("~/.zen");
}

#endif

void
ParseGlobalCliOptions(int argc, char* argv[], ZenServerOptions& GlobalOptions)
{
	cxxopts::Options options("zenserver", "Zen Server");
	options.add_options()("d, debug", "Enable debugging", cxxopts::value<bool>(GlobalOptions.IsDebug)->default_value("false"));
	options.add_options()("help", "Show command line help");
	options.add_options()("t, test", "Enable test mode", cxxopts::value<bool>(GlobalOptions.IsTest)->default_value("false"));
	options.add_options()("log-id", "Specify id for adding context to log output", cxxopts::value<std::string>(GlobalOptions.LogId));
	options.add_options()("data-dir", "Specify persistence root", cxxopts::value<std::filesystem::path>(GlobalOptions.DataDir));

	options
		.add_option("lifetime", "", "owner-pid", "Specify owning process id", cxxopts::value<int>(GlobalOptions.OwnerPid), "<identifier>");
	options.add_option("lifetime",
					   "",
					   "child-id",
					   "Specify id which can be used to signal parent",
					   cxxopts::value<std::string>(GlobalOptions.ChildId),
					   "<identifier>");

	options.add_option("network",
					   "p",
					   "port",
					   "Select HTTP port",
					   cxxopts::value<int>(GlobalOptions.BasePort)->default_value("1337"),
					   "<port number>");

	try
	{
		auto result = options.parse(argc, argv);

		if (result.count("help"))
		{
			ConsoleLog().info("{}", options.help());

			exit(0);
		}
	}
	catch (cxxopts::OptionParseException& e)
	{
		ConsoleLog().error("Error parsing zenserver arguments: {}\n\n{}", e.what(), options.help());

		throw;
	}

	if (GlobalOptions.DataDir.empty())
	{
		GlobalOptions.DataDir = PickDefaultStateDirectory();
	}
}

void
ParseServiceConfig(const std::filesystem::path& DataRoot, ZenServiceConfig& ServiceConfig)
{
	using namespace fmt::literals;

	std::filesystem::path ConfigScript = DataRoot / "zen_cfg.lua";
	zen::IoBuffer		  LuaScript	   = zen::IoBufferBuilder::MakeFromFile(ConfigScript.native().c_str());

	if (LuaScript)
	{
		sol::state lua;

		// Provide some context to help derive defaults
		lua.set("dataroot", DataRoot.native());

		lua.open_libraries(sol::lib::base);

		// We probably want to limit the scope of this so the script won't see
		// any more than it needs to

		lua.set_function("getenv", [&](const std::string env) -> sol::object {
			std::wstring EnvVarValue;
			size_t		 RequiredSize = 0;
			std::wstring EnvWide	  = zen::Utf8ToWide(env);
			_wgetenv_s(&RequiredSize, nullptr, 0, EnvWide.c_str());

			if (RequiredSize == 0)
				return sol::make_object(lua, sol::lua_nil);

			EnvVarValue.resize(RequiredSize);
			_wgetenv_s(&RequiredSize, EnvVarValue.data(), RequiredSize, EnvWide.c_str());
			return sol::make_object(lua, zen::WideToUtf8(EnvVarValue.c_str()));
		});

		try
		{
			sol::load_result config = lua.load(std::string_view((const char*)LuaScript.Data(), LuaScript.Size()), "zencfg");
			config();
		}
		catch (std::exception& e)
		{
			spdlog::error("config script failure: {}", e.what());

			throw std::exception("fatal zen global config script ({}) failure: {}"_format(ConfigScript, e.what()).c_str());
		}
		ServiceConfig.LegacyCacheEnabled	 = lua["legacycache"]["enable"];
		const std::string path				 = lua["legacycache"]["readpath"];
		ServiceConfig.StructuredCacheEnabled = lua["structuredcache"]["enable"];
	}
}

