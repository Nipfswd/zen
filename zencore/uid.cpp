// Copyright Noah Games, Inc. All Rights Reserved.

#include <zencore/uid.h>

#include <zencore/string.h>

#if _WIN32
#	include <zencore/windows.h>
#	include <bcrypt.h>
#	pragma comment(lib, "bcrypt.lib")
#endif

#include <atomic>
#include <bit>
#include <set>
#include <unordered_map>

#include <doctest/doctest.h>

namespace zen {

//////////////////////////////////////////////////////////////////////////

template<typename T>
T
EndianSwap(T value)
{
	uint8_t dest[sizeof value];

	memcpy(dest, &value, sizeof value);

	for (int i = 0; i < sizeof(value); i++)
	{
		uint8_t& other = dest[sizeof(value) - i - 1];

		uint8_t temp = dest[i];
		dest[i]		 = other;
		other		 = temp;
	}

	T ret;

	memcpy(&ret, &value, sizeof value);

	return ret;
}

#if _WIN32
__forceinline uint16_t
EndianSwap(uint16_t value)
{
	return _byteswap_ushort(value);
}

__forceinline uint32_t
EndianSwap(uint32_t value)
{
	return _byteswap_ulong(value);
}

__forceinline uint64_t
EndianSwap(uint64_t value)
{
	return _byteswap_uint64(value);
}
#endif

//////////////////////////////////////////////////////////////////////////

namespace detail {
	static bool					OidInitialised;
	static uint32_t				RunId;
	static std::atomic_uint32_t Serial;

	// Number of 100 nanosecond units from 1/1/1601 to 1/1/1970 - used for Windows impl
	constexpr int64_t kEpochBias = 116'444'736'000'000'000ull;
}  // namespace detail

//////////////////////////////////////////////////////////////////////////

const Oid Oid::Zero = {{0u, 0u, 0u}};
const Oid Oid::Max	= {{~0u, ~0u, ~0u}};

void
Oid::Initialize()
{
	using namespace detail;

	if (OidInitialised)
		return;

	OidInitialised = true;

#if _WIN32
	char rng[8];
	BCryptGenRandom(NULL, (PUCHAR)rng, sizeof rng, BCRYPT_USE_SYSTEM_PREFERRED_RNG);

	memcpy(&RunId, &rng[0], sizeof(RunId));
	memcpy((void*)&Serial, &rng[4], sizeof(Serial));
#else
#	error Must implement Oid::Initialize
#endif
}

const Oid&
Oid::Generate()
{
	using namespace detail;

	if (!OidInitialised)
	{
		Oid::Initialize();
	}

#if _WIN32
	FILETIME filetime;

	GetSystemTimeAsFileTime(&filetime);	 // Time is UTC

	uint64_t filetime64;
	memcpy(&filetime64, &filetime, sizeof filetime);

	OidBits[0] = EndianSwap(uint32_t((filetime64 - kEpochBias) / 10'000'000l));
	OidBits[1] = EndianSwap(uint32_t(Serial++));
	OidBits[2] = RunId;
#else
#	error Must implement Oid::Generate
#endif

	return *this;
}

Oid
Oid::NewOid()
{
	return Oid().Generate();
}

Oid
Oid::FromHexString(const std::string_view String)
{
	ZEN_ASSERT(String.size() == 2 * sizeof(Oid::OidBits));

	Oid Id;

	ParseHexBytes(String.data(), String.size(), reinterpret_cast<uint8_t*>(Id.OidBits));

	return Id;
}

StringBuilderBase&
Oid::ToString(StringBuilderBase& OutString) const
{
	char str[25];
	ToHexBytes(reinterpret_cast<const uint8_t*>(OidBits), sizeof(Oid::OidBits), str);
	str[2 * sizeof(Oid)] = '\0';

	OutString.AppendRange(str, &str[25]);

	return OutString;
}

TEST_CASE("Oid")
{
	SUBCASE("Basic")
	{
		Oid id1 = Oid::NewOid();

		std::vector<Oid>						  ids;
		std::set<Oid>							  idset;
		std::unordered_map<Oid, int, Oid::Hasher> idmap;

		const int Count = 1000;

		for (int i = 0; i < Count; ++i)
		{
			Oid id;
			id.Generate();

			ids.emplace_back(id);
			idset.insert(id);
			idmap.insert({id, i});
		}

		CHECK(ids.size() == Count);
		CHECK(idset.size() == Count);  // All ids should be unique
		CHECK(idmap.size() == Count);  // Ditto
	}
}

void
uid_forcelink()
{
}

}  // namespace zen

