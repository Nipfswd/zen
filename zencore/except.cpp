// Copyright Noah Games, Inc. All Rights Reserved.

#include <zencore/except.h>

namespace zen {

void
ThrowSystemException([[maybe_unused]] HRESULT hRes, [[maybe_unused]] const char* Message)
{
	// TODO

	int ErrValue = hRes;

	throw std::system_error(ErrValue, std::system_category(), Message);
}

}  // namespace zen

