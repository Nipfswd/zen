// Copyright Noah Games, Inc. All Rights Reserved.

#include <type_traits>
#include "zencore.h"

namespace zen {

template<typename T>
class ScopeGuardImpl
{
public:
	inline ScopeGuardImpl(T&& func) : m_guardFunc(func) {}
	~ScopeGuardImpl()
	{
		if (!m_dismissed)
			m_guardFunc();
	}

	void Dismiss() { m_dismissed = true; }

private:
	bool m_dismissed = false;
	T	 m_guardFunc;
};

template<typename T>
ScopeGuardImpl<T>
MakeGuard(T&& fn)
{
	return ScopeGuardImpl<T>(std::move(fn));
}

}  // namespace zen

