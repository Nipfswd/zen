// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include "../internalfile.h"
#include "../zen.h"

#include <ppl.h>

/** Execute a command (using Zen)
 */
class RunCommand : public ZenCmdBase
{
public:
	RunCommand();
	~RunCommand();

	virtual int				  Run(const ZenCliOptions& GlobalOptions, int argc, char** argv) override;
	virtual cxxopts::Options* Options() override { return &m_Options; }

private:
	cxxopts::Options m_Options{"run", "Run command"};
	std::string		 m_TargetHost;
	std::string		 m_ExeTree;
};

