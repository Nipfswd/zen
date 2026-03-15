// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include "copy.h"

#include <zencore/filesystem.h>
#include <zencore/string.h>
#include <zencore/timer.h>

#include <spdlog/spdlog.h>

CopyCommand::CopyCommand()
{
	m_Options.add_options()("h,help", "Print help");
	m_Options.add_options()("no-clone", "Do not perform block clone", cxxopts::value(m_NoClone)->default_value("false"));
	m_Options.add_option("", "s", "source", "Copy source", cxxopts::value(m_CopySource), "<file/directory>");
	m_Options.add_option("", "t", "target", "Copy target", cxxopts::value(m_CopyTarget), "<file/directory>");
	m_Options.add_option("", "", "positional", "Positional arguments", cxxopts::value(m_Positional), "");
}

CopyCommand::~CopyCommand() = default;

int
CopyCommand::Run(const ZenCliOptions& GlobalOptions, int argc, char** argv)
{
	ZEN_UNUSED(GlobalOptions);

	m_Options.parse_positional({"source", "target", "positional"});

	auto result = m_Options.parse(argc, argv);

	if (result.count("help"))
	{
		std::cout << m_Options.help({"", "Group"}) << std::endl;

		return 0;
	}

	// Validate arguments

	if (m_CopySource.empty())
		throw std::exception("No source specified");

	if (m_CopyTarget.empty())
		throw std::exception("No target specified");

	std::filesystem::path FromPath;
	std::filesystem::path ToPath;

	FromPath = m_CopySource;
	ToPath	 = m_CopyTarget;

	const bool IsFileCopy = std::filesystem::is_regular_file(m_CopySource);
	const bool IsDirCopy  = std::filesystem::is_directory(m_CopySource);

	if (!IsFileCopy && !IsDirCopy)
	{
		throw std::exception("Invalid source specification (neither directory nor file)");
	}

	if (IsFileCopy && IsDirCopy)
	{
		throw std::exception("Invalid source specification (both directory AND file!?)");
	}

	if (IsDirCopy)
	{
		if (std::filesystem::exists(ToPath))
		{
			const bool IsTargetDir = std::filesystem::is_directory(ToPath);
			if (!IsTargetDir)
			{
				if (std::filesystem::is_regular_file(ToPath))
				{
					throw std::exception("Attempted copy of directory into file");
				}
			}
		}
		else
		{
			std::filesystem::create_directories(ToPath);
		}
	}
	else
	{
		// Single file copy

		zen::Stopwatch Timer;

		zen::CopyFileOptions CopyOptions;
		CopyOptions.EnableClone = !m_NoClone;
		zen::CopyFile(FromPath, ToPath, CopyOptions);

		spdlog::info("Copy completed in {}", zen::NiceTimeSpanMs(Timer.getElapsedTimeMs()));
	}

	return 0;
}

