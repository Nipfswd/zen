// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include "../zen.h"

/** Deploy files from Zen build store
 */
class DeployCommand : public ZenCmdBase
{
public:
	DeployCommand();
	~DeployCommand();

	virtual cxxopts::Options* Options() override { return &m_Options; }
	virtual int				  Run(const ZenCliOptions& GlobalOptions, int argc, char** argv) override;

private:
	cxxopts::Options		 m_Options{"deploy", "Deploy cooked data"};
	std::vector<std::string> m_Positional;
	std::string				 m_CopySource;
	std::string				 m_CopyTarget;
	bool					 m_NoClone = false;
	bool					 m_IsClean = false;
};

