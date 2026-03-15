// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include "../internalfile.h"
#include "../zen.h"

#include <ppl.h>

/** Generate hash list file
 */
class HashCommand : public ZenCmdBase
{
public:
	HashCommand();
	~HashCommand();

	virtual int				  Run(const ZenCliOptions& GlobalOptions, int argc, char** argv) override;
	virtual cxxopts::Options* Options() override { return &m_Options; }

private:
	cxxopts::Options m_Options{"hash", "Hash files"};
	std::string		 m_ScanDirectory;
	std::string		 m_OutputFile;
};

