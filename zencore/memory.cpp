// Copyright Noah Games, Inc. All Rights Reserved.

#include <malloc.h>
#include <zencore/intmath.h>
#include <zencore/memory.h>

#include <doctest/doctest.h>

namespace zen {

//////////////////////////////////////////////////////////////////////////

MemoryArena::MemoryArena()
{
}

MemoryArena::~MemoryArena()
{
}

void*
MemoryArena::Alloc(size_t size, size_t alignment)
{
	return _mm_malloc(size, alignment);
}

void
MemoryArena::Free(void* ptr)
{
	if (ptr)
		_mm_free(ptr);
}

//////////////////////////////////////////////////////////////////////////

void*
Memory::Alloc(size_t size, size_t alignment)
{
	return _mm_malloc(size, alignment);
}

void
Memory::Free(void* ptr)
{
	if (ptr)
		_mm_free(ptr);
}

//////////////////////////////////////////////////////////////////////////

ChunkingLinearAllocator::ChunkingLinearAllocator(uint64_t ChunkSize, uint64_t ChunkAlignment)
: m_ChunkSize(ChunkSize)
, m_ChunkAlignment(ChunkAlignment)
{
}

ChunkingLinearAllocator::~ChunkingLinearAllocator()
{
	Reset();
}

void
ChunkingLinearAllocator::Reset()
{
	for (void* ChunkEntry : m_ChunkList)
	{
		Memory::Free(ChunkEntry);
	}
	m_ChunkList.clear();

	m_ChunkCursor	   = nullptr;
	m_ChunkBytesRemain = 0;
}

void*
ChunkingLinearAllocator::Alloc(size_t Size, size_t Alignment)
{
	ZEN_ASSERT_SLOW(zen::IsPow2(Alignment));

	// This could be improved in a bunch of ways
	//
	// * We pessimistically allocate memory even though there may be enough memory available for a single allocation due to the way we take
	//   alignment into account below
	// * The block allocation size could be chosen to minimize slack for the case when multiple oversize allocations are made rather than
	//   minimizing the number of chunks
	// * ...

	const uint64_t AllocationSize = zen::RoundUp(Size, Alignment);

	if (m_ChunkBytesRemain < (AllocationSize + Alignment - 1))
	{
		const uint64_t ChunkSize = zen::RoundUp(zen::Max(m_ChunkSize, Size), m_ChunkSize);
		void*		   ChunkPtr	 = Memory::Alloc(ChunkSize, m_ChunkAlignment);
		m_ChunkCursor			 = reinterpret_cast<uint8_t*>(ChunkPtr);
		m_ChunkBytesRemain		 = ChunkSize;
		m_ChunkList.push_back(ChunkPtr);
	}

	const uint64_t AlignFixup = (Alignment - reinterpret_cast<uintptr_t>(m_ChunkCursor)) & (Alignment - 1);
	void*		   ReturnPtr  = m_ChunkCursor + AlignFixup;
	const uint64_t Delta	  = AlignFixup + AllocationSize;

	ZEN_ASSERT_SLOW(m_ChunkBytesRemain >= Delta);

	m_ChunkCursor += Delta;
	m_ChunkBytesRemain -= Delta;

	ZEN_ASSERT_SLOW(IsPointerAligned(ReturnPtr, Alignment));

	return ReturnPtr;
}

//////////////////////////////////////////////////////////////////////////
//
// Unit tests
//

TEST_CASE("ChunkingLinearAllocator")
{
	ChunkingLinearAllocator Allocator(4096);

	void* p1 = Allocator.Alloc(1, 1);
	void* p2 = Allocator.Alloc(1, 1);

	CHECK(p1 != p2);

	void* p3 = Allocator.Alloc(1, 4);
	CHECK(IsPointerAligned(p3, 4));

	void* p3_2 = Allocator.Alloc(1, 4);
	CHECK(IsPointerAligned(p3_2, 4));

	void* p4 = Allocator.Alloc(1, 8);
	CHECK(IsPointerAligned(p4, 8));

	for (int i = 0; i < 100; ++i)
	{
		void* p0 = Allocator.Alloc(64);
		ZEN_UNUSED(p0);
	}
}

TEST_CASE("MemoryView")
{
	{
		uint8_t	   Array1[16];
		MemoryView View1 = MakeMemoryView(Array1);
		CHECK(View1.GetSize() == 16);
	}

	{
		uint32_t   Array2[16];
		MemoryView View2 = MakeMemoryView(Array2);
		CHECK(View2.GetSize() == 64);
	}

	CHECK(MakeMemoryView<float>({1.0f, 1.2f}).GetSize() == 8);
}

void
memory_forcelink()
{
}

}  // namespace zen

