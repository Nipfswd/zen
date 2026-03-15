// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include "dedup.h"

#include <zencore/blake3.h>
#include <zencore/filesystem.h>
#include <zencore/iobuffer.h>
#include <zencore/string.h>
#include <zencore/thread.h>
#include <zencore/timer.h>

#include <ppl.h>
#include <spdlog/spdlog.h>

DedupCommand::DedupCommand()
{
	m_Options.add_options()("h,help", "Print help");
	m_Options.add_options()("size", "Configure size threshold for dedup", cxxopts::value(m_SizeThreshold)->default_value("131072"));
	m_Options.add_option("", "s", "source", "Copy source", cxxopts::value(m_DedupSource), "<file/directory>");
	m_Options.add_option("", "t", "target", "Copy target", cxxopts::value(m_DedupTarget), "<file/directory>");
	m_Options.add_option("", "", "positional", "Positional arguments", cxxopts::value(m_Positional), "");
}

DedupCommand::~DedupCommand() = default;

int
DedupCommand::Run(const ZenCliOptions& GlobalOptions, int argc, char** argv)
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

	const bool SourceGood = zen::SupportsBlockRefCounting(m_DedupSource);
	const bool TargetGood = zen::SupportsBlockRefCounting(m_DedupTarget);

	if (!SourceGood)
	{
		spdlog::info("Source directory '{}' does not support deduplication", m_DedupSource);

		return 0;
	}

	if (!TargetGood)
	{
		spdlog::info("Target directory '{}' does not support deduplication", m_DedupTarget);

		return 0;
	}

	spdlog::info("Performing dedup operation between {} and {}, size threshold {}",
				 m_DedupSource,
				 m_DedupTarget,
				 zen::NiceBytes(m_SizeThreshold));

	using DirEntryList_t = std::list<std::filesystem::directory_entry>;

	zen::RwLock								   MapLock;
	std::unordered_map<size_t, DirEntryList_t> FileSizeMap;
	size_t									   CandidateCount = 0;

	auto AddToList = [&](const std::filesystem::directory_entry& Entry) {
		if (Entry.is_regular_file())
		{
			uintmax_t FileSize = Entry.file_size();
			if (FileSize > m_SizeThreshold)
			{
				zen::RwLock::ExclusiveLockScope _(MapLock);
				FileSizeMap[FileSize].push_back(Entry);
				++CandidateCount;
			}
		}
	};

	std::filesystem::recursive_directory_iterator DirEnd;

	struct Utf8Helper
	{
		zen::ExtendableStringBuilder<128> Path8;

		Utf8Helper(const wchar_t* Path) { zen::WideToUtf8(Path, Path8); };

		std::string_view c_str() { return std::string_view(Path8); };
	};

	spdlog::info("Gathering file info from source: '{}'", m_DedupSource);
	spdlog::info("Gathering file info from target: '{}'", m_DedupTarget);

	{
		zen::Stopwatch Timer;

		Concurrency::parallel_invoke(
			[&] {
				for (std::filesystem::recursive_directory_iterator DirIt1(m_DedupSource); DirIt1 != DirEnd; ++DirIt1)
				{
					AddToList(*DirIt1);
				}
			},
			[&] {
				for (std::filesystem::recursive_directory_iterator DirIt2(m_DedupTarget); DirIt2 != DirEnd; ++DirIt2)
				{
					AddToList(*DirIt2);
				}
			});

		spdlog::info("Gathered {} candidates across {} size buckets. Elapsed: {}",
					 CandidateCount,
					 FileSizeMap.size(),
					 zen::NiceTimeSpanMs(Timer.getElapsedTimeMs()));
	}

	spdlog::info("Sorting buckets by size");

	zen::Stopwatch Timer;

	uint64_t DupeBytes = 0;

	struct SizeList
	{
		size_t			Size;
		DirEntryList_t* DirEntries;
	};

	std::vector<SizeList> SizeLists{FileSizeMap.size()};

	{
		int i = 0;

		for (auto& kv : FileSizeMap)
		{
			ZEN_ASSERT(kv.first >= m_SizeThreshold);
			SizeLists[i].Size		= kv.first;
			SizeLists[i].DirEntries = &kv.second;
			++i;
		}
	}

	std::sort(begin(SizeLists), end(SizeLists), [](const SizeList& Lhs, const SizeList& Rhs) { return Lhs.Size > Rhs.Size; });

	spdlog::info("Bucket summary:");

	std::vector<size_t> BucketId;
	std::vector<size_t> BucketOffsets;
	std::vector<size_t> BucketSizes;
	std::vector<size_t> BucketFileCounts;

	size_t TotalFileSizes = 0;
	size_t TotalFileCount = 0;

	{
		size_t CurrentPow2	   = 0;
		size_t BucketSize	   = 0;
		size_t BucketFileCount = 0;
		bool   FirstBucket	   = true;

		for (int i = 0; i < SizeLists.size(); ++i)
		{
			const size_t ThisSize = SizeLists[i].Size;
			const size_t Pow2	  = zen::NextPow2(ThisSize);

			if (CurrentPow2 != Pow2)
			{
				CurrentPow2 = Pow2;

				if (!FirstBucket)
				{
					BucketSizes.push_back(BucketSize);
					BucketFileCounts.push_back(BucketFileCount);
				}

				BucketId.push_back(Pow2);
				BucketOffsets.push_back(i);

				FirstBucket		= false;
				BucketSize		= 0;
				BucketFileCount = 0;
			}

			BucketSize += ThisSize;
			TotalFileSizes += ThisSize;
			BucketFileCount += SizeLists[i].DirEntries->size();
			TotalFileCount += SizeLists[i].DirEntries->size();
		}

		if (!FirstBucket)
		{
			BucketSizes.push_back(BucketSize);
			BucketFileCounts.push_back(BucketFileCount);
		}

		ZEN_ASSERT(BucketOffsets.size() == BucketSizes.size());
		ZEN_ASSERT(BucketOffsets.size() == BucketFileCounts.size());
	}

	for (int i = 0; i < BucketOffsets.size(); ++i)
	{
		spdlog::info("  Bucket {} : {}, {} candidates", zen::NiceBytes(BucketId[i]), zen::NiceBytes(BucketSizes[i]), BucketFileCounts[i]);
	}

	spdlog::info("Total : {}, {} candidates", zen::NiceBytes(TotalFileSizes), TotalFileCount);

	std::string CurrentNice;

	for (SizeList& Size : SizeLists)
	{
		std::string CurNice{zen::NiceBytes(zen::NextPow2(Size.Size))};

		if (CurNice != CurrentNice)
		{
			CurrentNice = CurNice;
			spdlog::info("Now scanning bucket: {}", CurrentNice);
		}

		std::unordered_map<zen::BLAKE3, const std::filesystem::directory_entry*, zen::BLAKE3::Hasher> DedupMap;

		for (const auto& Entry : *Size.DirEntries)
		{
			zen::BLAKE3 Hash;

			if constexpr (true)
			{
				zen::BLAKE3Stream b3s;

				zen::ScanFile(Entry.path(), 64 * 1024, [&](const void* Data, size_t Size) { b3s.Append(Data, Size); });

				Hash = b3s.GetHash();
			}
			else
			{
				zen::FileContents Contents = zen::ReadFile(Entry.path());

				zen::BLAKE3Stream b3s;

				for (zen::IoBuffer& Buffer : Contents.Data)
				{
					b3s.Append(Buffer.Data(), Buffer.Size());
				}
				Hash = b3s.GetHash();
			}

			if (const std::filesystem::directory_entry* Dupe = DedupMap[Hash])
			{
				std::wstring FileA = Dupe->path().c_str();
				std::wstring FileB = Entry.path().c_str();

				size_t MinLen = std::min(FileA.size(), FileB.size());
				auto   Its	  = std::mismatch(FileB.rbegin(), FileB.rbegin() + MinLen, FileA.rbegin());

				if (Its.first != FileB.rbegin())
				{
					if (Its.first[-1] == '\\' || Its.first[-1] == '/')
						--Its.first;

					FileB = std::wstring(FileB.begin(), Its.first.base()) + L"...";
				}

				spdlog::info("{} {} <-> {}",
							 zen::NiceBytes(Entry.file_size()).c_str(),
							 Utf8Helper(FileA.c_str()).c_str(),
							 Utf8Helper(FileB.c_str()).c_str());

				zen::CopyFileOptions Options;
				Options.EnableClone = true;
				Options.MustClone	= true;

				zen::CopyFile(Dupe->path(), Entry.path(), Options);

				DupeBytes += Entry.file_size();
			}
			else
			{
				DedupMap[Hash] = &Entry;
			}
		}

		Size.DirEntries->clear();
	}

	spdlog::info("Elapsed: {} Deduped: {}", zen::NiceTimeSpanMs(Timer.getElapsedTimeMs()), zen::NiceBytes(DupeBytes));

	return 0;
}

