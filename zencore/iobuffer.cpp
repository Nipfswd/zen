// Copyright Noah Games, Inc. All Rights Reserved.

#include <zencore/iobuffer.h>

#include <doctest/doctest.h>
#include <memory.h>
#include <zencore/memory.h>
#include <zencore/thread.h>
#include <system_error>

#include <atlfile.h>
#include <spdlog/spdlog.h>
#include <gsl/gsl-lite.hpp>

namespace zen {

//////////////////////////////////////////////////////////////////////////

void*
IoBufferCore::AllocateBuffer(size_t InSize, size_t Alignment)
{
	if (((InSize & 0xffFF) == 0) && (Alignment == 0x10000))
	{
		m_Flags |= kLowLevelAlloc;
		return VirtualAlloc(nullptr, InSize, MEM_COMMIT, PAGE_READWRITE);
	}
	else
	{
		return Memory::Alloc(InSize, Alignment);
	}
}

void
IoBufferCore::FreeBuffer()
{
	if (m_Flags & kLowLevelAlloc)
	{
		VirtualFree(const_cast<void*>(m_DataPtr), 0, MEM_DECOMMIT);
	}
	else
	{
		return Memory::Free(const_cast<void*>(m_DataPtr));
	}
}

//////////////////////////////////////////////////////////////////////////

IoBufferCore::IoBufferCore(size_t InSize)
{
	static_assert(sizeof(IoBufferCore) == 32);

	m_DataPtr	= AllocateBuffer(InSize, sizeof(void*));
	m_DataBytes = InSize;

	SetIsOwned(true);
}

IoBufferCore::IoBufferCore(size_t InSize, size_t Alignment)
{
	m_DataPtr	= AllocateBuffer(InSize, Alignment);
	m_DataBytes = InSize;

	SetIsOwned(true);
}

IoBufferCore::~IoBufferCore()
{
	if (IsOwned() && m_DataPtr)
	{
		FreeBuffer();
		m_DataPtr = nullptr;
	}
}

void
IoBufferCore::DeleteThis() const
{
	// We do this just to avoid paying for the cost of a vtable
	if (const IoBufferExtendedCore* _ = ExtendedCore())
	{
		delete _;
	}
	else
	{
		delete this;
	}
}

void
IoBufferCore::Materialize() const
{
	if (const IoBufferExtendedCore* _ = ExtendedCore())
	{
		_->Materialize();
	}
}

void
IoBufferCore::MakeOwned(bool Immutable)
{
	if (!IsOwned())
	{
		void* OwnedDataPtr = AllocateBuffer(m_DataBytes, sizeof(void*));
		memcpy(OwnedDataPtr, m_DataPtr, m_DataBytes);

		m_DataPtr = OwnedDataPtr;
		SetIsOwned(true);
	}

	SetIsImmutable(Immutable);
}

//////////////////////////////////////////////////////////////////////////

IoBufferExtendedCore::IoBufferExtendedCore(void* FileHandle, uint64_t Offset, uint64_t Size, bool TransferHandleOwnership)
: IoBufferCore(nullptr, Size)
, m_FileHandle(FileHandle)
, m_FileOffset(Offset)
{
	m_Flags |= kIsOwned | kIsExtended;

	if (TransferHandleOwnership)
	{
		m_Flags |= kOwnsFile;
	}
}

IoBufferExtendedCore::IoBufferExtendedCore(const IoBufferExtendedCore* Outer, uint64_t Offset, uint64_t Size)
: IoBufferCore(Outer, nullptr, Size)
, m_FileHandle(Outer->m_FileHandle)
, m_FileOffset(Outer->m_FileOffset + Offset)
{
	m_Flags |= kIsOwned | kIsExtended;
}

IoBufferExtendedCore::~IoBufferExtendedCore()
{
	if (m_MappedPointer)
	{
		UnmapViewOfFile(m_MappedPointer);
	}

	if (m_Flags & kOwnsMmap)
	{
		CloseHandle(m_MmapHandle);
	}

	if (m_Flags & kOwnsFile)
	{
		BOOL Success = CloseHandle(m_FileHandle);

		if (!Success)
		{
			spdlog::warn("Error reported on file handle close!");
		}
	}

	m_DataPtr = nullptr;
}

RwLock g_MappingLock;

void
IoBufferExtendedCore::Materialize() const
{
	// The synchronization scheme here is very primitive, if we end up with
	// a lot of contention we can make it more fine-grained

	if (m_MmapHandle)
		return;

	RwLock::ExclusiveLockScope _(g_MappingLock);

	// Someone could have gotten here first
	if (m_MmapHandle)
		return;

	m_MmapHandle = CreateFileMapping(m_FileHandle,
									 /* lpFileMappingAttributes */ nullptr,
									 /* flProtect */ PAGE_READONLY,
									 /* dwMaximumSizeLow */ 0,
									 /* dwMaximumSizeHigh */ 0,
									 /* lpName */ nullptr);

	if (m_MmapHandle == nullptr)
	{
		throw std::system_error(std::error_code(::GetLastError(), std::system_category()), "file copy failed");
	}

	m_Flags |= kOwnsMmap;

	const uint64_t MapOffset				= m_FileOffset & ~0xffffull;
	const uint64_t MappedOffsetDisplacement = m_FileOffset - MapOffset;
	const uint64_t MapSize					= (MappedOffsetDisplacement + m_DataBytes + 0xffffu) & ~0xffffull;

	void* MappedBase = MapViewOfFile(m_MmapHandle,
									 /* dwDesiredAccess */ FILE_MAP_READ,
									 /* FileOffsetHigh */ uint32_t(MapOffset >> 32),
									 /* FileOffsetLow */ uint32_t(MapOffset & 0xffFFffFFu),
									 /* dwNumberOfBytesToMap */ m_DataBytes);

	if (MappedBase == nullptr)
	{
		throw std::system_error(std::error_code(::GetLastError(), std::system_category()), "MapViewOfFile failed");
	}

	m_MappedPointer = MappedBase;
	m_DataPtr		= reinterpret_cast<uint8_t*>(MappedBase) + MappedOffsetDisplacement;

	m_Flags |= kIsMaterialized;
}

bool
IoBufferExtendedCore::GetFileReference(IoBufferFileReference& OutRef) const
{
	if (m_FileHandle == nullptr)
	{
		return false;
	}

	OutRef.FileHandle	   = m_FileHandle;
	OutRef.FileChunkOffset = m_FileOffset;
	OutRef.FileChunkSize   = m_DataBytes;

	return true;
}

//////////////////////////////////////////////////////////////////////////

IoBuffer::IoBuffer(size_t InSize) : m_Core(new IoBufferCore(InSize))
{
}

IoBuffer::IoBuffer(size_t InSize, uint64_t InAlignment) : m_Core(new IoBufferCore(InSize, InAlignment))
{
}

IoBuffer::IoBuffer(const IoBuffer& OuterBuffer, size_t Offset, size_t Size)
{
	if (Size == ~(0ull))
	{
		Size = std::clamp<size_t>(Size, 0, OuterBuffer.Size() - Offset);
	}

	ZEN_ASSERT(Offset <= OuterBuffer.Size());
	ZEN_ASSERT((Offset + Size) <= OuterBuffer.Size());

	if (IoBufferExtendedCore* Extended = OuterBuffer.m_Core->ExtendedCore())
	{
		m_Core = new IoBufferExtendedCore(Extended, Offset, Size);
	}
	else
	{
		m_Core = new IoBufferCore(OuterBuffer.m_Core, reinterpret_cast<const uint8_t*>(OuterBuffer.Data()) + Offset, Size);
	}
}

IoBuffer::IoBuffer(EFileTag, void* FileHandle, uint64_t ChunkFileOffset, uint64_t ChunkSize)
: m_Core(new IoBufferExtendedCore(FileHandle, ChunkFileOffset, ChunkSize, /* owned */ true))
{
}

IoBuffer::IoBuffer(EBorrowedFileTag, void* FileHandle, uint64_t ChunkFileOffset, uint64_t ChunkSize)
: m_Core(new IoBufferExtendedCore(FileHandle, ChunkFileOffset, ChunkSize, /* owned */ false))
{
}

bool
IoBuffer::GetFileReference(IoBufferFileReference& OutRef) const
{
	if (IoBufferExtendedCore* ExtCore = m_Core->ExtendedCore())
	{
		if (ExtCore->GetFileReference(OutRef))
		{
			return true;
		}
	}

	// Not a file reference

	OutRef.FileHandle	   = 0;
	OutRef.FileChunkOffset = ~0ull;
	OutRef.FileChunkSize   = 0;

	return false;
}

//////////////////////////////////////////////////////////////////////////

IoBuffer
IoBufferBuilder::MakeFromFileHandle(void* FileHandle, uint64_t Offset, uint64_t Size)
{
	return IoBuffer(IoBuffer::BorrowedFile, FileHandle, Offset, Size);
}

IoBuffer
IoBufferBuilder::MakeFromFile(const wchar_t* FileName, uint64_t Offset, uint64_t Size)
{
	CAtlFile DataFile;

	HRESULT hRes = DataFile.Create(FileName, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING);

	if (SUCCEEDED(hRes))
	{
		ULONGLONG FileSize;
		DataFile.GetSize(FileSize);

		if (Size == ~0ull)
		{
			Size = FileSize;
		}
		else
		{
			// Clamp size
			if ((Offset + Size) > FileSize)
			{
				Size = FileSize - Offset;
			}
		}

		return IoBuffer(IoBuffer::File, DataFile.Detach(), Offset, Size);
	}

	return {};
}

//////////////////////////////////////////////////////////////////////////

void
iobuffer_forcelink()
{
}

TEST_CASE("IoBuffer")
{
	zen::IoBuffer buffer1;
	zen::IoBuffer buffer2(16384);
	zen::IoBuffer buffer3(buffer2, 0, buffer2.Size());
}

}  // namespace zen

