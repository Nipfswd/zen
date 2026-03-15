// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#pragma warning(push)
#pragma warning(disable : 4267)	 // warning C4267: '=': conversion from 'size_t' to 'US', possible loss of data
#include <cxxopts.hpp>
#pragma warning(pop)

#include <zencore/refcount.h>
#include <zencore/windows.h>

#include <atlfile.h>
#include <filesystem>

struct ZenCliOptions
{
	bool IsDebug   = false;
	bool IsVerbose = false;

	// Arguments after " -- " on command line are passed through and not parsed
	std::string				 PassthroughArgs;
	std::vector<std::string> PassthroughV;
};

class ZenCmdBase
{
public:
	virtual int				  Run(const ZenCliOptions& GlobalOptions, int argc, char** argv) = 0;
	virtual cxxopts::Options* Options()														 = 0;
};

