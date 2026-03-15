// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <zencore/iohash.h>
#include <zencore/zencore.h>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace zen {

struct LeafNode
{
	uint64_t	 FileSize		  = 0;
	uint64_t	 FileModifiedTime = 0;
	zen::IoHash	 ChunkHash		  = zen::IoHash::Zero;
	std::wstring Name;
};

struct TreeNode
{
	std::vector<TreeNode> Children;
	std::vector<LeafNode> Leaves;
	std::wstring		  Name;
	zen::BLAKE3			  ChunkHash = zen::BLAKE3::Zero;

	ZENCORE_API void VisitModifyFiles(std::function<void(LeafNode& node)> func);
	ZENCORE_API void VisitFiles(std::function<void(const LeafNode& node)> func);
	ZENCORE_API void Finalize();
};

struct SnapshotManifest
{
	std::string Id;
	TreeNode	Root;
	zen::BLAKE3 ChunkHash = zen::BLAKE3::Zero;

	ZENCORE_API void finalize();
};

class InStream;
class OutStream;

ZENCORE_API void ReadManifest(SnapshotManifest& Manifest, InStream& FromStream);
ZENCORE_API void WriteManifest(const SnapshotManifest& Manifest, OutStream& ToStream);
ZENCORE_API void PrintManifest(const SnapshotManifest& Manifest, OutStream& ToStream);

// Translate a user-provided manifest specification into a file path.
// Supports hashtag syntax to implicitly refer to user documents zenfs folder
ZENCORE_API std::filesystem::path ManifestSpecToPath(const char* ManifestSpec);

void snapshotmanifest_forcelink();

}  // namespace zen

