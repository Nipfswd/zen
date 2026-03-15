// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once
#include <zencore/zencore.h>
#include "../zen.h"

class ChunkCommand : public ZenCmdBase
{
public:
	ChunkCommand();
	~ChunkCommand();

	virtual int				  Run(const ZenCliOptions& GlobalOptions, int argc, char** argv) override;
	virtual cxxopts::Options* Options() override { return &m_Options; }

private:
	cxxopts::Options m_Options{"chunk", "Do a chunking pass"};
	std::string		 m_RootDirectory;
	std::string		 m_ScanDirectory;
	size_t			 m_ChunkSize		= 0;
	size_t			 m_AverageChunkSize = 0;
	bool			 m_UseCompression	= true;
};

