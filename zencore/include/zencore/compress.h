// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include "zencore/zencore.h"

namespace zen::CompressedBuffer {

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static constexpr uint64_t DefaultBlockSize = 256 * 1024;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Method used to compress the data in a compressed buffer. */
enum class Method : uint8_t
{
	/** Header is followed by one uncompressed block. */
	None = 0,
	/** Header is followed by an array of compressed block sizes then the compressed blocks. */
	LZ4 = 4,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Header used on every compressed buffer. Always stored in big-endian format */
struct BufferHeader
{
	static constexpr uint32_t ExpectedMagic = 0xb7756362;

	/** A magic number to identify a compressed buffer. Always 0xb7756362 */
	uint32_t Magic = ExpectedMagic;
	/** A CRC-32 used to check integrity of the buffer. Uses the polynomial 0x04c11db7 */
	uint32_t Crc32 = 0;
	/** The method used to compress the buffer. Affects layout of data following the header */
	Method Method = Method::None;
	/** The reserved bytes must be initialized to zero */
	uint8_t Reserved[2]{};
	/** The power of two size of every uncompressed block except the last. Size is 1 << BlockSizeExponent */
	uint8_t BlockSizeExponent = 0;
	/** The number of blocks that follow the header */
	uint32_t BlockCount = 0;
	/** The total size of the uncompressed data */
	uint64_t TotalRawSize = 0;
	/** The total size of the compressed data including the header */
	uint64_t TotalCompressedSize = 0;
};

static_assert(sizeof(BufferHeader) == 32, "BufferHeader is the wrong size");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

}  // namespace zen::CompressedBuffer

