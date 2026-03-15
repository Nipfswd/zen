// Copyright Noah Games, Inc. All Rights Reserved.

#include <doctest/doctest.h>
#include <zencore/snapshot_manifest.h>
#include <zencore/stream.h>
#include <zencore/streamutil.h>
#include <zencore/string.h>
#include <ostream>

#include <filesystem>

#include <atlbase.h>

// Used for getting My Documents for default snapshot dir
#include <ShlObj.h>
#pragma comment(lib, "shell32.lib")

namespace zen {

constexpr const char* magicString = "-=- ZEN_SNAP -=-";

struct SerializedManifestHeader
{
	char Magic[16];

	void init() { memcpy(Magic, magicString, sizeof Magic); }
	bool verify() const { return memcmp(Magic, magicString, sizeof Magic) == 0; }
};

TextWriter&
operator<<(TextWriter& Writer, const LeafNode& Leaf)
{
	Writer << "modTime: " << Leaf.FileModifiedTime << ", size: " << Leaf.FileSize << ", hash: " << Leaf.ChunkHash << ", name: " << Leaf.Name
		   << "\n";

	return Writer;
}

BinaryWriter&
operator<<(BinaryWriter& Writer, const LeafNode& Leaf)
{
	Writer << Leaf.FileModifiedTime << Leaf.FileSize << Leaf.ChunkHash << Leaf.Name;

	return Writer;
}

BinaryReader&
operator>>(BinaryReader& Reader, LeafNode& Leaf)
{
	Reader >> Leaf.FileModifiedTime >> Leaf.FileSize >> Leaf.ChunkHash >> Leaf.Name;

	return Reader;
}

void
TreeNode::Finalize()
{
	zen::BLAKE3Stream Blake3Stream;

	for (auto& Node : Children)
	{
		Node.Finalize();
		Blake3Stream.Append(Node.ChunkHash.Hash, sizeof Node.ChunkHash);
		Blake3Stream.Append(Node.Name.data(), Node.Name.size() + 1);
	}

	for (auto& leaf : Leaves)
	{
		Blake3Stream.Append(leaf.ChunkHash.Hash, sizeof leaf.ChunkHash);
		Blake3Stream.Append(leaf.Name.data(), leaf.Name.size() + 1);
	}

	this->ChunkHash = Blake3Stream.GetHash();
}

void
TreeNode::VisitFiles(std::function<void(const LeafNode& node)> func)
{
	for (auto& Node : Children)
		Node.VisitFiles(func);

	for (auto& Leaf : Leaves)
		func(Leaf);
}

void
TreeNode::VisitModifyFiles(std::function<void(LeafNode& node)> func)
{
	for (auto& Node : Children)
		Node.VisitModifyFiles(func);

	for (auto& Leaf : Leaves)
		func(Leaf);
}

IndentTextWriter&
operator<<(IndentTextWriter& Writer, const TreeNode& Node)
{
	Writer << "hash: " << Node.ChunkHash << ", name: " << Node.Name << "\n";

	if (!Node.Leaves.empty())
	{
		Writer << "files: "
			   << "\n";

		IndentTextWriter::Scope _(Writer);

		for (const LeafNode& Leaf : Node.Leaves)
			Writer << Leaf;
	}

	if (!Node.Children.empty())
	{
		Writer << "children: "
			   << "\n";

		IndentTextWriter::Scope _(Writer);

		for (const TreeNode& Child : Node.Children)
		{
			Writer << Child;
		}
	}

	return Writer;
}

BinaryWriter&
operator<<(BinaryWriter& Writer, const TreeNode& Node)
{
	Writer << Node.ChunkHash << Node.Name;
	Writer << uint32_t(Node.Children.size());

	for (const TreeNode& child : Node.Children)
		Writer << child;

	Writer << uint32_t(Node.Leaves.size());

	for (const LeafNode& Leaf : Node.Leaves)
		Writer << Leaf;

	return Writer;
}

BinaryReader&
operator>>(BinaryReader& Reader, TreeNode& Node)
{
	Reader >> Node.ChunkHash >> Node.Name;

	uint32_t ChildCount = 0;
	Reader >> ChildCount;
	Node.Children.resize(ChildCount);

	for (TreeNode& Child : Node.Children)
		Reader >> Child;

	uint32_t LeafCount = 0;
	Reader >> LeafCount;
	Node.Leaves.resize(LeafCount);

	for (LeafNode& Leaf : Node.Leaves)
		Reader >> Leaf;

	return Reader;
}

void
SnapshotManifest::finalize()
{
	Root.Finalize();

	zen::BLAKE3Stream Blake3Stream;

	Blake3Stream.Append(Root.ChunkHash.Hash, sizeof Root.ChunkHash);
	Blake3Stream.Append(Root.Name.data(), Root.Name.size() + 1);

	this->ChunkHash = Blake3Stream.GetHash();
}

void
WriteManifest(const SnapshotManifest& Manifest, OutStream& ToStream)
{
	BinaryWriter			 Out(ToStream);
	SerializedManifestHeader Header;
	Header.init();
	Out.Write(&Header, sizeof Header);

	Out << Manifest.ChunkHash << Manifest.Id << Manifest.Root;
}

void
ReadManifest(SnapshotManifest& Manifest, InStream& FromStream)
{
	BinaryReader			 Reader(FromStream);
	SerializedManifestHeader Header;
	Reader.Read(&Header, sizeof Header);

	Reader >> Manifest.ChunkHash >> Manifest.Id >> Manifest.Root;
}

void
PrintManifest(const SnapshotManifest& Manifest, OutStream& ToStream)
{
	IndentTextWriter Writer(ToStream);

	Writer << "hash: " << Manifest.ChunkHash << "\n";
	Writer << "id:   " << Manifest.Id << "\n";
	Writer << "root: "
		   << "\n";
	IndentTextWriter::Scope _(Writer);
	Writer << Manifest.Root;
}

std::filesystem::path
ManifestSpecToPath(const char* ManifestSpec)
{
	ExtendableWideStringBuilder<128> ManifestTargetFile;

	if (ManifestSpec[0] == '#')
	{
		// Pick sensible default

		WCHAR	MyDocumentsDir[MAX_PATH];
		HRESULT hRes = SHGetFolderPathW(NULL,
										CSIDL_PERSONAL /*  My Documents */,
										NULL,
										SHGFP_TYPE_CURRENT,
										/* out */ MyDocumentsDir);

		if (SUCCEEDED(hRes))
		{
			wcscat_s(MyDocumentsDir, L"\\zenfs\\Snapshots\\");

			ManifestTargetFile.Append(MyDocumentsDir);
			ManifestTargetFile.AppendAscii(ManifestSpec + 1);
		}
	}
	else
	{
		ManifestTargetFile.AppendAscii(ManifestSpec);
	}

	std::filesystem::path ManifestPath{ManifestTargetFile.c_str()};

	if (ManifestPath.extension() != L".zenfs")
	{
		ManifestPath.append(L".zenfs");
	}

	return ManifestPath;
}

//////////////////////////////////////////////////////////////////////////
//
// Testing related code follows...
//

void
snapshotmanifest_forcelink()
{
}

TEST_CASE("Snapshot manifest")
{
	SnapshotManifest Manifest;

	Manifest.Id		   = "test_manifest";
	Manifest.ChunkHash = zen::BLAKE3::HashMemory("abcd", 4);

	MemoryOutStream Outstream;
	WriteManifest(Manifest, Outstream);

	MemoryInStream	 Instream(Outstream.Data(), Outstream.Size());
	SnapshotManifest Manifest2;
	ReadManifest(/* out */ Manifest2, Instream);

	CHECK(Manifest.Id == Manifest2.Id);
	CHECK(Manifest.ChunkHash == Manifest2.ChunkHash);
}

}  // namespace zen

