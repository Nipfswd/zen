// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include "../zen.h"

/** Copy files, possibly using block cloning
 */
class CopyCommand : public ZenCmdBase
{
public:
	CopyCommand();
	~CopyCommand();

	virtual cxxopts::Options* Options() override { return &m_Options; }
	virtual int				  Run(const ZenCliOptions& GlobalOptions, int argc, char** argv) override;

private:
	cxxopts::Options		 m_Options{"copy", "Copy files"};
	std::vector<std::string> m_Positional;
	std::string				 m_CopySource;
	std::string				 m_CopyTarget;
	bool					 m_NoClone = false;
};

