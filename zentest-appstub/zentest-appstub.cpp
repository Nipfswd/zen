// Copyright Noah Games, Inc. All Rights Reserved.

#include <stdio.h>
#include <cstdlib>
#include <cstring>
#include <thread>

using namespace std::chrono_literals;

int
main(int argc, char* argv[])
{
	for (int i = 0; i < argc; ++i)
	{
		if (std::strncmp(argv[i], "-t=", 3) == 0)
		{
			int sleeptime = std::atoi(argv[i] + 3);

			printf("[zentest] sleeping for %ds!", sleeptime);

			std::this_thread::sleep_for(sleeptime * 1s);
		}
	}
}

