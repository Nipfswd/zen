// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

namespace zen {

inline uint16_t
ByteSwap(uint16_t x)
{
	return _byteswap_ushort(x);
}

inline uint32_t
ByteSwap(uint32_t x)
{
	return _byteswap_ulong(x);
}

inline uint64_t
ByteSwap(uint64_t x)
{
	return _byteswap_uint64(x);
}

inline uint16_t
FromNetworkOrder(uint16_t x)
{
	return ByteSwap(x);
}

inline uint32_t
FromNetworkOrder(uint32_t x)
{
	return ByteSwap(x);
}

inline uint64_t
FromNetworkOrder(uint64_t x)
{
	return ByteSwap(x);
}

inline uint16_t
FromNetworkOrder(int16_t x)
{
	return ByteSwap(uint16_t(x));
}

inline uint32_t
FromNetworkOrder(int32_t x)
{
	return ByteSwap(uint32_t(x));
}

inline uint64_t
FromNetworkOrder(int64_t x)
{
	return ByteSwap(uint64_t(x));
}

}  // namespace zen

