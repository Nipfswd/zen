// Copyright Noah Games, Inc. All Rights Reserved.

#include "chunk.h"
#include <doctest/doctest.h>

#include <gsl/gsl-lite.hpp>

#include <zencore/filesystem.h>
#include <zencore/iohash.h>
#include <zencore/refcount.h>
#include <zencore/scopeguard.h>
#include <zencore/sha1.h>
#include <zencore/string.h>
#include <zencore/thread.h>
#include <zencore/timer.h>
#include <zenstore/cas.h>

#include "../internalfile.h"

#include <lz4.h>
#include <spdlog/spdlog.h>
#include <zstd.h>

#include <ppl.h>
#include <ppltasks.h>

#include <cmath>
#include <filesystem>
#include <random>
#include <vector>

//////////////////////////////////////////////////////////////////////////

namespace detail {
static const uint32_t buzhashTable[] = {
	0x458be752, 0xc10748cc, 0xfbbcdbb8, 0x6ded5b68, 0xb10a82b5, 0x20d75648, 0xdfc5665f, 0xa8428801, 0x7ebf5191, 0x841135c7, 0x65cc53b3,
	0x280a597c, 0x16f60255, 0xc78cbc3e, 0x294415f5, 0xb938d494, 0xec85c4e6, 0xb7d33edc, 0xe549b544, 0xfdeda5aa, 0x882bf287, 0x3116737c,
	0x05569956, 0xe8cc1f68, 0x0806ac5e, 0x22a14443, 0x15297e10, 0x50d090e7, 0x4ba60f6f, 0xefd9f1a7, 0x5c5c885c, 0x82482f93, 0x9bfd7c64,
	0x0b3e7276, 0xf2688e77, 0x8fad8abc, 0xb0509568, 0xf1ada29f, 0xa53efdfe, 0xcb2b1d00, 0xf2a9e986, 0x6463432b, 0x95094051, 0x5a223ad2,
	0x9be8401b, 0x61e579cb, 0x1a556a14, 0x5840fdc2, 0x9261ddf6, 0xcde002bb, 0x52432bb0, 0xbf17373e, 0x7b7c222f, 0x2955ed16, 0x9f10ca59,
	0xe840c4c9, 0xccabd806, 0x14543f34, 0x1462417a, 0x0d4a1f9c, 0x087ed925, 0xd7f8f24c, 0x7338c425, 0xcf86c8f5, 0xb19165cd, 0x9891c393,
	0x325384ac, 0x0308459d, 0x86141d7e, 0xc922116a, 0xe2ffa6b6, 0x53f52aed, 0x2cd86197, 0xf5b9f498, 0xbf319c8f, 0xe0411fae, 0x977eb18c,
	0xd8770976, 0x9833466a, 0xc674df7f, 0x8c297d45, 0x8ca48d26, 0xc49ed8e2, 0x7344f874, 0x556f79c7, 0x6b25eaed, 0xa03e2b42, 0xf68f66a4,
	0x8e8b09a2, 0xf2e0e62a, 0x0d3a9806, 0x9729e493, 0x8c72b0fc, 0x160b94f6, 0x450e4d3d, 0x7a320e85, 0xbef8f0e1, 0x21d73653, 0x4e3d977a,
	0x1e7b3929, 0x1cc6c719, 0xbe478d53, 0x8d752809, 0xe6d8c2c6, 0x275f0892, 0xc8acc273, 0x4cc21580, 0xecc4a617, 0xf5f7be70, 0xe795248a,
	0x375a2fe9, 0x425570b6, 0x8898dcf8, 0xdc2d97c4, 0x0106114b, 0x364dc22f, 0x1e0cad1f, 0xbe63803c, 0x5f69fac2, 0x4d5afa6f, 0x1bc0dfb5,
	0xfb273589, 0x0ea47f7b, 0x3c1c2b50, 0x21b2a932, 0x6b1223fd, 0x2fe706a8, 0xf9bd6ce2, 0xa268e64e, 0xe987f486, 0x3eacf563, 0x1ca2018c,
	0x65e18228, 0x2207360a, 0x57cf1715, 0x34c37d2b, 0x1f8f3cde, 0x93b657cf, 0x31a019fd, 0xe69eb729, 0x8bca7b9b, 0x4c9d5bed, 0x277ebeaf,
	0xe0d8f8ae, 0xd150821c, 0x31381871, 0xafc3f1b0, 0x927db328, 0xe95effac, 0x305a47bd, 0x426ba35b, 0x1233af3f, 0x686a5b83, 0x50e072e5,
	0xd9d3bb2a, 0x8befc475, 0x487f0de6, 0xc88dff89, 0xbd664d5e, 0x971b5d18, 0x63b14847, 0xd7d3c1ce, 0x7f583cf3, 0x72cbcb09, 0xc0d0a81c,
	0x7fa3429b, 0xe9158a1b, 0x225ea19a, 0xd8ca9ea3, 0xc763b282, 0xbb0c6341, 0x020b8293, 0xd4cd299d, 0x58cfa7f8, 0x91b4ee53, 0x37e4d140,
	0x95ec764c, 0x30f76b06, 0x5ee68d24, 0x679c8661, 0xa41979c2, 0xf2b61284, 0x4fac1475, 0x0adb49f9, 0x19727a23, 0x15a7e374, 0xc43a18d5,
	0x3fb1aa73, 0x342fc615, 0x924c0793, 0xbee2d7f0, 0x8a279de9, 0x4aa2d70c, 0xe24dd37f, 0xbe862c0b, 0x177c22c2, 0x5388e5ee, 0xcd8a7510,
	0xf901b4fd, 0xdbc13dbc, 0x6c0bae5b, 0x64efe8c7, 0x48b02079, 0x80331a49, 0xca3d8ae6, 0xf3546190, 0xfed7108b, 0xc49b941b, 0x32baf4a9,
	0xeb833a4a, 0x88a3f1a5, 0x3a91ce0a, 0x3cc27da1, 0x7112e684, 0x4a3096b1, 0x3794574c, 0xa3c8b6f3, 0x1d213941, 0x6e0a2e00, 0x233479f1,
	0x0f4cd82f, 0x6093edd2, 0x5d7d209e, 0x464fe319, 0xd4dcac9e, 0x0db845cb, 0xfb5e4bc3, 0xe0256ce1, 0x09fb4ed1, 0x0914be1e, 0xa5bdb2c3,
	0xc6eb57bb, 0x30320350, 0x3f397e91, 0xa67791bc, 0x86bc0e2c, 0xefa0a7e2, 0xe9ff7543, 0xe733612c, 0xd185897b, 0x329e5388, 0x91dd236b,
	0x2ecb0d93, 0xf4d82a3d, 0x35b5c03f, 0xe4e606f0, 0x05b21843, 0x37b45964, 0x5eff22f4, 0x6027f4cc, 0x77178b3c, 0xae507131, 0x7bf7cabc,
	0xf9c18d66, 0x593ade65, 0xd95ddf11,
};

// ROL operation (compiler turns this into a ROL when optimizing)
static inline uint32_t
Rotate32(uint32_t Value, size_t RotateCount)
{
	RotateCount &= 31;

	return ((Value) << (RotateCount)) | ((Value) >> (32 - RotateCount));
}
}  // namespace detail

//////////////////////////////////////////////////////////////////////////

class ZenChunker
{
public:
	void   SetChunkSize(size_t MinSize, size_t MaxSize, size_t AvgSize);
	size_t ScanChunk(const void* DataBytes, size_t ByteCount);
	void   Reset();

	// This controls which chunking approach is used - threshold or
	// modulo based. Threshold is faster and generates similarly sized
	// chunks
	void SetUseThreshold(bool NewState) { m_useThreshold = NewState; }

	inline size_t	ChunkSizeMin() const { return m_chunkSizeMin; }
	inline size_t	ChunkSizeMax() const { return m_chunkSizeMax; }
	inline size_t	ChunkSizeAvg() const { return m_chunkSizeAvg; }
	inline uint64_t BytesScanned() const { return m_bytesScanned; }

	static constexpr size_t NoBoundaryFound = size_t(~0ull);

private:
	size_t m_chunkSizeMin = 0;
	size_t m_chunkSizeMax = 0;
	size_t m_chunkSizeAvg = 0;

	uint32_t m_discriminator = 0;  // Computed in SetChunkSize()
	uint32_t m_threshold	 = 0;  // Computed in SetChunkSize()

	bool m_useThreshold = true;

	static constexpr size_t kChunkSizeLimitMax = 64 * 1024 * 1024;
	static constexpr size_t kChunkSizeLimitMin = 1024;

	static constexpr size_t kDefaultAverageChunkSize = 64 * 1024;

	static constexpr int kWindowSize = 48;
	uint8_t				 m_window[kWindowSize];
	uint32_t			 m_windowSize = 0;

	uint32_t m_currentHash		= 0;
	uint32_t m_currentChunkSize = 0;

	uint64_t m_bytesScanned = 0;

	size_t InternalScanChunk(const void* DataBytes, size_t ByteCount);
	void   InternalReset();
};

void
ZenChunker::Reset()
{
	InternalReset();

	m_bytesScanned = 0;
}

void
ZenChunker::InternalReset()
{
	m_currentHash	   = 0;
	m_currentChunkSize = 0;
	m_windowSize	   = 0;
}

void
ZenChunker::SetChunkSize(size_t MinSize, size_t MaxSize, size_t AvgSize)
{
	if (m_windowSize)
		return;	 // Already started

	static_assert(kChunkSizeLimitMin > kWindowSize);

	if (AvgSize)
	{
		// TODO: Validate AvgSize range
	}
	else
	{
		if (MinSize && MaxSize)
		{
			AvgSize = lrint(pow(2, (log2(MinSize) + log2(MaxSize)) / 2));
		}
		else if (MinSize)
		{
			AvgSize = MinSize * 4;
		}
		else if (MaxSize)
		{
			AvgSize = MaxSize / 4;
		}
		else
		{
			AvgSize = kDefaultAverageChunkSize;
		}
	}

	if (MinSize)
	{
		// TODO: Validate MinSize range
	}
	else
	{
		MinSize = std::max(AvgSize / 4, kChunkSizeLimitMin);
	}

	if (MaxSize)
	{
		// TODO: Validate MaxSize range
	}
	else
	{
		MaxSize = std::min(AvgSize * 4, kChunkSizeLimitMax);
	}

	m_discriminator = gsl::narrow<uint32_t>(AvgSize - MinSize);

	if (m_discriminator < MinSize)
	{
		m_discriminator = gsl::narrow<uint32_t>(MinSize);
	}

	if (m_discriminator > MaxSize)
	{
		m_discriminator = gsl::narrow<uint32_t>(MaxSize);
	}

	m_threshold = gsl::narrow<uint32_t>((uint64_t(std::numeric_limits<uint32_t>::max()) + 1) / m_discriminator);

	m_chunkSizeMin = MinSize;
	m_chunkSizeMax = MaxSize;
	m_chunkSizeAvg = AvgSize;
}

size_t
ZenChunker::ScanChunk(const void* DataBytesIn, size_t ByteCount)
{
	size_t Result = InternalScanChunk(DataBytesIn, ByteCount);

	if (Result == NoBoundaryFound)
	{
		m_bytesScanned += ByteCount;
	}
	else
	{
		m_bytesScanned += Result;
	}

	return Result;
}

size_t
ZenChunker::InternalScanChunk(const void* DataBytesIn, size_t ByteCount)
{
	size_t		   CurrentOffset = 0;
	const uint8_t* CursorPtr	 = reinterpret_cast<const uint8_t*>(DataBytesIn);

	// There's no point in updating the hash if we know we're not
	// going to have a cut point, so just skip the data. This logic currently
	// provides roughly a 20% speedup on my machine

	const size_t NeedHashOffset = m_chunkSizeMin - kWindowSize;

	if (m_currentChunkSize < NeedHashOffset)
	{
		const uint32_t SkipBytes = gsl::narrow<uint32_t>(std::min<uint64_t>(ByteCount, NeedHashOffset - m_currentChunkSize));

		ByteCount -= SkipBytes;
		m_currentChunkSize += SkipBytes;
		CurrentOffset += SkipBytes;
		CursorPtr += SkipBytes;

		m_windowSize = 0;

		if (ByteCount == 0)
		{
			return NoBoundaryFound;
		}
	}

	// Fill window first

	if (m_windowSize < kWindowSize)
	{
		const uint32_t FillBytes = uint32_t(std::min<size_t>(ByteCount, kWindowSize - m_windowSize));

		memcpy(&m_window[m_windowSize], CursorPtr, FillBytes);

		CursorPtr += FillBytes;

		m_windowSize += FillBytes;
		m_currentChunkSize += FillBytes;

		CurrentOffset += FillBytes;
		ByteCount -= FillBytes;

		if (m_windowSize < kWindowSize)
		{
			return NoBoundaryFound;
		}

		// We have a full window, initialize hash

		uint32_t CurrentHash = 0;

		for (int i = 1; i < kWindowSize; ++i)
		{
			CurrentHash ^= detail::Rotate32(detail::buzhashTable[m_window[i - 1]], kWindowSize - i);
		}

		m_currentHash = CurrentHash ^ detail::buzhashTable[m_window[kWindowSize - 1]];
	}

	// Scan for boundaries (i.e points where the hash matches the value determined by
	// the discriminator)

	uint32_t CurrentHash	  = m_currentHash;
	uint32_t CurrentChunkSize = m_currentChunkSize;

	size_t Index = CurrentChunkSize % kWindowSize;

	if (m_threshold && m_useThreshold)
	{
		// This is roughly 4x faster than the general modulo approach on my
		// TR 3990X (~940MB/sec) and doesn't require any special parameters to
		// achieve max performance

		while (ByteCount)
		{
			const uint8_t NewByte = *CursorPtr;
			const uint8_t OldByte = m_window[Index];

			CurrentHash = detail::Rotate32(CurrentHash, 1) ^ detail::Rotate32(detail::buzhashTable[OldByte], m_windowSize) ^
						  detail::buzhashTable[NewByte];

			CurrentChunkSize++;
			CurrentOffset++;

			if (CurrentChunkSize >= m_chunkSizeMin)
			{
				bool foundBreak;

				if (CurrentChunkSize >= m_chunkSizeMax)
				{
					foundBreak = true;
				}
				else
				{
					foundBreak = CurrentHash <= m_threshold;
				}

				if (foundBreak)
				{
					// Boundary found!
					InternalReset();

					return CurrentOffset;
				}
			}

			m_window[Index++] = *CursorPtr;

			if (Index == kWindowSize)
			{
				Index = 0;
			}

			++CursorPtr;
			--ByteCount;
		}
	}
	else if ((m_discriminator & (m_discriminator - 1)) == 0)
	{
		// This is quite a bit faster than the generic modulo path, but
		// requires a very specific average chunk size to be used. If you
		// pass in an even power-of-two divided by 0.75 as the average
		// chunk size you'll hit this path

		const uint32_t Mask = m_discriminator - 1;

		while (ByteCount)
		{
			const uint8_t NewByte = *CursorPtr;
			const uint8_t OldByte = m_window[Index];

			CurrentHash = detail::Rotate32(CurrentHash, 1) ^ detail::Rotate32(detail::buzhashTable[OldByte], m_windowSize) ^
						  detail::buzhashTable[NewByte];

			CurrentChunkSize++;
			CurrentOffset++;

			if (CurrentChunkSize >= m_chunkSizeMin)
			{
				bool foundBreak;

				if (CurrentChunkSize >= m_chunkSizeMax)
				{
					foundBreak = true;
				}
				else
				{
					foundBreak = (CurrentHash & Mask) == Mask;
				}

				if (foundBreak)
				{
					// Boundary found!
					InternalReset();

					return CurrentOffset;
				}
			}

			m_window[Index++] = *CursorPtr;

			if (Index == kWindowSize)
			{
				Index = 0;
			}

			++CursorPtr;
			--ByteCount;
		}
	}
	else
	{
		// This is the slowest path, which caps out around 250MB/sec for large sizes
		// on my TR3900X

		while (ByteCount)
		{
			const uint8_t NewByte = *CursorPtr;
			const uint8_t OldByte = m_window[Index];

			CurrentHash = detail::Rotate32(CurrentHash, 1) ^ detail::Rotate32(detail::buzhashTable[OldByte], m_windowSize) ^
						  detail::buzhashTable[NewByte];

			CurrentChunkSize++;
			CurrentOffset++;

			if (CurrentChunkSize >= m_chunkSizeMin)
			{
				bool foundBreak;

				if (CurrentChunkSize >= m_chunkSizeMax)
				{
					foundBreak = true;
				}
				else
				{
					foundBreak = (CurrentHash % m_discriminator) == (m_discriminator - 1);
				}

				if (foundBreak)
				{
					// Boundary found!
					InternalReset();

					return CurrentOffset;
				}
			}

			m_window[Index++] = *CursorPtr;

			if (Index == kWindowSize)
			{
				Index = 0;
			}

			++CursorPtr;
			--ByteCount;
		}
	}

	m_currentChunkSize = CurrentChunkSize;
	m_currentHash	   = CurrentHash;

	return NoBoundaryFound;
}

//////////////////////////////////////////////////////////////////////////

class DirectoryScanner
{
public:
	struct FileEntry
	{
		std::filesystem::path Path;
		uint64_t			  FileSize;
	};

	const std::vector<FileEntry>& Files() { return m_Files; }
	std::vector<FileEntry>&&	  TakeFiles() { return std::move(m_Files); }
	uint64_t					  FileBytes() const { return m_FileBytes; }

	void Scan(std::filesystem::path RootPath)
	{
		for (const std::filesystem::directory_entry& Entry : std::filesystem::recursive_directory_iterator(RootPath))
		{
			if (Entry.is_regular_file())
			{
				m_Files.push_back({Entry.path(), Entry.file_size()});
				m_FileBytes += Entry.file_size();
			}
		}
	}

private:
	std::vector<FileEntry> m_Files;
	uint64_t			   m_FileBytes = 0;
};

//////////////////////////////////////////////////////////////////////////

class BaseChunker
{
public:
	void SetCasStore(zen::CasStore* CasStore) { m_CasStore = CasStore; }

	struct StatsBlock
	{
		uint64_t TotalBytes		  = 0;
		uint64_t TotalChunks	  = 0;
		uint64_t TotalCompressed  = 0;
		uint64_t UniqueBytes	  = 0;
		uint64_t UniqueChunks	  = 0;
		uint64_t UniqueCompressed = 0;
		uint64_t DuplicateBytes	  = 0;
		uint64_t NewCasChunks	  = 0;
		uint64_t NewCasBytes	  = 0;

		StatsBlock& operator+=(const StatsBlock& Rhs)
		{
			TotalBytes += Rhs.TotalBytes;
			TotalChunks += Rhs.TotalChunks;
			TotalCompressed += Rhs.TotalCompressed;
			UniqueBytes += Rhs.UniqueBytes;
			UniqueChunks += Rhs.UniqueChunks;
			UniqueCompressed += Rhs.UniqueCompressed;
			DuplicateBytes += Rhs.DuplicateBytes;
			NewCasChunks += Rhs.NewCasChunks;
			NewCasBytes += Rhs.NewCasBytes;
			return *this;
		}
	};

protected:
	Concurrency::combinable<StatsBlock> m_StatsBlock;

public:
	StatsBlock SumStats()
	{
		StatsBlock _;
		m_StatsBlock.combine_each([&](const StatsBlock& Block) { _ += Block; });
		return _;
	}

protected:
	struct HashSet
	{
		bool Add(const zen::IoHash& Hash)
		{
			const uint8_t ShardNo = Hash.Hash[19];

			Bucket& Shard = m_Buckets[ShardNo];

			zen::RwLock::ExclusiveLockScope _(Shard.HashLock);

			auto rv = Shard.Hashes.insert(Hash);

			return rv.second;
		}

	private:
		struct alignas(64) Bucket
		{
			zen::RwLock											 HashLock;
			std::unordered_set<zen::IoHash, zen::IoHash::Hasher> Hashes;
#pragma warning(suppress : 4324)  // Padding due to alignment
		};

		Bucket m_Buckets[256];
	};

	zen::CasStore* m_CasStore = nullptr;
};

class FixedBlockSizeChunker : public BaseChunker
{
public:
	FixedBlockSizeChunker(std::filesystem::path InRootPath) : m_RootPath(InRootPath) {}
	~FixedBlockSizeChunker() = default;

	void SetChunkSize(uint64_t ChunkSize)
	{
		/* TODO: verify validity of chunk size */
		m_ChunkSize = ChunkSize;
	}
	void SetUseCompression(bool UseCompression) { m_UseCompression = UseCompression; }
	void SetPerformValidation(bool PerformValidation) { m_PerformValidation = PerformValidation; }

	void InitCompression()
	{
		if (!m_CompressionBufferManager)
		{
			std::call_once(m_CompressionInitFlag, [&] {
				// Wasteful, but should only be temporary
				m_CompressionBufferManager.reset(new FileBufferManager(m_ChunkSize * 2, 128));
			});
		}
	}

	void ChunkFile(const DirectoryScanner::FileEntry& File)
	{
		InitCompression();

		std::filesystem::path RelativePath{std::filesystem::relative(File.Path.generic_string(), m_RootPath)};

		Concurrency::task_group ChunkProcessTasks;

		spdlog::info("Chunking {} ({})", RelativePath.generic_string(), zen::NiceBytes(File.FileSize));

		zen::RefPtr<InternalFile> Zfile = new InternalFile;
		Zfile->OpenRead(File.Path);

		size_t	 FileBytes		   = Zfile->GetFileSize();
		uint64_t CurrentFileOffset = 0;

		std::vector<zen::IoHash> BlockHashes{(FileBytes + m_ChunkSize - 1) / m_ChunkSize};

		while (FileBytes)
		{
			zen::IoBuffer Buffer = m_BufferManager.AllocBuffer();

			const size_t BytesToRead = std::min(FileBytes, Buffer.Size());

			Zfile->Read((void*)Buffer.Data(), BytesToRead, CurrentFileOffset);

			auto ProcessChunk = [this, Buffer, &BlockHashes, CurrentFileOffset, BytesToRead] {
				StatsBlock& Stats = m_StatsBlock.local();
				for (uint64_t Offset = 0; Offset < BytesToRead; Offset += m_ChunkSize)
				{
					const uint8_t*	  DataPointer = reinterpret_cast<const uint8_t*>(Buffer.Data()) + Offset;
					const uint64_t	  DataSize	  = std::min(BytesToRead - Offset, m_ChunkSize);
					const zen::IoHash Hash		  = zen::IoHash::HashMemory(DataPointer, DataSize);

					BlockHashes[(CurrentFileOffset + Offset) / m_ChunkSize] = Hash;

					const bool IsNew = m_LocalHashSet.Add(Hash);

					if (IsNew)
					{
						if (m_UseCompression)
						{
							if (true)
							{
								// Compress using ZSTD
								const size_t CompressBufferSize = ZSTD_compressBound(DataSize);

								zen::IoBuffer CompressedBuffer = m_CompressionBufferManager->AllocBuffer();
								char*		  CompressBuffer   = (char*)CompressedBuffer.Data();

								ZEN_ASSERT(CompressedBuffer.Size() >= CompressBufferSize);

								const size_t CompressedSize = ZSTD_compress(CompressBuffer,
																			CompressBufferSize,
																			(const char*)DataPointer,
																			DataSize,
																			ZSTD_CLEVEL_DEFAULT);

								Stats.UniqueCompressed += CompressedSize;

								if (m_CasStore)
								{
									const zen::IoHash			CompressedHash = zen::IoHash::HashMemory(CompressBuffer, CompressedSize);
									zen::CasStore::InsertResult Result =
										m_CasStore->InsertChunk(CompressBuffer, CompressedSize, CompressedHash);

									if (Result.New)
									{
										Stats.NewCasChunks += 1;
										Stats.NewCasBytes += CompressedSize;
									}
								}

								m_CompressionBufferManager->ReturnBuffer(CompressedBuffer);
							}
							else
							{
								// Compress using LZ4
								const int CompressBufferSize = LZ4_compressBound(gsl::narrow<int>(DataSize));

								zen::IoBuffer CompressedBuffer = m_CompressionBufferManager->AllocBuffer();
								char*		  CompressBuffer   = (char*)CompressedBuffer.Data();

								ZEN_ASSERT(CompressedBuffer.Size() >= CompressBufferSize);

								const int CompressedSize = LZ4_compress_default((const char*)DataPointer,
																				CompressBuffer,
																				gsl::narrow<int>(DataSize),
																				CompressBufferSize);

								Stats.UniqueCompressed += CompressedSize;

								if (m_CasStore)
								{
									const zen::IoHash			CompressedHash = zen::IoHash::HashMemory(CompressBuffer, CompressedSize);
									zen::CasStore::InsertResult Result =
										m_CasStore->InsertChunk(CompressBuffer, CompressedSize, CompressedHash);

									if (Result.New)
									{
										Stats.NewCasChunks += 1;
										Stats.NewCasBytes += CompressedSize;
									}
								}

								m_CompressionBufferManager->ReturnBuffer(CompressedBuffer);
							}
						}
						else if (m_CasStore)
						{
							zen::CasStore::InsertResult Result = m_CasStore->InsertChunk(zen::IoBuffer(Buffer, Offset, DataSize), Hash);

							if (Result.New)
							{
								Stats.NewCasChunks += 1;
								Stats.NewCasBytes += DataSize;
							}
						}

						Stats.UniqueBytes += DataSize;
						Stats.UniqueChunks += 1;
					}
					else
					{
						// We've seen this chunk before
						Stats.DuplicateBytes += DataSize;
					}

					Stats.TotalBytes += DataSize;
					Stats.TotalChunks += 1;
				}

				m_BufferManager.ReturnBuffer(Buffer);
			};

			ChunkProcessTasks.run(ProcessChunk);

			CurrentFileOffset += BytesToRead;
			FileBytes -= BytesToRead;
		}

		ChunkProcessTasks.wait();

		// Verify pass

		if (!m_UseCompression && m_PerformValidation)
		{
			const uint8_t* FileData	   = reinterpret_cast<const uint8_t*>(Zfile->MemoryMapFile());
			uint64_t	   Offset	   = 0;
			const uint64_t BytesToRead = Zfile->GetFileSize();

			for (zen::IoHash& Hash : BlockHashes)
			{
				const uint64_t	  DataSize = std::min(BytesToRead - Offset, m_ChunkSize);
				const zen::IoHash CalcHash = zen::IoHash::HashMemory(FileData + Offset, DataSize);

				ZEN_ASSERT(CalcHash == Hash);

				zen::IoBuffer FoundValue = m_CasStore->FindChunk(CalcHash);

				ZEN_ASSERT(FoundValue);
				ZEN_ASSERT(FoundValue.Size() == DataSize);

				Offset += DataSize;
			}
		}
	}

private:
	std::filesystem::path m_RootPath;
	FileBufferManager	  m_BufferManager{128 * 1024, 128};
	uint64_t			  m_ChunkSize = 64 * 1024;
	HashSet				  m_LocalHashSet;
	bool				  m_UseCompression	  = true;
	bool				  m_PerformValidation = false;

	std::once_flag					   m_CompressionInitFlag;
	std::unique_ptr<FileBufferManager> m_CompressionBufferManager;
};

class VariableBlockSizeChunker : public BaseChunker
{
public:
	VariableBlockSizeChunker(std::filesystem::path InRootPath) : m_RootPath(InRootPath) {}

	void SetAverageChunkSize(uint64_t AverageChunkSize) { m_AverageChunkSize = AverageChunkSize; }
	void SetUseCompression(bool UseCompression) { m_UseCompression = UseCompression; }

	void ChunkFile(const DirectoryScanner::FileEntry& File)
	{
		std::filesystem::path RelativePath{std::filesystem::relative(File.Path.generic_string(), m_RootPath)};

		spdlog::info("Chunking {} ({})", RelativePath.generic_string(), zen::NiceBytes(File.FileSize));

		zen::RefPtr<InternalFile> Zfile = new InternalFile;
		Zfile->OpenRead(File.Path);

		// Could use IoBuffer here to help manage lifetimes of things
		// across tasks / threads

		ZenChunker Chunker;
		Chunker.SetChunkSize(0, 0, m_AverageChunkSize);

		const size_t DataSize = Zfile->GetFileSize();

		std::vector<size_t> Boundaries;

		uint64_t CurrentStreamPosition = 0;
		uint64_t CurrentChunkSize	   = 0;
		size_t	 RemainBytes		   = DataSize;

		Concurrency::structured_task_group CompressionTasks;

		zen::IoHashStream IoHashStream;

		while (RemainBytes != 0)
		{
			zen::IoBuffer Buffer = m_BufferManager.AllocBuffer();

			size_t BytesToRead = std::min(RemainBytes, Buffer.Size());

			uint8_t* DataPointer = (uint8_t*)Buffer.Data();

			Zfile->Read(DataPointer, BytesToRead, CurrentStreamPosition);

			StatsBlock& Stats = m_StatsBlock.local();

			while (BytesToRead)
			{
				const size_t Boundary = Chunker.ScanChunk(DataPointer, BytesToRead);

				if (Boundary == ZenChunker::NoBoundaryFound)
				{
					IoHashStream.Append(DataPointer, BytesToRead);
					CurrentStreamPosition += BytesToRead;
					CurrentChunkSize += BytesToRead;
					RemainBytes -= BytesToRead;
					break;
				}

				// Boundary found

				IoHashStream.Append(DataPointer, Boundary);

				const zen::IoHash Hash	= IoHashStream.GetHash();
				const bool		  IsNew = m_LocalHashSet.Add(Hash);

				CurrentStreamPosition += Boundary;
				CurrentChunkSize += Boundary;
				Boundaries.push_back(CurrentStreamPosition);

				if (IsNew)
				{
					Stats.UniqueBytes += CurrentChunkSize;
				}
				else
				{
					// We've seen this chunk before
					Stats.DuplicateBytes += CurrentChunkSize;
				}

				DataPointer += Boundary;
				RemainBytes -= Boundary;
				BytesToRead -= Boundary;
				CurrentChunkSize = 0;
				IoHashStream.Reset();
			}

			m_BufferManager.ReturnBuffer(Buffer);

#if 0
			Active.AddCount();	// needs fixing

			Concurrency::create_task([this, Zfile, CurrentPosition, DataPointer, &Active] {
				const zen::IoHash Hash = zen::IoHash::HashMemory(DataPointer, CurrentPosition);

				const bool isNew = m_LocalHashSet.Add(Hash);

				const int CompressBufferSize = LZ4_compressBound(gsl::narrow<int>(CurrentPosition));
				char* CompressBuffer = (char*)_aligned_malloc(CompressBufferSize, 16);

				const int CompressedSize =
					LZ4_compress_default((const char*)DataPointer, CompressBuffer, gsl::narrow<int>(CurrentPosition), CompressBufferSize);

				m_TotalCompressed.local() += CompressedSize;

				if (isNew)
				{
					m_UniqueBytes.local() += CurrentPosition;
					m_UniqueCompressed.local() += CompressedSize;

					if (m_CasStore)
					{
						const zen::IoHash CompressedHash = zen::IoHash::HashMemory(CompressBuffer, CompressedSize);
						m_CasStore->InsertChunk(CompressBuffer, CompressedSize, CompressedHash);
					}
				}

				Active.Signal();	// needs fixing

				_aligned_free(CompressBuffer);
			});
#endif
		}

		StatsBlock& Stats = m_StatsBlock.local();
		Stats.TotalBytes += DataSize;
		Stats.TotalChunks += Boundaries.size() + 1;

		// TODO: Wait for all compression tasks

		auto ChunkCount = Boundaries.size() + 1;

		spdlog::info("Split {} ({}) into {} chunks, avg size {}",
					 RelativePath.generic_string(),
					 zen::NiceBytes(File.FileSize),
					 ChunkCount,
					 File.FileSize / ChunkCount);
	};

private:
	HashSet				  m_LocalHashSet;
	std::filesystem::path m_RootPath;
	uint64_t			  m_AverageChunkSize = 32 * 1024;
	bool				  m_UseCompression	 = true;
	FileBufferManager	  m_BufferManager{128 * 1024, 128};
};

//////////////////////////////////////////////////////////////////////////

ChunkCommand::ChunkCommand()
{
	m_Options.add_options()("r,root", "Root directory for CAS pool", cxxopts::value(m_RootDirectory));
	m_Options.add_options()("d,dir", "Directory to scan", cxxopts::value(m_ScanDirectory));
	m_Options.add_options()("c,chunk-size", "Use fixed chunk size", cxxopts::value(m_ChunkSize));
	m_Options.add_options()("a,average-chunk-size", "Use dynamic chunk size", cxxopts::value(m_AverageChunkSize));
	m_Options.add_options()("compress", "Apply compression to chunks", cxxopts::value(m_UseCompression));
}

ChunkCommand::~ChunkCommand() = default;

int
ChunkCommand::Run(const ZenCliOptions& GlobalOptions, int argc, char** argv)
{
	ZEN_UNUSED(GlobalOptions);

	auto result = m_Options.parse(argc, argv);

	bool IsValid = m_ScanDirectory.length();

	if (!IsValid)
		throw cxxopts::OptionParseException("Chunk command requires a directory to scan");

	if ((m_ChunkSize && m_AverageChunkSize) && (!m_ChunkSize && !m_AverageChunkSize))
		throw cxxopts::OptionParseException("Either of --chunk-size or --average-chunk-size must be used");

	std::unique_ptr<zen::CasStore> CasStore;

	if (!m_RootDirectory.empty())
	{
		zen::CasStoreConfiguration Config;
		Config.RootDirectory = m_RootDirectory;

		CasStore.reset(zen::CreateCasStore());
		CasStore->Initialize(Config);
	}

	// Gather list of files to process

	spdlog::info("Gathering files from {}", m_ScanDirectory);

	std::filesystem::path RootPath{m_ScanDirectory};
	DirectoryScanner	  Scanner;
	Scanner.Scan(RootPath);

	auto	 Files	   = Scanner.TakeFiles();
	uint64_t FileBytes = Scanner.FileBytes();

	std::sort(begin(Files), end(Files), [](const DirectoryScanner::FileEntry& Lhs, const DirectoryScanner::FileEntry& Rhs) {
		return Lhs.FileSize < Rhs.FileSize;
	});

	spdlog::info("Gathered {} files, total size {}", Files.size(), zen::NiceBytes(FileBytes));

	auto ReportSummary = [&](BaseChunker& Chunker, uint64_t ElapsedMs) {
		const BaseChunker::StatsBlock& Stats = Chunker.SumStats();

		const size_t TotalChunkCount = Stats.TotalChunks;
		spdlog::info("Scanned {} files in {}, generated {} chunks", Files.size(), zen::NiceTimeSpanMs(ElapsedMs), TotalChunkCount);

		const size_t TotalByteCount		  = Stats.TotalBytes;
		const size_t TotalCompressedBytes = Stats.TotalCompressed;

		spdlog::info("Total bytes {} ({}), compresses into {}",
					 zen::NiceBytes(TotalByteCount),
					 zen::NiceByteRate(TotalByteCount, ElapsedMs),
					 zen::NiceBytes(TotalCompressedBytes));

		const size_t TotalUniqueBytes			= Stats.UniqueBytes;
		const size_t TotalUniqueCompressedBytes = Stats.UniqueCompressed;
		const size_t TotalDuplicateBytes		= Stats.DuplicateBytes;

		spdlog::info("Chunksize average {}, unique bytes = {} (compressed {}), dup bytes = {}",
					 TotalByteCount / TotalChunkCount,
					 zen::NiceBytes(TotalUniqueBytes),
					 zen::NiceBytes(TotalUniqueCompressedBytes),
					 zen::NiceBytes(TotalDuplicateBytes));

		spdlog::info("New to CAS: {} chunks, {}", Stats.NewCasChunks, zen::NiceBytes(Stats.NewCasBytes));
	};

	// Process them as quickly as possible

	if (m_AverageChunkSize)
	{
		VariableBlockSizeChunker Chunker{RootPath};
		Chunker.SetAverageChunkSize(m_AverageChunkSize);
		Chunker.SetUseCompression(m_UseCompression);
		Chunker.SetCasStore(CasStore.get());

		zen::Stopwatch timer;

#if 1
		Concurrency::parallel_for_each(begin(Files), end(Files), [&Chunker](const auto& ThisFile) { Chunker.ChunkFile(ThisFile); });
#else
		for (const auto& ThisFile : Files)
		{
			Chunker.ChunkFile(ThisFile);
		}
#endif

		uint64_t ElapsedMs = timer.getElapsedTimeMs();

		ReportSummary(Chunker, ElapsedMs);
	}
	else if (m_ChunkSize)
	{
		FixedBlockSizeChunker Chunker{RootPath};
		Chunker.SetChunkSize(m_ChunkSize);
		Chunker.SetUseCompression(m_UseCompression);
		Chunker.SetCasStore(CasStore.get());

		zen::Stopwatch timer;

		Concurrency::parallel_for_each(begin(Files), end(Files), [&Chunker](const DirectoryScanner::FileEntry& ThisFile) {
			try
			{
				Chunker.ChunkFile(ThisFile);
			}
			catch (std::exception& ex)
			{
				zen::ExtendableStringBuilder<256> Path8;
				zen::WideToUtf8(ThisFile.Path.c_str(), Path8);
				spdlog::warn("Caught exception while chunking '{}': {}", Path8, ex.what());
			}
		});

		uint64_t ElapsedMs = timer.getElapsedTimeMs();

		ReportSummary(Chunker, ElapsedMs);
	}
	else
	{
		ZEN_ASSERT(false);
	}

	// TODO: implement snapshot enumeration and display
	return 0;
}

//////////////////////////////////////////////////////////////////////////

TEST_CASE("chunking")
{
	using namespace zen;

	auto test = [](bool UseThreshold, bool Random, int MinBlockSize, int MaxBlockSize) {
		std::mt19937_64 mt;

		std::vector<uint64_t> bytes;
		bytes.resize(1 * 1024 * 1024);

		if (Random == false)
		{
			// Generate a single block of randomness
			for (auto& w : bytes)
			{
				w = mt();
			}
		}

		for (int i = MinBlockSize; i <= MaxBlockSize; i <<= 1)
		{
			Stopwatch timer;

			ZenChunker chunker;
			chunker.SetUseThreshold(UseThreshold);
			chunker.SetChunkSize(0, 0, i);
			// chunker.SetChunkSize(i / 4, i * 4, 0);
			// chunker.SetChunkSize(i / 8, i * 8, 0);
			// chunker.SetChunkSize(i / 16, i * 16, 0);
			// chunker.SetChunkSize(0, 0, size_t(i / 0.75));	// Hits the fast modulo path

			std::vector<size_t> boundaries;

			size_t CurrentPosition = 0;
			int	   BoundaryCount   = 0;

			do
			{
				if (Random == true)
				{
					// Generate a new block of randomness for each pass
					for (auto& w : bytes)
					{
						w = mt();
					}
				}

				const uint8_t* Ptr		   = reinterpret_cast<const uint8_t*>(bytes.data());
				size_t		   BytesRemain = bytes.size() * sizeof(uint64_t);

				for (;;)
				{
					const size_t Boundary = chunker.ScanChunk(Ptr, BytesRemain);

					if (Boundary == ZenChunker::NoBoundaryFound)
					{
						CurrentPosition += BytesRemain;
						break;
					}

					// Boundary found

					CurrentPosition += Boundary;

					CHECK(CurrentPosition >= chunker.ChunkSizeMin());
					CHECK(CurrentPosition <= chunker.ChunkSizeMax());

					boundaries.push_back(CurrentPosition);

					CurrentPosition = 0;
					Ptr += Boundary;
					BytesRemain -= Boundary;

					++BoundaryCount;
				}
			} while (BoundaryCount < 5000);

			size_t BoundarySum = 0;

			for (const auto& v : boundaries)
			{
				BoundarySum += v;
			}

			double		   Avg			 = double(BoundarySum) / BoundaryCount;
			const uint64_t ElapsedTimeMs = timer.getElapsedTimeMs();

			spdlog::info("{:9} : Avg {:9} - {:2.5} ({:6}, {})",
						 i,
						 Avg,
						 double(i / Avg),
						 NiceTimeSpanMs(ElapsedTimeMs),
						 NiceByteRate(chunker.BytesScanned(), ElapsedTimeMs));
		}
	};

	const bool Random = false;

	SUBCASE("threshold method") { test(/* UseThreshold */ true, /* Random */ Random, 2048, 1 * 1024 * 1024); }

	SUBCASE("mod method") { test(/* UseThreshold */ false, /* Random */ Random, 2048, 1 * 1024 * 1024); }
}

