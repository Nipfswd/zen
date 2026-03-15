// Copyright Noah Games, Inc. All Rights Reserved.

#include <zencore/sharedbuffer.h>

#include <doctest/doctest.h>
#include <memory.h>

#include <atlfile.h>
#include <gsl/gsl-lite.hpp>

namespace zen {

BufferOwner::~BufferOwner()
{
	if (m_IsOwned)
	{
		Memory::Free(m_Data);
	}
}

//////////////////////////////////////////////////////////////////////////

UniqueBuffer
UniqueBuffer::Alloc(uint64_t Size)
{
	void*		 Buffer = Memory::Alloc(Size, 16);
	BufferOwner* Owner	= new BufferOwner(Buffer, Size, /* owned */ true);

	return UniqueBuffer(Owner);
}

UniqueBuffer
UniqueBuffer::MakeView(void* DataPtr, uint64_t Size)
{
	return UniqueBuffer(new BufferOwner(DataPtr, Size, /* owned */ false));
}

UniqueBuffer::UniqueBuffer(BufferOwner* Owner) : m_buffer(Owner)
{
}

//////////////////////////////////////////////////////////////////////////

SharedBuffer::SharedBuffer(UniqueBuffer&& InBuffer) : m_buffer(std::move(InBuffer.m_buffer))
{
}

void
SharedBuffer::MakeOwned()
{
	if (IsOwned() || !m_buffer)
		return;

	const uint64_t Size		= m_buffer->m_Size;
	void*		   Buffer	= Memory::Alloc(Size, 16);
	auto		   NewOwner = new BufferOwner(Buffer, Size, /* owned */ true);

	memcpy(Buffer, m_buffer->m_Data, Size);

	m_buffer = NewOwner;
}

SharedBuffer
SharedBuffer::MakeView(MemoryView View, SharedBuffer Buffer)
{
	// Todo: verify that view is within the shared buffer

	return SharedBuffer(new BufferOwner(const_cast<void*>(View.GetData()), View.GetSize(), /* owned */ false, Buffer.m_buffer));
}

SharedBuffer
SharedBuffer::MakeView(const void* Data, uint64_t Size)
{
	return SharedBuffer(new BufferOwner(const_cast<void*>(Data), Size, /* owned */ false));
}

SharedBuffer
SharedBuffer::Clone()
{
	const uint64_t Size		= GetSize();
	void*		   Buffer	= Memory::Alloc(Size, 16);
	auto		   NewOwner = new BufferOwner(Buffer, Size, /* owned */ true);
	memcpy(Buffer, m_buffer->m_Data, Size);

	return SharedBuffer(NewOwner);
}

SharedBuffer
SharedBuffer::Clone(MemoryView View)
{
	const uint64_t Size		= View.GetSize();
	void*		   Buffer	= Memory::Alloc(Size, 16);
	auto		   NewOwner = new BufferOwner(Buffer, Size, /* owned */ true);
	memcpy(Buffer, View.GetData(), Size);

	return SharedBuffer(NewOwner);
}

//////////////////////////////////////////////////////////////////////////

void
sharedbuffer_forcelink()
{
}

TEST_CASE("SharedBuffer")
{
}

}  // namespace zen

