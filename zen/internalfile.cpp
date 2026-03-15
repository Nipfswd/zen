// Copyright Noah Games, Inc. All Rights Reserved.

#include "internalfile.h"

#include <zencore/windows.h>

#include <gsl/gsl-lite.hpp>

#define ZEN_USE_SLIST ZEN_PLATFORM_WINDOWS

#if ZEN_USE_SLIST == 0
struct FileBufferManager::Impl
{
	zen::RwLock				 m_Lock;
	std::list<zen::IoBuffer> m_FreeBuffers;

	uint64_t m_BufferSize;
	uint64_t m_MaxBufferCount;

	Impl(uint64_t BufferSize, uint64_t MaxBuffers) : m_BufferSize(BufferSize), m_MaxBufferCount(MaxBuffers) {}

	zen::IoBuffer AllocBuffer()
	{
		zen::RwLock::ExclusiveLockScope _(m_Lock);

		if (m_FreeBuffers.empty())
		{
			return zen::IoBuffer{m_BufferSize, 64 * 1024};
		}
		else
		{
			zen::IoBuffer Buffer = std::move(m_FreeBuffers.front());
			m_FreeBuffers.pop_front();
			return Buffer;
		}
	}

	void ReturnBuffer(zen::IoBuffer Buffer)
	{
		zen::RwLock::ExclusiveLockScope _(m_Lock);

		m_FreeBuffers.push_front(std::move(Buffer));
	}
};
#else
struct FileBufferManager::Impl
{
	struct BufferItem
	{
		SLIST_ENTRY	  ItemEntry;
		zen::IoBuffer Buffer;
	};

	SLIST_HEADER m_FreeList;
	uint64_t	 m_BufferSize;
	uint64_t	 m_MaxBufferCount;

	Impl(uint64_t BufferSize, uint64_t MaxBuffers) : m_BufferSize(BufferSize), m_MaxBufferCount(MaxBuffers)
	{
		InitializeSListHead(&m_FreeList);
	}

	~Impl()
	{
		while (SLIST_ENTRY* Entry = InterlockedPopEntrySList(&m_FreeList))
		{
			BufferItem* Item = reinterpret_cast<BufferItem*>(Entry);
			delete Item;
		}
	}

	zen::IoBuffer AllocBuffer()
	{
		SLIST_ENTRY* Entry = InterlockedPopEntrySList(&m_FreeList);

		if (Entry == nullptr)
		{
			return zen::IoBuffer{m_BufferSize, 64 * 1024};
		}
		else
		{
			BufferItem*	  Item	 = reinterpret_cast<BufferItem*>(Entry);
			zen::IoBuffer Buffer = std::move(Item->Buffer);
			delete Item;  // Todo: could keep this around in another list

			return Buffer;
		}
	}

	void ReturnBuffer(zen::IoBuffer Buffer)
	{
		BufferItem* Item = new BufferItem{nullptr, std::move(Buffer)};

		InterlockedPushEntrySList(&m_FreeList, &Item->ItemEntry);
	}
};
#endif

FileBufferManager::FileBufferManager(uint64_t BufferSize, uint64_t MaxBuffers)
{
	m_Impl = new Impl{BufferSize, MaxBuffers};
}

FileBufferManager::~FileBufferManager()
{
	delete m_Impl;
}

zen::IoBuffer
FileBufferManager::AllocBuffer()
{
	return m_Impl->AllocBuffer();
}

void
FileBufferManager::ReturnBuffer(zen::IoBuffer Buffer)
{
	return m_Impl->ReturnBuffer(Buffer);
}

//////////////////////////////////////////////////////////////////////////

InternalFile::InternalFile()
{
}

InternalFile::~InternalFile()
{
	if (m_memory)
		_aligned_free(m_memory);
}

size_t
InternalFile::GetFileSize()
{
	ULONGLONG sz;
	m_file.GetSize(sz);

	return size_t(sz);
}

void
InternalFile::OpenWrite(std::filesystem::path FileName, bool IsCreate)
{
	const DWORD dwCreationDisposition = IsCreate ? CREATE_ALWAYS : OPEN_EXISTING;

	HRESULT hRes = m_file.Create(FileName.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, dwCreationDisposition);

	if (FAILED(hRes))
	{
		throw std::system_error(GetLastError(), std::system_category(), "Failed to open file");
	}
}

void
InternalFile::OpenRead(std::filesystem::path FileName)
{
	const DWORD dwCreationDisposition = OPEN_EXISTING;

	HRESULT hRes = m_file.Create(FileName.c_str(), GENERIC_READ, FILE_SHARE_READ, dwCreationDisposition);

	if (FAILED(hRes))
	{
		throw std::system_error(GetLastError(), std::system_category(), "Failed to open file");
	}
}

const void*
InternalFile::MemoryMapFile()
{
	auto fileSize = GetFileSize();

	if (fileSize > 100 * 1024 * 1024)
	{
		m_mmap.MapFile(m_file);

		return m_mmap.GetData();
	}

	m_memory = _aligned_malloc(fileSize, 64);
	Read(m_memory, fileSize, 0);

	return m_memory;
}

void
InternalFile::Read(void* Data, uint64_t Size, uint64_t Offset)
{
	OVERLAPPED ovl{};

	ovl.Offset	   = DWORD(Offset & 0xffff'ffffu);
	ovl.OffsetHigh = DWORD(Offset >> 32);

	HRESULT hRes = m_file.Read(Data, gsl::narrow<DWORD>(Size), &ovl);

	if (FAILED(hRes))
	{
		throw std::system_error(GetLastError(), std::system_category(), "Failed to read from file" /* TODO: add context */);
	}
}

void
InternalFile::Write(const void* Data, uint64_t Size, uint64_t Offset)
{
	OVERLAPPED ovl{};

	ovl.Offset	   = DWORD(Offset & 0xffff'ffffu);
	ovl.OffsetHigh = DWORD(Offset >> 32);

	HRESULT hRes = m_file.Write(Data, gsl::narrow<DWORD>(Size), &ovl);

	if (FAILED(hRes))
	{
		throw std::system_error(GetLastError(), std::system_category(), "Failed to write to file" /* TODO: add context */);
	}
}

void
InternalFile::Flush()
{
	m_file.Flush();
}

