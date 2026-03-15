// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include "zencore.h"

#include <zencore/thread.h>

#include <algorithm>
#include <vector>

namespace zen {

class MemoryArena
{
public:
	ZENCORE_API MemoryArena();
	ZENCORE_API ~MemoryArena();

	ZENCORE_API void* Alloc(size_t size, size_t alignment);
	ZENCORE_API void  Free(void* ptr);

private:
};

class Memory
{
public:
	ZENCORE_API static void* Alloc(size_t size, size_t alignment = sizeof(void*));
	ZENCORE_API static void	 Free(void* ptr);
};

/** Allocator which claims fixed-size blocks from the underlying allocator.

	There is no way to free individual memory blocks.

	\note This is not thread-safe, you will need to provide synchronization yourself
*/

class ChunkingLinearAllocator
{
public:
	ChunkingLinearAllocator(uint64_t ChunkSize, uint64_t ChunkAlignment = sizeof(std::max_align_t));
	~ChunkingLinearAllocator();

	ZENCORE_API void Reset();

	ZENCORE_API void* Alloc(size_t Size, size_t Alignment = sizeof(void*));
	inline void		  Free(void* Ptr) { ZEN_UNUSED(Ptr); /* no-op */ }

private:
	uint8_t*		   m_ChunkCursor	  = nullptr;
	uint64_t		   m_ChunkBytesRemain = 0;
	const uint64_t	   m_ChunkSize		  = 0;
	const uint64_t	   m_ChunkAlignment	  = 0;
	std::vector<void*> m_ChunkList;
};

//////////////////////////////////////////////////////////////////////////

struct MutableMemoryView
{
	MutableMemoryView() = default;

	MutableMemoryView(void* DataPtr, size_t DataSize)
	: m_Data(reinterpret_cast<uint8_t*>(DataPtr))
	, m_DataEnd(reinterpret_cast<uint8_t*>(DataPtr) + DataSize)
	{
	}

	inline bool IsEmpty() const { return m_Data == m_DataEnd; }
	void*		GetData() const { return m_Data; }
	void*		GetDataEnd() const { return m_DataEnd; }
	size_t		GetSize() const { return reinterpret_cast<uint8_t*>(m_DataEnd) - reinterpret_cast<uint8_t*>(m_Data); }

	inline bool EqualBytes(const MutableMemoryView& InView) const
	{
		const size_t Size = GetSize();

		return Size == InView.GetSize() && (memcmp(m_Data, InView.m_Data, Size) == 0);
	}

	/** Modifies the view by chopping the given number of bytes from the left. */
	inline void RightChopInline(uint64_t InSize)
	{
		const uint64_t Offset = std::min(GetSize(), InSize);
		m_Data				  = GetDataAtOffsetNoCheck(Offset);
	}

	/** Returns the left-most part of the view by taking the given number of bytes from the left. */
	constexpr inline MutableMemoryView Left(uint64_t InSize) const
	{
		MutableMemoryView View(*this);
		View.LeftInline(InSize);
		return View;
	}

	/** Modifies the view to be the given number of bytes from the left. */
	constexpr inline void LeftInline(uint64_t InSize) { m_DataEnd = std::min(m_DataEnd, m_Data + InSize); }

private:
	uint8_t* m_Data	   = nullptr;
	uint8_t* m_DataEnd = nullptr;

	/** Returns the data pointer advanced by an offset in bytes. */
	inline uint8_t* GetDataAtOffsetNoCheck(uint64_t InOffset) const { return m_Data + InOffset; }
};

struct MemoryView
{
	MemoryView() = default;

	MemoryView(const MutableMemoryView& MutableView)
	: m_Data(reinterpret_cast<const uint8_t*>(MutableView.GetData()))
	, m_DataEnd(m_Data + MutableView.GetSize())
	{
	}

	MemoryView(const void* DataPtr, size_t DataSize)
	: m_Data(reinterpret_cast<const uint8_t*>(DataPtr))
	, m_DataEnd(reinterpret_cast<const uint8_t*>(DataPtr) + DataSize)
	{
	}

	MemoryView(const void* DataPtr, const void* DataEndPtr)
	: m_Data(reinterpret_cast<const uint8_t*>(DataPtr))
	, m_DataEnd(reinterpret_cast<const uint8_t*>(DataEndPtr))
	{
	}

	inline bool Contains(const MemoryView& Other) const { return (m_Data <= Other.m_Data) && (m_DataEnd >= Other.m_DataEnd); }
	inline bool IsEmpty() const { return m_Data == m_DataEnd; }
	const void* GetData() const { return m_Data; }
	const void* GetDataEnd() const { return m_DataEnd; }
	size_t		GetSize() const { return reinterpret_cast<const uint8_t*>(m_DataEnd) - reinterpret_cast<const uint8_t*>(m_Data); }
	inline bool operator==(const MemoryView& Rhs) const { return m_Data == Rhs.m_Data && m_DataEnd == Rhs.m_DataEnd; }

	inline bool EqualBytes(const MemoryView& InView) const
	{
		const size_t Size = GetSize();

		return Size == InView.GetSize() && (memcmp(m_Data, InView.GetData(), Size) == 0);
	}

	inline MemoryView& operator+=(size_t InSize)
	{
		RightChopInline(InSize);
		return *this;
	}

	/** Modifies the view by chopping the given number of bytes from the left. */
	inline void RightChopInline(uint64_t InSize)
	{
		const uint64_t Offset = std::min(GetSize(), InSize);
		m_Data				  = GetDataAtOffsetNoCheck(Offset);
	}

	inline MemoryView RightChop(uint64_t InSize)
	{
		MemoryView View(*this);
		View.RightChopInline(InSize);
		return View;
	}

	/** Returns the left-most part of the view by taking the given number of bytes from the left. */
	constexpr inline MemoryView Left(uint64_t InSize) const
	{
		MemoryView View(*this);
		View.LeftInline(InSize);
		return View;
	}

	/** Modifies the view to be the given number of bytes from the left. */
	constexpr inline void LeftInline(uint64_t InSize) { m_DataEnd = std::min(m_DataEnd, m_Data + InSize); }

	constexpr void Reset()
	{
		m_Data	  = nullptr;
		m_DataEnd = nullptr;
	}

private:
	const uint8_t* m_Data	 = nullptr;
	const uint8_t* m_DataEnd = nullptr;

	/** Returns the data pointer advanced by an offset in bytes. */
	inline const uint8_t* GetDataAtOffsetNoCheck(uint64_t InOffset) const { return m_Data + InOffset; }
};

/**
 * Make a non-owning view of the memory of the initializer list.
 *
 * This overload is only available when the element type does not need to be deduced.
 */
template<typename T>
[[nodiscard]] inline MemoryView
MakeMemoryView(std::initializer_list<typename std::type_identity<T>::type> List)
{
	return MemoryView(List.begin(), List.size() * sizeof(T));
}

/** Make a non-owning view of the memory of the contiguous container. */
template<std::ranges::contiguous_range R>
[[nodiscard]] constexpr inline MemoryView
MakeMemoryView(const R& Container)
{
	const auto& Front = *std::begin(Container);
	return MemoryView(std::addressof(Front), std::size(Container) * sizeof(Front));
}

void memory_forcelink();  // internal

}  // namespace zen

