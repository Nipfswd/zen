// Copyright Noah Games, Inc. All Rights Reserved.
#pragma once

#include "atomic.h"
#include "zencore.h"

namespace zen {

/**
 * Helper base class for reference counted objects using intrusive reference counts
 *
 * This class is pretty straightforward but does one thing which may be unexpected:
 *
 * - Instances on the stack are initialized with a reference count of one to ensure
 *   nobody tries to accidentally delete it. (TODO: is this really useful?)
 */
class RefCounted
{
public:
	RefCounted() : m_RefCount(IsPointerToStack(this)){};
	virtual ~RefCounted() = default;

	inline uint32_t AddRef() const { return AtomicIncrement(const_cast<RefCounted*>(this)->m_RefCount); }
	inline uint32_t Release() const
	{
		uint32_t refCount = AtomicDecrement(const_cast<RefCounted*>(this)->m_RefCount);
		if (refCount == 0)
		{
			delete this;
		}
		return refCount;
	}

	// Copying reference counted objects doesn't make a lot of sense generally, so let's prevent it

	RefCounted(const RefCounted&) = delete;
	RefCounted(RefCounted&&)	  = delete;
	RefCounted& operator=(const RefCounted&) = delete;
	RefCounted& operator=(RefCounted&&) = delete;

protected:
	inline uint32_t RefCount() const { return m_RefCount; }

private:
	uint32_t m_RefCount = 0;
};

/**
 * Smart pointer for classes derived from RefCounted
 */

template<class T>
class RefPtr
{
public:
	inline RefPtr() = default;
	inline RefPtr(const RefPtr& Rhs) : m_Ref(Rhs.m_Ref) { m_Ref && m_Ref->AddRef(); }
	inline RefPtr(T* Ptr) : m_Ref(Ptr) { m_Ref && m_Ref->AddRef(); }
	inline ~RefPtr() { m_Ref && m_Ref->Release(); }

	inline explicit operator bool() const { return m_Ref != nullptr; }
	inline			operator T*() const { return m_Ref; }
	inline T*		operator->() const { return m_Ref; }

	inline std::strong_ordering operator<=>(const RefPtr& Rhs) const = default;

	inline RefPtr& operator=(T* Rhs)
	{
		Rhs && Rhs->AddRef();
		m_Ref && m_Ref->Release();
		m_Ref = Rhs;
		return *this;
	}
	inline RefPtr& operator=(const RefPtr& Rhs)
	{
		m_Ref && m_Ref->Release();
		auto Ref = m_Ref = Rhs.m_Ref;
		Ref && Ref->AddRef();
		return *this;
	}
	inline RefPtr& operator=(RefPtr&& Rhs) noexcept
	{
		m_Ref && m_Ref->Release();
		m_Ref	  = Rhs.m_Ref;
		Rhs.m_Ref = nullptr;
		return *this;
	}
	inline RefPtr(RefPtr&& Rhs) noexcept : m_Ref(Rhs.m_Ref) { Rhs.m_Ref = nullptr; }

private:
	T* m_Ref = nullptr;
};

/**
 * Smart pointer for classes derived from RefCounted
 *
 * This variant does not decay to a raw pointer
 *
 */

template<class T>
class Ref
{
public:
	inline Ref() = default;
	inline Ref(const Ref& Rhs) : m_Ref(Rhs.m_Ref) { m_Ref && m_Ref->AddRef(); }
	inline Ref(T* Ptr) : m_Ref(Ptr) { m_Ref && m_Ref->AddRef(); }
	inline ~Ref() { m_Ref && m_Ref->Release(); }

	inline explicit operator bool() const { return m_Ref != nullptr; }
	inline T*		operator->() const { return m_Ref; }

	inline std::strong_ordering operator<=>(const Ref& Rhs) const = default;

	inline Ref& operator=(T* Rhs)
	{
		Rhs && Rhs->AddRef();
		m_Ref && m_Ref->Release();
		m_Ref = Rhs;
		return *this;
	}
	inline Ref& operator=(const Ref& Rhs)
	{
		m_Ref && m_Ref->Release();
		auto Ref = m_Ref = Rhs.m_Ref;
		Ref && Ref->AddRef();
		return *this;
	}
	inline Ref& operator=(Ref&& Rhs) noexcept
	{
		m_Ref && m_Ref->Release();
		m_Ref	  = Rhs.m_Ref;
		Rhs.m_Ref = nullptr;
		return *this;
	}
	inline Ref(Ref&& Rhs) noexcept : m_Ref(Rhs.m_Ref) { Rhs.m_Ref = nullptr; }

private:
	T* m_Ref = nullptr;
};

void refcount_forcelink();

}  // namespace zen

