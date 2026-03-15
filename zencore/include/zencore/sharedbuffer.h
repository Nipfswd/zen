// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include "zencore.h"

#include <zencore/memory.h>
#include <zencore/refcount.h>

#include <memory.h>

namespace zen {

class BufferOwner : public RefCounted
{
protected:
	inline BufferOwner(void* DataPtr, uint64_t DataSize, bool IsOwned, BufferOwner* OuterBuffer = nullptr)
	: m_IsOwned(IsOwned)
	, m_Data(DataPtr)
	, m_Size(DataSize)
	, m_Outer(OuterBuffer)
	{
	}

	virtual ~BufferOwner();

	// Ownership is a transitive property, and m_IsOwned currently only flags that this instance is responsible
	// for managing the allocated memory, so we need to make recursive calls. Could be optimized slightly by
	// adding a dedicated flag
	inline bool IsOwned() const
	{
		if (m_IsOwned)
		{
			return true;
		}
		else
		{
			return m_Outer && m_Outer->IsOwned();
		}
	}

	BufferOwner(const BufferOwner&) = delete;
	BufferOwner& operator=(const BufferOwner&) = delete;

private:
	bool				m_IsOwned;
	void*				m_Data;
	uint64_t			m_Size;
	RefPtr<BufferOwner> m_Outer;

	friend class UniqueBuffer;
	friend class SharedBuffer;
};

/**
 * Reference to a memory buffer with a single owner (see std::unique_ptr)
 */
class UniqueBuffer
{
public:
	UniqueBuffer(const UniqueBuffer&) = delete;
	UniqueBuffer& operator=(const UniqueBuffer&) = delete;

	UniqueBuffer() = default;
	ZENCORE_API explicit UniqueBuffer(BufferOwner* Owner);

	void*		GetData() { return m_buffer->m_Data; }
	const void* GetData() const { return m_buffer->m_Data; }
	size_t		GetSize() const { return m_buffer->m_Size; }
				operator MutableMemoryView() { return GetView(); }
				operator MemoryView() const { return MemoryView(m_buffer->m_Data, m_buffer->m_Size); }

	MutableMemoryView GetView() { return MutableMemoryView(m_buffer->m_Data, m_buffer->m_Size); }

	/** Make an uninitialized owned buffer of the specified size. */
	ZENCORE_API static UniqueBuffer Alloc(uint64_t Size);

	/** Make a non-owned view of the input. */
	ZENCORE_API static UniqueBuffer MakeView(void* DataPtr, uint64_t Size);

private:
	RefPtr<BufferOwner> m_buffer;

	friend class SharedBuffer;
};

/**
 * Reference to a memory buffer with shared ownership
 */
class SharedBuffer
{
public:
	SharedBuffer() = default;
	ZENCORE_API explicit SharedBuffer(UniqueBuffer&&);
	inline explicit SharedBuffer(BufferOwner* Owner) : m_buffer(Owner) {}

	void* GetData()
	{
		if (m_buffer)
		{
			return m_buffer->m_Data;
		}
		return nullptr;
	}

	const void* GetData() const
	{
		if (m_buffer)
		{
			return m_buffer->m_Data;
		}
		return nullptr;
	}

	size_t GetSize() const
	{
		if (m_buffer)
		{
			return m_buffer->m_Size;
		}
		return 0;
	}

	ZENCORE_API void MakeOwned();
	bool			 IsOwned() const { return m_buffer && m_buffer->IsOwned(); }
	inline explicit	 operator bool() const { return m_buffer; }
	inline bool		 IsNull() const { return !m_buffer; }
	inline void		 Reset() { m_buffer = nullptr; }

	MemoryView GetView() const
	{
		if (m_buffer)
		{
			return MemoryView(m_buffer->m_Data, m_buffer->m_Size);
		}
		else
		{
			return MemoryView();
		}
	}

	operator MemoryView() const { return GetView(); }

	SharedBuffer& operator=(UniqueBuffer&& Rhs)
	{
		m_buffer = std::move(Rhs.m_buffer);
		return *this;
	}

	std::strong_ordering operator<=>(const SharedBuffer& Rhs) const = default;

	/** Make a non-owned view of the input */
	inline static SharedBuffer MakeView(MemoryView View) { return MakeView(View.GetData(), View.GetSize()); }
	/** Make a non-owned view of the input */
	ZENCORE_API static SharedBuffer MakeView(const void* Data, uint64_t Size);
	/** Make a non-owned view of the input */
	ZENCORE_API static SharedBuffer MakeView(MemoryView View, SharedBuffer Buffer);
	/** Make am owned clone of the buffer */
	ZENCORE_API SharedBuffer Clone();
	/** Make an owned clone of the memory in the input view */
	ZENCORE_API static SharedBuffer Clone(MemoryView View);

private:
	RefPtr<BufferOwner> m_buffer;
};

void sharedbuffer_forcelink();

}  // namespace zen

