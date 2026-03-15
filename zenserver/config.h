// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <filesystem>
#include <string>

struct ZenServerOptions
{
	bool				  IsDebug  = false;
	bool				  IsTest   = false;
	int					  BasePort = 1337;	// Service listen port (used for both UDP and TCP)
	int					  OwnerPid = 0;		// Parent process id (zero for standalone)
	std::string			  ChildId;			// Id assigned by parent process (used for lifetime management)
	std::string			  LogId;			// Id for tagging log output
	std::filesystem::path DataDir;			// Root directory for state (used for testing)
	std::string			  FlockId;			// Id for grouping test instances into sets
};

void ParseGlobalCliOptions(int argc, char* argv[], ZenServerOptions& GlobalOptions);

struct ZenServiceConfig
{
	bool LegacyCacheEnabled		= false;
	bool StructuredCacheEnabled = true;
};

void ParseServiceConfig(const std::filesystem::path& DataRoot, ZenServiceConfig& ServiceConfig);

