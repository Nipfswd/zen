// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include "hash.h"

#include <zencore/blake3.h>
#include <zencore/string.h>
#include <zencore/timer.h>

#include <ppl.h>
#include <spdlog/spdlog.h>

HashCommand::HashCommand()
{
	m_Options.add_options()("d,dir", "Directory to scan", cxxopts::value<std::string>(m_ScanDirectory))(
		"o,output",
		"Output file",
		cxxopts::value<std::string>(m_OutputFile));
}

HashCommand::~HashCommand() = default;

int
HashCommand::Run(const ZenCliOptions& GlobalOptions, int argc, char** argv)
{
	ZEN_UNUSED(GlobalOptions);

	auto result = m_Options.parse(argc, argv);

	bool valid = m_ScanDirectory.length();

	if (!valid)
		throw cxxopts::OptionParseException("Chunk command requires a directory to scan");

	// Gather list of files to process

	spdlog::info("Gathering files from {}", m_ScanDirectory);

	struct FileEntry
	{
		std::filesystem::path FilePath;
		zen::BLAKE3			  FileHash;
	};

	std::vector<FileEntry> FileList;
	uint64_t			   FileBytes = 0;

	std::filesystem::path ScanDirectoryPath{m_ScanDirectory};

	for (const std::filesystem::directory_entry& Entry : std::filesystem::recursive_directory_iterator(ScanDirectoryPath))
	{
		if (Entry.is_regular_file())
		{
			FileList.push_back({Entry.path()});
			FileBytes += Entry.file_size();
		}
	}

	spdlog::info("Gathered {} files, total size {}", FileList.size(), zen::NiceBytes(FileBytes));

	Concurrency::combinable<uint64_t> TotalBytes;

	auto hashFile = [&](FileEntry& File) {
		InternalFile InputFile;
		InputFile.OpenRead(File.FilePath);
		const uint8_t* DataPointer = (const uint8_t*)InputFile.MemoryMapFile();
		const size_t   DataSize	   = InputFile.GetFileSize();

		File.FileHash = zen::BLAKE3::HashMemory(DataPointer, DataSize);

		TotalBytes.local() += DataSize;
	};

	// Process them as quickly as possible

	zen::Stopwatch Timer;

#if 1
	Concurrency::parallel_for_each(begin(FileList), end(FileList), [&](auto& file) { hashFile(file); });
#else
	for (const auto& file : FileList)
	{
		hashFile(file);
	}
#endif

	size_t TotalByteCount = 0;

	TotalBytes.combine_each([&](size_t Total) { TotalByteCount += Total; });

	const uint64_t ElapsedMs = Timer.getElapsedTimeMs();
	spdlog::info("Scanned {} files in {}", FileList.size(), zen::NiceTimeSpanMs(ElapsedMs));
	spdlog::info("Total bytes {} ({})", zen::NiceBytes(TotalByteCount), zen::NiceByteRate(TotalByteCount, ElapsedMs));

	InternalFile Output;

	if (m_OutputFile.empty())
	{
		// TEMPORARY -- should properly open stdout
		Output.OpenWrite("CONOUT$", false);
	}
	else
	{
		Output.OpenWrite(m_OutputFile, true);
	}

	zen::ExtendableStringBuilder<256> Line;

	uint64_t CurrentOffset = 0;

	for (const auto& File : FileList)
	{
		Line.Append(File.FilePath.generic_u8string().c_str());
		Line.Append(',');
		File.FileHash.ToHexString(Line);
		Line.Append('\n');

		Output.Write(Line.Data(), Line.Size(), CurrentOffset);
		CurrentOffset += Line.Size();

		Line.Reset();
	}

	// TODO: implement snapshot enumeration and display
	return 0;
}

