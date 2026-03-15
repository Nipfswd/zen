// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <memory.h>
#include "refcount.h"
#include "zencore.h"

namespace zen {

struct IoBufferExtendedCore;

struct IoBufferFileReference
{
	void*	 FileHandle;
	uint64_t FileChunkOffset;
	uint64_t FileChunkSize;
};

struct IoBufferCore
{
public:
	IoBufferCore() = default;
	inline IoBufferCore(const void* DataPtr, size_t SizeBytes) : m_DataPtr(DataPtr), m_DataBytes(SizeBytes) {}
	inline IoBufferCore(const IoBufferCore* Outer, const void* DataPtr, size_t SizeBytes)
	: m_DataPtr(DataPtr)
	, m_DataBytes(SizeBytes)
	, m_OuterCore(Outer)
	{
	}

	ZENCORE_API explicit IoBufferCore(size_t SizeBytes);
	ZENCORE_API IoBufferCore(size_t SizeBytes, size_t Alignment);
	ZENCORE_API ~IoBufferCore();

	// Reference counting

	inline uint32_t AddRef() const { return AtomicIncrement(const_cast<IoBufferCore*>(this)->m_RefCount); }
	inline uint32_t Release() const
	{
		const uint32_t NewRefCount = AtomicDecrement(const_cast<IoBufferCore*>(this)->m_RefCount);
		if (NewRefCount == 0)
		{
			DeleteThis();
		}
		return NewRefCount;
	}

	// Copying reference counted objects doesn't make a lot of sense generally, so let's prevent it

	IoBufferCore(const IoBufferCore&) = delete;
	IoBufferCore(IoBufferCore&&)	  = delete;
	IoBufferCore& operator=(const IoBufferCore&) = delete;
	IoBufferCore& operator=(IoBufferCore&&) = delete;

	//

	ZENCORE_API void Materialize() const;
	ZENCORE_API void DeleteThis() const;
	ZENCORE_API void MakeOwned(bool Immutable = true);

	inline void EnsureDataValid() const
	{
		if ((m_Flags & kIsExtended) && !(m_Flags & kIsMaterialized))
			Materialize();
	}

	inline bool IsOwned() const { return !!(m_Flags & kIsOwned); }
	inline bool IsImmutable() const { return !(m_Flags & kIsMutable); }
	inline bool IsNull() const { return m_DataBytes == 0; }

	inline IoBufferExtendedCore*	   ExtendedCore();
	inline const IoBufferExtendedCore* ExtendedCore() const;

	inline const void* DataPointer() const
	{
		EnsureDataValid();
		return m_DataPtr;
	}

	inline size_t DataBytes() const { return m_DataBytes; }

	inline void Set(const void* Ptr, size_t Sz)
	{
		m_DataPtr	= Ptr;
		m_DataBytes = Sz;
	}

	inline void SetIsOwned(bool NewState)
	{
		if (NewState)
		{
			m_Flags |= kIsOwned;
		}
		else
		{
			m_Flags &= ~kIsOwned;
		}
	}

	inline void SetIsImmutable(bool NewState)
	{
		if (!NewState)
		{
			m_Flags |= kIsMutable;
		}
		else
		{
			m_Flags &= ~kIsMutable;
		}
	}

	inline uint32_t GetRefCount() const { return m_RefCount; }

protected:
	uint32_t				   m_RefCount  = 0;
	mutable uint32_t		   m_Flags	   = 0;
	mutable const void*		   m_DataPtr   = nullptr;
	size_t					   m_DataBytes = 0;
	RefPtr<const IoBufferCore> m_OuterCore;

	enum Flags
	{
		kIsOwned		= 1 << 0,
		kIsMutable		= 1 << 1,
		kIsExtended		= 1 << 2,  // Is actually a SharedBufferExtendedCore
		kIsMaterialized = 1 << 3,  // Data pointers are valid
		kLowLevelAlloc	= 1 << 4,  // Using direct memory allocation
	};

	void* AllocateBuffer(size_t InSize, size_t Alignment);
	void  FreeBuffer();
};

/**
 * An "Extended" core references a segment of a file
 */

struct IoBufferExtendedCore : public IoBufferCore
{
	IoBufferExtendedCore(void* FileHandle, uint64_t Offset, uint64_t Size, bool TransferHandleOwnership);
	IoBufferExtendedCore(const IoBufferExtendedCore* Outer, uint64_t Offset, uint64_t Size);
	~IoBufferExtendedCore();

	enum ExtendedFlags
	{
		kOwnsFile = 1 << 8,
		kOwnsMmap = 1 << 9
	};

	void Materialize() const;
	bool GetFileReference(IoBufferFileReference& OutRef) const;

private:
	void*		  m_FileHandle	  = nullptr;
	uint64_t	  m_FileOffset	  = 0;
	mutable void* m_MmapHandle	  = nullptr;
	mutable void* m_MappedPointer = nullptr;
};

inline IoBufferExtendedCore*
IoBufferCore::ExtendedCore()
{
	if (m_Flags & kIsExtended)
	{
		return static_cast<IoBufferExtendedCore*>(this);
	}

	return nullptr;
}

inline const IoBufferExtendedCore*
IoBufferCore::ExtendedCore() const
{
	if (m_Flags & kIsExtended)
	{
		return static_cast<const IoBufferExtendedCore*>(this);
	}

	return nullptr;
}

/**
 * I/O buffer
 *
 * This represents a reference to a payload in memory or on disk
 *
 */
class IoBuffer
{
public:
	enum EAssumeOwnershipTag
	{
		AssumeOwnership
	};
	enum ECloneTag
	{
		Clone
	};
	enum EWrapTag
	{
		Wrap
	};
	enum EFileTag
	{
		File
	};
	enum EBorrowedFileTag
	{
		BorrowedFile
	};

	inline IoBuffer()						 = default;
	inline IoBuffer(IoBuffer&& Rhs) noexcept = default;
	inline IoBuffer(const IoBuffer& Rhs)	 = default;
	inline IoBuffer& operator=(const IoBuffer& Rhs) = default;
	inline IoBuffer& operator=(IoBuffer&& Rhs) noexcept = default;

	/** Create an uninitialized buffer of the given size
	 */
	ZENCORE_API explicit IoBuffer(size_t InSize);

	/** Create an uninitialized buffer of the given size with the specified alignment
	 */
	ZENCORE_API explicit IoBuffer(size_t InSize, uint64_t InAlignment);

	/** Create a buffer which references a sequence of bytes inside another buffer
	 */
	ZENCORE_API IoBuffer(const IoBuffer& OuterBuffer, size_t Offset, size_t SizeBytes);

	/** Create a buffer which references a range of bytes which we assume will live
	 * for the entire life time.
	 */
	inline IoBuffer(EWrapTag, const void* DataPtr, size_t SizeBytes) : m_Core(new IoBufferCore(DataPtr, SizeBytes)) {}

	inline IoBuffer(ECloneTag, const void* DataPtr, size_t SizeBytes) : m_Core(new IoBufferCore(SizeBytes))
	{
		memcpy(const_cast<void*>(m_Core->DataPointer()), DataPtr, SizeBytes);
	}

	inline IoBuffer(EAssumeOwnershipTag, const void* DataPtr, size_t Sz) : m_Core(new IoBufferCore(DataPtr, Sz))
	{
		m_Core->SetIsOwned(true);
	}

	ZENCORE_API IoBuffer(EFileTag, void* FileHandle, uint64_t ChunkFileOffset, uint64_t ChunkSize);
	ZENCORE_API IoBuffer(EBorrowedFileTag, void* FileHandle, uint64_t ChunkFileOffset, uint64_t ChunkSize);

	inline			 operator bool() const { return !m_Core->IsNull(); }
	ZENCORE_API void MakeOwned() { return m_Core->MakeOwned(); }
	inline bool		 IsOwned() const { return m_Core->IsOwned(); }
	const void*		 Data() const { return m_Core->DataPointer(); }
	const size_t	 Size() const { return m_Core->DataBytes(); }
	ZENCORE_API bool GetFileReference(IoBufferFileReference& OutRef) const;

private:
	RefPtr<IoBufferCore> m_Core = new IoBufferCore;
};

class IoBufferBuilder
{
public:
	ZENCORE_API static IoBuffer MakeFromFile(const wchar_t* FileName, uint64_t Offset = 0, uint64_t Size = ~0ull);
	ZENCORE_API static IoBuffer MakeFromFileHandle(void* FileHandle, uint64_t Offset = 0, uint64_t Size = ~0ull);
	inline static IoBuffer		MakeCloneFromMemory(const void* Ptr, size_t Sz) { return IoBuffer(IoBuffer::Clone, Ptr, Sz); }

private:
};

void iobuffer_forcelink();

}  // namespace zen

