// Zen command line client utility
//

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#undef DOCTEST_CONFIG_IMPLEMENT

#include "zen.h"

#include "chunk/chunk.h"
#include "cmds/copy.h"
#include "cmds/dedup.h"
#include "cmds/deploy.h"
#include "cmds/hash.h"
#include "cmds/run.h"

#include <zencore/scopeguard.h>
#include <zencore/string.h>
#include <zenstore/cas.h>

#if TEST_UWS
#	pragma warning(push)
#	pragma warning(disable : 4458)
#	pragma warning(disable : 4324)
#	pragma warning(disable : 4100)
#	pragma warning(disable : 4706)
#	include <uwebsockets/App.h>
#	pragma warning(pop)

#	pragma comment(lib, "Iphlpapi.lib")
#	pragma comment(lib, "userenv.lib")
#endif

#include <spdlog/spdlog.h>
#include <gsl/gsl-lite.hpp>

#include <mimalloc-new-delete.h>

//////////////////////////////////////////////////////////////////////////

class TemplateCommand : public ZenCmdBase
{
public:
	TemplateCommand() { m_Options.add_options()("r,root", "Root directory for CAS pool", cxxopts::value<std::string>(m_RootDirectory)); }

	virtual int Run(const ZenCliOptions& GlobalOptions, int argc, char** argv) override { ZEN_UNUSED(GlobalOptions, argc, argv); }

	virtual cxxopts::Options* Options() override { return &m_Options; }

private:
	cxxopts::Options m_Options{"template", "EDIT THIS COMMAND DESCRIPTION"};
	std::string		 m_RootDirectory;
};

//////////////////////////////////////////////////////////////////////////

class RunTestsCommand : public ZenCmdBase
{
public:
	virtual int Run(const ZenCliOptions& GlobalOptions, int argc, char** argv) override
	{
		ZEN_UNUSED(GlobalOptions);

		// Set output mode to handle virtual terminal sequences
		HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
		if (hOut == INVALID_HANDLE_VALUE)
			return GetLastError();

		DWORD dwMode = 0;
		if (!GetConsoleMode(hOut, &dwMode))
			return GetLastError();

		dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
		if (!SetConsoleMode(hOut, dwMode))
			return GetLastError();

		return doctest::Context(argc, argv).run();
	}

	virtual cxxopts::Options* Options() override { return &m_Options; }

private:
	cxxopts::Options m_Options{"runtests", "Run tests"};
};

//////////////////////////////////////////////////////////////////////////
// TODO: should make this Unicode-aware so we can pass anything in on the
// command line.

int
main(int argc, char** argv)
{
	mi_version();

#if TEST_UWS
	/* Overly simple hello world app, using multiple threads */
	std::vector<std::thread*> threads(4);

	std::transform(threads.begin(), threads.end(), threads.begin(), [](std::thread* /*t*/) {
		return new std::thread([]() {
			uWS::App()
				.get("/*",
					 [&](uWS::HttpResponse<false>* res, uWS::HttpRequest*) {
						 zen::Sleep(1);
						 res->end("hello, world!");
					 })
				.listen(1337, [&](auto* listen_socket) { ZEN_UNUSED(listen_socket); })
				.run();
		});
	});

	std::for_each(threads.begin(), threads.end(), [](std::thread* t) { t->join(); });
#endif
	//////////////////////////////////////////////////////////////////////////

	auto _ = zen::MakeGuard([] { spdlog::shutdown(); });

	HashCommand		HashCmd;
	CopyCommand		CopyCmd;
	DedupCommand	DedupCmd;
	DeployCommand	DeployCmd;
	ChunkCommand	ChunkCmd;
	RunTestsCommand RunTestsCmd;
	RunCommand		RunCmd;

	const struct CommandInfo
	{
		const char* CmdName;
		ZenCmdBase* Cmd;
	} Commands[] = {
		{"chunk", &ChunkCmd},
		{"copy", &CopyCmd},
		{"deploy", &DeployCmd},
		{"dedup", &DedupCmd},
		{"hash", &HashCmd},
		{"runtests", &RunTestsCmd},
		{"run", &RunCmd},
	};

	// Build set containing available commands

	std::unordered_set<std::string> CommandSet;

	for (const auto& Cmd : Commands)
		CommandSet.insert(Cmd.CmdName);

	// Split command line into options, commands and any pass-through arguments

	std::string				 Passthrough;
	std::vector<std::string> PassthroughV;

	for (int i = 1; i < argc; ++i)
	{
		if (strcmp(argv[i], "--") == 0)
		{
			bool							  IsFirst = true;
			zen::ExtendableStringBuilder<256> Line;

			for (int j = i + 1; j < argc; ++j)
			{
				if (!IsFirst)
				{
					Line.AppendAscii(" ");
				}

				std::string_view ThisArg(argv[j]);
				PassthroughV.push_back(std::string(ThisArg));

				const bool NeedsQuotes = (ThisArg.find(' ') != std::string_view::npos);

				if (NeedsQuotes)
				{
					Line.AppendAscii("\"");
				}

				Line.Append(ThisArg);

				if (NeedsQuotes)
				{
					Line.AppendAscii("\"");
				}

				IsFirst = false;
			}

			Passthrough = Line.c_str();

			// This will "truncate" the arg vector and terminate the loop
			argc = i - 1;
		}
	}

	// Split command line into global vs command options. We do this by simply
	// scanning argv for a string we recognise as a command and split it there

	std::vector<char*> CommandArgVec;
	CommandArgVec.push_back(argv[0]);

	for (int i = 1; i < argc; ++i)
	{
		if (CommandSet.find(argv[i]) != CommandSet.end())
		{
			int commandArgCount = /* exec name */ 1 + argc - (i + 1);
			CommandArgVec.resize(commandArgCount);
			std::copy(argv + i + 1, argv + argc, CommandArgVec.begin() + 1);

			argc = i + 1;

			break;
		}
	}

	// Parse global CLI arguments

	ZenCliOptions GlobalOptions;

	GlobalOptions.PassthroughArgs = Passthrough;
	GlobalOptions.PassthroughV	  = PassthroughV;

	std::string SubCommand = "<None>";

	cxxopts::Options Options("zen", "Zen management tool");
	Options.add_options()("d, debug", "Enable debugging", cxxopts::value<bool>(GlobalOptions.IsDebug));
	Options.add_options()("v, verbose", "Enable verbose logging", cxxopts::value<bool>(GlobalOptions.IsVerbose));
	Options.add_options()("help", "Show command line help");
	Options.add_options()("c, command", "Sub command", cxxopts::value<std::string>(SubCommand));

	Options.parse_positional({"command"});

	const bool IsNullInvoke = (argc == 1);	// If no arguments are passed we want to print usage information

	try
	{
		auto ParseResult = Options.parse(argc, argv);

		if (ParseResult.count("help") || IsNullInvoke == 1)
		{
			std::string Help = Options.help();

			printf("%s\n", Help.c_str());

			printf("available commands:\n");

			for (const auto& CmdInfo : Commands)
			{
				printf("\n-- %s\n%s\n", CmdInfo.CmdName, CmdInfo.Cmd->Options()->help().c_str());
			}

			exit(0);
		}

		for (const CommandInfo& CmdInfo : Commands)
		{
			if (_stricmp(SubCommand.c_str(), CmdInfo.CmdName) == 0)
			{
				cxxopts::Options* VerbOptions = CmdInfo.Cmd->Options();

				try
				{
					return CmdInfo.Cmd->Run(GlobalOptions, (int)CommandArgVec.size(), CommandArgVec.data());
				}
				catch (cxxopts::OptionParseException& Ex)
				{
					if (VerbOptions)
					{
						std::string help = VerbOptions->help();

						printf("Error parsing arguments for command '%s': %s\n\n%s", SubCommand.c_str(), Ex.what(), help.c_str());

						exit(11);
					}
					else
					{
						printf("Error parsing arguments for command '%s': %s\n\n", SubCommand.c_str(), Ex.what());

						exit(11);
					}
				}
			}
		}

		printf("Unknown command specified: '%s', exiting\n", SubCommand.c_str());
	}
	catch (cxxopts::OptionParseException& Ex)
	{
		std::string HelpMessage = Options.help();

		printf("Error parsing snapshot program arguments: %s\n\n%s", Ex.what(), HelpMessage.c_str());

		return 9;
	}
	catch (std::exception& Ex)
	{
		printf("Exception caught from 'main': %s\n", Ex.what());

		return 10;
	}

	return 0;
}
