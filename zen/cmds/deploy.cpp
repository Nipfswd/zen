// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include "deploy.h"

#include <zencore/string.h>

#include <spdlog/spdlog.h>

DeployCommand::DeployCommand()
{
	m_Options.add_options()("h,help", "Print help");
	m_Options.add_options()("no-clone", "Do not perform block clone", cxxopts::value(m_NoClone)->default_value("false"));
	m_Options.add_options()("clean",
							"Make clean deploy (i.e remove anything in target first)",
							cxxopts::value(m_IsClean)->default_value("false"));
	m_Options.add_option("", "s", "source", "Deploy source", cxxopts::value(m_CopySource), "<build store>");
	m_Options.add_option("", "t", "target", "Deploy target", cxxopts::value(m_CopyTarget), "<directory>");
	m_Options.add_option("", "", "positional", "Positional arguments", cxxopts::value(m_Positional), "");
}

DeployCommand::~DeployCommand() = default;

int
DeployCommand::Run(const ZenCliOptions& GlobalOptions, int argc, char** argv)
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

	std::filesystem::path ToPath;

	ToPath = m_CopyTarget;

	const bool IsTargetDir = std::filesystem::is_directory(ToPath);
	bool	   IsTargetNew = !std::filesystem::exists(ToPath);

	if (!IsTargetNew && !IsTargetDir)
	{
		throw std::exception("Invalid target specification (needs to be a directory)");
	}

	zen::ExtendableStringBuilder<128> Path8;
	zen::WideToUtf8(ToPath.c_str(), Path8);

	if (IsTargetNew == false && m_IsClean)
	{
		spdlog::info("Clean deploy -- deleting directory {}", Path8.c_str());

		std::filesystem::remove_all(ToPath);

		IsTargetNew = true;	 // Create fresh new directory
	}

	if (IsTargetNew)
	{
		spdlog::info("Creating directory {}", Path8.c_str());

		std::filesystem::create_directories(ToPath);
	}

	spdlog::info("Starting deploy operation...");

	// TODO: implement!

	return 0;
}

