// Copyright Noah Games, Inc. All Rights Reserved.

#include <zencore/refcount.h>

#include <doctest/doctest.h>
#include <functional>

namespace zen {

//////////////////////////////////////////////////////////////////////////
//
// Testing related code follows...
//

struct TestRefClass : public RefCounted
{
	~TestRefClass()
	{
		if (OnDestroy)
			OnDestroy();
	}

	using RefCounted::RefCount;

	std::function<void()> OnDestroy;
};

void
refcount_forcelink()
{
}

TEST_CASE("RefPtr")
{
	RefPtr<TestRefClass> Ref;
	Ref = new TestRefClass;

	bool IsDestroyed = false;
	Ref->OnDestroy	 = [&] { IsDestroyed = true; };

	CHECK(IsDestroyed == false);
	CHECK(Ref->RefCount() == 1);

	RefPtr<TestRefClass> Ref2;
	Ref2 = Ref;

	CHECK(IsDestroyed == false);
	CHECK(Ref->RefCount() == 2);

	RefPtr<TestRefClass> Ref3;
	Ref2 = Ref3;

	CHECK(IsDestroyed == false);
	CHECK(Ref->RefCount() == 1);
	Ref = Ref3;

	CHECK(IsDestroyed == true);
}

TEST_CASE("RefPtr on Stack allocated object")
{
	bool IsDestroyed = false;

	{
		TestRefClass StackRefClass;

		StackRefClass.OnDestroy = [&] { IsDestroyed = true; };

		CHECK(StackRefClass.RefCount() == 1);  // Stack allocated objects should have +1 ref

		RefPtr<TestRefClass> Ref{&StackRefClass};

		CHECK(IsDestroyed == false);
		CHECK(StackRefClass.RefCount() == 2);

		RefPtr<TestRefClass> Ref2;
		Ref2 = Ref;

		CHECK(IsDestroyed == false);
		CHECK(StackRefClass.RefCount() == 3);

		RefPtr<TestRefClass> Ref3;
		Ref2 = Ref3;

		CHECK(IsDestroyed == false);
		CHECK(StackRefClass.RefCount() == 2);

		Ref = Ref3;
		CHECK(IsDestroyed == false);
		CHECK(StackRefClass.RefCount() == 1);
	}

	CHECK(IsDestroyed == true);
}

}  // namespace zen

