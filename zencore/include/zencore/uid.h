// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <zencore/zencore.h>
#include <compare>

namespace zen {

class StringBuilderBase;

/** Object identifier

	Can be used as a GUID essentially, but is more compact (12 bytes) and as such
	is more susceptible to collisions than a 16-byte GUID but also I don't expect
	the population to be large so in practice the risk should be minimal due to
	how the identifiers work.

	Similar in spirit to MongoDB ObjectId

	When serialized, object identifiers generated in a given session in sequence
	will sort in chronological order since the timestamp is in the MSB in big
	endian format. This makes it suitable as a database key since most indexing
	structures work better when keys are inserted in lexicographically
	increasing order.

	The current layout is basically:

	|----------------|----------------|----------------|
	|   timestamp    |    serial #    |    run id      |
	|----------------|----------------|----------------|
	MSB												 LSB

	- Timestamp is a unsigned 32-bit value (seconds since Jan 1 1970)
	- Serial # is another unsigned 32-bit value which is assigned a (strong)
	  random number at initialization time which is incremented when a new Oid
	  is generated
	- The run id is generated from a strong random number generator
	  at initialization time and stays fixed for the duration of the program

	Timestamp and serial are stored in memory in such a way that they can be
	ordered lexicographically. I.e they are in big-endian byte order.

  */

struct Oid
{
	static const int StringLength = 24;
	typedef char	 String_t[StringLength + 1];

	static void				 Initialize();
	[[nodiscard]] static Oid NewOid();

	const Oid&				 Generate();
	[[nodiscard]] static Oid FromHexString(const std::string_view String);
	StringBuilderBase&		 ToString(StringBuilderBase& OutString) const;

	auto operator<=>(const Oid& rhs) const = default;

	static const Oid Zero;	// Min (can be used to signify a "null" value, or for open range queries)
	static const Oid Max;	// Max (can be used for open range queries)

	struct Hasher
	{
		size_t operator()(const Oid& id) const
		{
			const size_t seed = id.OidBits[0];
			return (seed << 6) + (seed >> 2) + 0x9e3779b9 + uint64_t(id.OidBits[1]) | (uint64_t(id.OidBits[2]) << 32);
		}
	};

	// You should not assume anything about these words
	uint32_t OidBits[3];
};

extern void uid_forcelink();

}  // namespace zen

