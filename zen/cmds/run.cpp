// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#define _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS	 // for <cstdbool> include warning triggered by cpr

#include "run.h"

#include <zencore/compactbinarybuilder.h>
#include <zencore/except.h>
#include <zencore/filesystem.h>
#include <zencore/fmtutils.h>
#include <zencore/iohash.h>
#include <zencore/string.h>
#include <zencore/timer.h>
#include <zenserverprocess.h>

#include <spdlog/spdlog.h>
#include <filesystem>

// cpr ////////////////////////////////////////////////////////////////////
//
// For some reason, these don't seem to stick, so we disable the warnings
//#	define _SILENCE_CXX17_C_HEADER_DEPRECATION_WARNING 1
//#	define _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS		1
#pragma warning(push)
#pragma warning(disable : 4004)
#pragma warning(disable : 4996)
#include <cpr/cpr.h>
#pragma warning(pop)

#if ZEN_PLATFORM_WINDOWS
#	pragma comment(lib, "Crypt32.lib")
#	pragma comment(lib, "Wldap32.lib")
#	pragma comment(lib, "Ws2_32.lib")
#endif

//////////////////////////////////////////////////////////////////////////

using namespace std::literals;

RunCommand::RunCommand()
{
	m_Options.add_options()("h,host", "Host to run on", cxxopts::value<std::string>(m_TargetHost))("d,dir",
																								   "Tree to run",
																								   cxxopts::value<std::string>(m_ExeTree));
}

RunCommand::~RunCommand() = default;

void
CreateTreeManifest(std::filesystem::path RootPath)
{
}

int
RunCommand::Run(const ZenCliOptions& GlobalOptions, int argc, char** argv)
{
	if (GlobalOptions.PassthroughV.empty())
	{
		throw cxxopts::OptionParseException("run command requires a command to run!");
	}

	ZenTestEnvironment	  TestEnv;
	std::filesystem::path ProgramBaseDir = std::filesystem::path(argv[0]).parent_path();
	std::filesystem::path TestBaseDir	 = ProgramBaseDir.parent_path().parent_path() / ".test";
	TestEnv.Initialize(ProgramBaseDir, TestBaseDir);

	std::filesystem::path TestDir = TestEnv.CreateNewTestDir();

	ZenServerInstance Zen1(TestEnv);
	Zen1.SetTestDir(TestDir);
	Zen1.SpawnServer(13337);

	auto result = m_Options.parse(argc, argv);

	std::filesystem::path TreePath{m_ExeTree};

	struct Visitor : public zen::FileSystemTraversal::TreeVisitor
	{
		const std::filesystem::path& m_RootPath;

		Visitor(const std::filesystem::path& RootPath) : m_RootPath(RootPath) {}

		virtual void VisitFile(const std::filesystem::path& Parent, const std::wstring_view& FileName, uint64_t FileSize) override
		{
			std::filesystem::path FullPath = Parent / FileName;

			zen::IoHashStream Ios;
			zen::ScanFile(FullPath, 64 * 1024, [&](const void* Data, size_t Size) { Ios.Append(Data, Size); });
			zen::IoHash Hash = Ios.GetHash();

			std::wstring RelativePath = FullPath.lexically_relative(m_RootPath).native();
			//			spdlog::info("File: {:32} => {} ({})", zen::WideToUtf8(RelativePath), Hash, FileSize);

			FileEntry& Entry = m_Files[RelativePath];
			Entry.Hash		 = Hash;
			Entry.Size		 = FileSize;

			m_HashToFile[Hash] = FullPath;
		}

		virtual bool VisitDirectory(const std::filesystem::path& Parent, const std::wstring_view& DirectoryName) override
		{
			std::filesystem::path FullPath = Parent / DirectoryName;

			if (DirectoryName.starts_with(L"."))
			{
				return false;
			}

			return true;
		}

		struct FileEntry
		{
			uint64_t	Size;
			zen::IoHash Hash;
		};

		std::map<std::wstring, FileEntry>											m_Files;
		std::unordered_map<zen::IoHash, std::filesystem::path, zen::IoHash::Hasher> m_HashToFile;
	};

	zen::FileSystemTraversal Traversal;
	Visitor					 Visit(TreePath);
	Traversal.TraverseFileSystem(TreePath, Visit);

	zen::CbObjectWriter PrepReq;
	PrepReq << "cmd" << GlobalOptions.PassthroughV[0];
	PrepReq << "args" << GlobalOptions.PassthroughArgs;
	PrepReq.BeginArray("files");

	for (const auto& Kv : Visit.m_Files)
	{
		PrepReq.BeginObject();
		PrepReq << "file" << zen::WideToUtf8(Kv.first) << "size" << Kv.second.Size << "hash" << Kv.second.Hash;
		PrepReq.EndObject();
	}
	PrepReq.EndArray();

	zen::MemoryOutStream MemOut;
	zen::BinaryWriter	 MemWriter(MemOut);
	PrepReq.Save(MemWriter);

	Zen1.WaitUntilReady();

	cpr::Response Response =
		cpr::Post(cpr::Url("http://localhost:13337/exec/jobs/prep"), cpr::Body((const char*)MemOut.Data(), MemOut.Size()));

	if (Response.status_code < 300)
	{
		zen::IoBuffer Payload(zen::IoBuffer::Clone, Response.text.data(), Response.text.size());
		zen::CbObject Result = zen::LoadCompactBinaryObject(Payload);

		for (auto& Need : Result["need"])
		{
			zen::IoHash NeedHash = Need.AsHash();

			if (auto It = Visit.m_HashToFile.find(NeedHash); It != Visit.m_HashToFile.end())
			{
				zen::IoBuffer FileData = zen::IoBufferBuilder::MakeFromFile(It->second.c_str());

				cpr::Response CasResponse =
					cpr::Post(cpr::Url("http://localhost:13337/cas"), cpr::Body((const char*)FileData.Data(), FileData.Size()));

				if (CasResponse.status_code >= 300)
				{
					spdlog::error("CAS put failed with {}", CasResponse.status_code);
				}
			}
			else
			{
				spdlog::error("unknown hash in 'need' list: {}", NeedHash);
			}
		}
	}

	cpr::Response JobResponse =
		cpr::Post(cpr::Url("http://localhost:13337/exec/jobs"), cpr::Body((const char*)MemOut.Data(), MemOut.Size()));

	spdlog::info("job exec: {}", JobResponse.status_code);

	return 0;
}

