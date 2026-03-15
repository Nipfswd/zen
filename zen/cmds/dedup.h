// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include "../zen.h"

#include <ppl.h>

/** Deduplicate files in a tree using block cloning
 */
class DedupCommand : public ZenCmdBase
{
public:
	DedupCommand();
	~DedupCommand();

	virtual cxxopts::Options* Options() override { return &m_Options; }
	virtual int				  Run(const ZenCliOptions& GlobalOptions, int argc, char** argv) override;

private:
	cxxopts::Options		 m_Options{"dedup", "Deduplicate files"};
	std::vector<std::string> m_Positional;
	std::string				 m_DedupSource;
	std::string				 m_DedupTarget;
	size_t					 m_SizeThreshold = 1024 * 1024;
};

