// zencore-test.cpp : Defines the entry point for the console application.
//

#include <zencore/sha1.h>
#include <zencore/zencore.h>

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#undef DOCTEST_CONFIG_IMPLEMENT

void
forceLinkTests()
{
	zencore_forcelinktests();
}

int
main(int argc, char* argv[])
{
	return doctest::Context(argc, argv).run();
}
