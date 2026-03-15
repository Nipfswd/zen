// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#pragma warning(push)
#pragma warning(disable : 4267)	 // warning C4267: '=': conversion from 'size_t' to 'US', possible loss of data
#include <cxxopts.hpp>
#pragma warning(pop)

#include <zencore/iobuffer.h>
#include <zencore/refcount.h>
#include <zencore/thread.h>
#include <zencore/windows.h>

#include <atlfile.h>
#include <filesystem>
#include <list>

//////////////////////////////////////////////////////////////////////////

class FileBufferManager : public zen::RefCounted
{
public:
	FileBufferManager(uint64_t BufferSize, uint64_t MaxBufferCount);
	~FileBufferManager();

	zen::IoBuffer AllocBuffer();
	void		  ReturnBuffer(zen::IoBuffer Buffer);

private:
	struct Impl;

	Impl* m_Impl;
};

class InternalFile : public zen::RefCounted
{
public:
	InternalFile();
	~InternalFile();

	void OpenRead(std::filesystem::path FileName);
	void Read(void* Data, uint64_t Size, uint64_t Offset);

	void OpenWrite(std::filesystem::path FileName, bool isCreate);
	void Write(const void* Data, uint64_t Size, uint64_t Offset);

	void  Flush();
	void* Handle() { return m_File; }

	const void* MemoryMapFile();
	size_t		GetFileSize();

private:
	CAtlFile			m_File;
	CAtlFileMappingBase m_Mmap;
	void*				m_Memory = nullptr;
};