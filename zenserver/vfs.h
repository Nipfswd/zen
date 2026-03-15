// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <zencore/httpserver.h>
#include <zenstore/CAS.h>

namespace zen {

/**
 * Virtual File System serving
 */

class Vfs
{
public:
	Vfs();
	~Vfs();

	void Initialize();

	void Start();
	void Stop();

private:
	struct VfsImpl;

	std::unique_ptr<VfsImpl> m_Impl;
};

}  // namespace zen

