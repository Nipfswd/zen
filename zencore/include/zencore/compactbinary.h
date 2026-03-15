// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <zencore/zencore.h>

#include <zencore/enumflags.h>
#include <zencore/intmath.h>
#include <zencore/iobuffer.h>
#include <zencore/iohash.h>
#include <zencore/memory.h>
#include <zencore/meta.h>
#include <zencore/sharedbuffer.h>
#include <zencore/uid.h>
#include <zencore/varint.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <gsl/gsl-lite.hpp>

namespace zen {

class CbObjectView;
class CbArrayView;
class BinaryReader;
class BinaryWriter;

class DateTime
{
public:
	explicit DateTime(uint64_t InTicks) : Ticks(InTicks) {}
	inline DateTime(int Year, int Month, int Day, int Hours = 0, int Minutes = 0, int Seconds = 0, int MilliSeconds = 0)
	{
		Set(Year, Month, Day, Hours, Minutes, Seconds, MilliSeconds);
	}

	inline uint64_t GetTicks() const { return Ticks; }
	inline bool		operator==(const DateTime& Rhs) const { return Ticks == Rhs.Ticks; }
	inline auto		operator<=>(const DateTime& Rhs) const { return Ticks - Rhs.Ticks; }

private:
	void	 Set(int Year, int Month, int Day, int Hours, int Minutes, int Seconds, int MilliSecond);
	uint64_t Ticks;
};

class TimeSpan
{
public:
	explicit TimeSpan(uint64_t InTicks) : Ticks(InTicks) {}
	inline TimeSpan(int Hours, int Minutes, int Seconds) { Set(0, Hours, Minutes, Seconds, 0); }
	inline TimeSpan(int Days, int Hours, int Minutes, int Seconds) { Set(Days, Hours, Minutes, Seconds, 0); }
	inline TimeSpan(int Days, int Hours, int Minutes, int Seconds, int Nanos) { Set(Days, Hours, Minutes, Seconds, Nanos); }

	inline uint64_t GetTicks() const { return Ticks; }
	inline bool		operator==(const TimeSpan& Rhs) const { return Ticks == Rhs.Ticks; }
	inline auto		operator<=>(const TimeSpan& Rhs) const { return Ticks - Rhs.Ticks; }

	/**
	 * Time span related constants.
	 */

	/** The maximum number of ticks that can be represented in FTimespan. */
	static constexpr int64_t MaxTicks = 9223372036854775807;

	/** The minimum number of ticks that can be represented in FTimespan. */
	static constexpr int64_t MinTicks = -9223372036854775807 - 1;

	/** The number of nanoseconds per tick. */
	static constexpr int64_t NanosecondsPerTick = 100;

	/** The number of timespan ticks per day. */
	static constexpr int64_t TicksPerDay = 864000000000;

	/** The number of timespan ticks per hour. */
	static constexpr int64_t TicksPerHour = 36000000000;

	/** The number of timespan ticks per microsecond. */
	static constexpr int64_t TicksPerMicrosecond = 10;

	/** The number of timespan ticks per millisecond. */
	static constexpr int64_t TicksPerMillisecond = 10000;

	/** The number of timespan ticks per minute. */
	static constexpr int64_t TicksPerMinute = 600000000;

	/** The number of timespan ticks per second. */
	static constexpr int64_t TicksPerSecond = 10000000;

	/** The number of timespan ticks per week. */
	static constexpr int64_t TicksPerWeek = 6048000000000;

	/** The number of timespan ticks per year (365 days, not accounting for leap years). */
	static constexpr int64_t TicksPerYear = 365 * TicksPerDay;

private:
	void Set(int Days, int Hours, int Minutes, int Seconds, int FractionNano);

	uint64_t Ticks;
};

struct Guid
{
	uint32_t A, B, C, D;
};

//////////////////////////////////////////////////////////////////////////

/**
 * Field types and flags for CbField.
 *
 * This is a private type and is only declared here to enable inline use below.
 *
 * DO NOT CHANGE THE VALUE OF ANY MEMBERS OF THIS ENUM!
 * BACKWARD COMPATIBILITY REQUIRES THAT THESE VALUES BE FIXED!
 * SERIALIZATION USES HARD-CODED CONSTANTS BASED ON THESE VALUES!
 */
enum class CbFieldType : uint8_t
{
	/** A field type that does not occur in a valid object. */
	None = 0x00,

	/** Null. Payload is empty. */
	Null = 0x01,

	/**
	 * Object is an array of fields with unique non-empty names.
	 *
	 * Payload is a VarUInt byte count for the encoded fields followed by the fields.
	 */
	Object = 0x02,
	/**
	 * UniformObject is an array of fields with the same field types and unique non-empty names.
	 *
	 * Payload is a VarUInt byte count for the encoded fields followed by the fields.
	 */
	UniformObject = 0x03,

	/**
	 * Array is an array of fields with no name that may be of different types.
	 *
	 * Payload is a VarUInt byte count, followed by a VarUInt item count, followed by the fields.
	 */
	Array = 0x04,
	/**
	 * UniformArray is an array of fields with no name and with the same field type.
	 *
	 * Payload is a VarUInt byte count, followed by a VarUInt item count, followed by field type,
	 * followed by the fields without their field type.
	 */
	UniformArray = 0x05,

	/** Binary. Payload is a VarUInt byte count followed by the data. */
	Binary = 0x06,

	/** String in UTF-8. Payload is a VarUInt byte count then an unterminated UTF-8 string. */
	String = 0x07,

	/**
	 * Non-negative integer with the range of a 64-bit unsigned integer.
	 *
	 * Payload is the value encoded as a VarUInt.
	 */
	IntegerPositive = 0x08,
	/**
	 * Negative integer with the range of a 64-bit signed integer.
	 *
	 * Payload is the ones' complement of the value encoded as a VarUInt.
	 */
	IntegerNegative = 0x09,

	/** Single precision float. Payload is one big endian IEEE 754 binary32 float. */
	Float32 = 0x0a,
	/** Double precision float. Payload is one big endian IEEE 754 binary64 float. */
	Float64 = 0x0b,

	/** Boolean false value. Payload is empty. */
	BoolFalse = 0x0c,
	/** Boolean true value. Payload is empty. */
	BoolTrue = 0x0d,

	/**
	 * CompactBinaryAttachment is a reference to a compact binary attachment stored externally.
	 *
	 * Payload is a 160-bit hash digest of the referenced compact binary data.
	 */
	CompactBinaryAttachment = 0x0e,
	/**
	 * BinaryAttachment is a reference to a binary attachment stored externally.
	 *
	 * Payload is a 160-bit hash digest of the referenced binary data.
	 */
	BinaryAttachment = 0x0f,

	/** Hash. Payload is a 160-bit hash digest. */
	Hash = 0x10,
	/** UUID/GUID. Payload is a 128-bit UUID as defined by RFC 4122. */
	Uuid = 0x11,

	/**
	 * Date and time between 0001-01-01 00:00:00.0000000 and 9999-12-31 23:59:59.9999999.
	 *
	 * Payload is a big endian int64 count of 100ns ticks since 0001-01-01 00:00:00.0000000.
	 */
	DateTime = 0x12,
	/**
	 * Difference between two date/time values.
	 *
	 * Payload is a big endian int64 count of 100ns ticks in the span, and may be negative.
	 */
	TimeSpan = 0x13,

	/**
	 * Object ID
	 *
	 * Payload is a 12-byte opaque identifier
	 */
	ObjectId = 0x14,

	/**
	 * CustomById identifies the sub-type of its payload by an integer identifier.
	 *
	 * Payload is a VarUInt byte count of the sub-type identifier and the sub-type payload, followed
	 * by a VarUInt of the sub-type identifier then the payload of the sub-type.
	 */
	CustomById = 0x1e,
	/**
	 * CustomByType identifies the sub-type of its payload by a string identifier.
	 *
	 * Payload is a VarUInt byte count of the sub-type identifier and the sub-type payload, followed
	 * by a VarUInt byte count of the unterminated sub-type identifier, then the sub-type identifier
	 * without termination, then the payload of the sub-type.
	 */
	CustomByName = 0x1f,

	/** Reserved for future use as a flag. Do not add types in this range. */
	Reserved = 0x20,

	/**
	 * A transient flag which indicates that the object or array containing this field has stored
	 * the field type before the payload and name. Non-uniform objects and fields will set this.
	 *
	 * Note: Since the flag must never be serialized, this bit may be repurposed in the future.
	 */
	HasFieldType = 0x40,

	/** A persisted flag which indicates that the field has a name stored before the payload. */
	HasFieldName = 0x80,
};

ENUM_CLASS_FLAGS(CbFieldType);

/** Functions that operate on CbFieldType. */
class CbFieldTypeOps
{
	static constexpr CbFieldType SerializedTypeMask = CbFieldType(0b1011'1111);
	static constexpr CbFieldType TypeMask			= CbFieldType(0b0011'1111);
	static constexpr CbFieldType ObjectMask			= CbFieldType(0b0011'1110);
	static constexpr CbFieldType ObjectBase			= CbFieldType(0b0000'0010);
	static constexpr CbFieldType ArrayMask			= CbFieldType(0b0011'1110);
	static constexpr CbFieldType ArrayBase			= CbFieldType(0b0000'0100);
	static constexpr CbFieldType IntegerMask		= CbFieldType(0b0011'1110);
	static constexpr CbFieldType IntegerBase		= CbFieldType(0b0000'1000);
	static constexpr CbFieldType FloatMask			= CbFieldType(0b0011'1100);
	static constexpr CbFieldType FloatBase			= CbFieldType(0b0000'1000);
	static constexpr CbFieldType BoolMask			= CbFieldType(0b0011'1110);
	static constexpr CbFieldType BoolBase			= CbFieldType(0b0000'1100);
	static constexpr CbFieldType AttachmentMask		= CbFieldType(0b0011'1110);
	static constexpr CbFieldType AttachmentBase		= CbFieldType(0b0000'1110);

	static void StaticAssertTypeConstants();

public:
	/** The type with flags removed. */
	static constexpr inline CbFieldType GetType(CbFieldType Type) { return Type & TypeMask; }
	/** The type with transient flags removed. */
	static constexpr inline CbFieldType GetSerializedType(CbFieldType Type) { return Type & SerializedTypeMask; }

	static constexpr inline bool HasFieldType(CbFieldType Type) { return EnumHasAnyFlags(Type, CbFieldType::HasFieldType); }
	static constexpr inline bool HasFieldName(CbFieldType Type) { return EnumHasAnyFlags(Type, CbFieldType::HasFieldName); }

	static constexpr inline bool IsNone(CbFieldType Type) { return GetType(Type) == CbFieldType::None; }
	static constexpr inline bool IsNull(CbFieldType Type) { return GetType(Type) == CbFieldType::Null; }

	static constexpr inline bool IsObject(CbFieldType Type) { return (Type & ObjectMask) == ObjectBase; }
	static constexpr inline bool IsArray(CbFieldType Type) { return (Type & ArrayMask) == ArrayBase; }

	static constexpr inline bool IsBinary(CbFieldType Type) { return GetType(Type) == CbFieldType::Binary; }
	static constexpr inline bool IsString(CbFieldType Type) { return GetType(Type) == CbFieldType::String; }

	static constexpr inline bool IsInteger(CbFieldType Type) { return (Type & IntegerMask) == IntegerBase; }
	/** Whether the field is a float, or integer due to implicit conversion. */
	static constexpr inline bool IsFloat(CbFieldType Type) { return (Type & FloatMask) == FloatBase; }
	static constexpr inline bool IsBool(CbFieldType Type) { return (Type & BoolMask) == BoolBase; }

	static constexpr inline bool IsCompactBinaryAttachment(CbFieldType Type)
	{
		return GetType(Type) == CbFieldType::CompactBinaryAttachment;
	}
	static constexpr inline bool IsBinaryAttachment(CbFieldType Type) { return GetType(Type) == CbFieldType::BinaryAttachment; }
	static constexpr inline bool IsAttachment(CbFieldType Type) { return (Type & AttachmentMask) == AttachmentBase; }

	static constexpr inline bool IsHash(CbFieldType Type)
	{
		switch (GetType(Type))
		{
			case CbFieldType::Hash:
			case CbFieldType::BinaryAttachment:
			case CbFieldType::CompactBinaryAttachment:
				return true;
			default:
				return false;
		}
	}

	static constexpr inline bool IsUuid(CbFieldType Type) { return GetType(Type) == CbFieldType::Uuid; }
	static constexpr inline bool IsObjectId(CbFieldType Type) { return GetType(Type) == CbFieldType::ObjectId; }

	static constexpr inline bool IsDateTime(CbFieldType Type) { return GetType(Type) == CbFieldType::DateTime; }
	static constexpr inline bool IsTimeSpan(CbFieldType Type) { return GetType(Type) == CbFieldType::TimeSpan; }

	/** Whether the type is or may contain fields of any attachment type. */
	static constexpr inline bool MayContainAttachments(CbFieldType Type)
	{
		// The use of !! will suppress V792 from static analysis. Using //-V792 did not work.
		return !!IsObject(Type) | !!IsArray(Type) | !!IsAttachment(Type);
	}
};

/** Errors that can occur when accessing a field. */
enum class CbFieldError : uint8_t
{
	/** The field is not in an error state. */
	None,
	/** The value type does not match the requested type. */
	TypeError,
	/** The value is out of range for the requested type. */
	RangeError,
};

class ICbVisitor
{
public:
	virtual void SetName(std::string_view Name)				= 0;
	virtual void BeginObject()								= 0;
	virtual void EndObject()								= 0;
	virtual void BeginArray()								= 0;
	virtual void EndArray()									= 0;
	virtual void VisitNull()								= 0;
	virtual void VisitBinary(SharedBuffer Value)			= 0;
	virtual void VisitString(std::string_view Value)		= 0;
	virtual void VisitInteger(int64_t Value)				= 0;
	virtual void VisitInteger(uint64_t Value)				= 0;
	virtual void VisitFloat(float Value)					= 0;
	virtual void VisitDouble(double Value)					= 0;
	virtual void VisitBool(bool value)						= 0;
	virtual void VisitCbAttachment(const IoHash& Value)		= 0;
	virtual void VisitBinaryAttachment(const IoHash& Value) = 0;
	virtual void VisitHash(const IoHash& Value)				= 0;
	virtual void VisitUuid(const Guid& Value)				= 0;
	virtual void VisitObjectId(const Oid& Value)			= 0;
	virtual void VisitDateTime(DateTime Value)				= 0;
	virtual void VisitTimeSpan(TimeSpan Value)				= 0;
};

/**
 * An atom of data in the compact binary format.
 *
 * Accessing the value of a field is always a safe operation, even if accessed as the wrong type.
 * An invalid access will return a default value for the requested type, and set an error code on
 * the field that can be checked with GetLastError and HasLastError. A valid access will clear an
 * error from a previous invalid access.
 *
 * A field is encoded in one or more bytes, depending on its type and the type of object or array
 * that contains it. A field of an object or array which is non-uniform encodes its field type in
 * the first byte, and includes the HasFieldName flag for a field in an object. The field name is
 * encoded in a variable-length unsigned integer of its size in bytes, for named fields, followed
 * by that many bytes of the UTF-8 encoding of the name with no null terminator. The remainder of
 * the field is the payload and is described in the field type enum. Every field must be uniquely
 * addressable when encoded, which means a zero-byte field is not permitted, and only arises in a
 * uniform array of fields with no payload, where the answer is to encode as a non-uniform array.
 *
 * This type only provides a view into memory and does not perform any memory management itself.
 * Use CbFieldRef to hold a reference to the underlying memory when necessary.
 */

class CbFieldView
{
public:
	CbFieldView() = default;

	ZENCORE_API CbFieldView(const void* DataPointer, CbFieldType FieldType = CbFieldType::HasFieldType);

	/** Returns the name of the field if it has a name, otherwise an empty view. */
	constexpr inline std::string_view GetName() const { return std::string_view(static_cast<const char*>(Payload) - NameLen, NameLen); }

	ZENCORE_API MemoryView	 AsBinaryView(MemoryView Default = MemoryView());
	ZENCORE_API CbObjectView AsObjectView();
	ZENCORE_API CbArrayView	 AsArrayView();
	ZENCORE_API std::string_view AsString(std::string_view Default = std::string_view());

	ZENCORE_API void IterateAttachments(std::function<void(CbFieldView)> Visitor) const;

	/** Access the field as an int8. Returns the provided default on error. */
	inline int8_t AsInt8(int8_t Default = 0) { return AsInteger<int8_t>(Default); }
	/** Access the field as an int16. Returns the provided default on error. */
	inline int16_t AsInt16(int16_t Default = 0) { return AsInteger<int16_t>(Default); }
	/** Access the field as an int32. Returns the provided default on error. */
	inline int32_t AsInt32(int32_t Default = 0) { return AsInteger<int32_t>(Default); }
	/** Access the field as an int64. Returns the provided default on error. */
	inline int64_t AsInt64(int64_t Default = 0) { return AsInteger<int64_t>(Default); }
	/** Access the field as a uint8. Returns the provided default on error. */
	inline uint8_t AsUInt8(uint8_t Default = 0) { return AsInteger<uint8_t>(Default); }
	/** Access the field as a uint16. Returns the provided default on error. */
	inline uint16_t AsUInt16(uint16_t Default = 0) { return AsInteger<uint16_t>(Default); }
	/** Access the field as a uint32. Returns the provided default on error. */
	inline uint32_t AsUInt32(uint32_t Default = 0) { return AsInteger<uint32_t>(Default); }
	/** Access the field as a uint64. Returns the provided default on error. */
	inline uint64_t AsUInt64(uint64_t Default = 0) { return AsInteger<uint64_t>(Default); }

	/** Access the field as a float. Returns the provided default on error. */
	ZENCORE_API float AsFloat(float Default = 0.0f);
	/** Access the field as a double. Returns the provided default on error. */
	ZENCORE_API double AsDouble(double Default = 0.0);

	/** Access the field as a bool. Returns the provided default on error. */
	ZENCORE_API bool AsBool(bool bDefault = false);

	/** Access the field as a hash referencing a compact binary attachment. Returns the provided default on error. */
	ZENCORE_API IoHash AsCompactBinaryAttachment(const IoHash& Default = IoHash());
	/** Access the field as a hash referencing a binary attachment. Returns the provided default on error. */
	ZENCORE_API IoHash AsBinaryAttachment(const IoHash& Default = IoHash());
	/** Access the field as a hash referencing an attachment. Returns the provided default on error. */
	ZENCORE_API IoHash AsAttachment(const IoHash& Default = IoHash());

	/** Access the field as a hash. Returns the provided default on error. */
	ZENCORE_API IoHash AsHash(const IoHash& Default = IoHash());

	/** Access the field as a UUID. Returns a nil UUID on error. */
	ZENCORE_API Guid AsUuid();
	/** Access the field as a UUID. Returns the provided default on error. */
	ZENCORE_API Guid AsUuid(const Guid& Default);

	/** Access the field as an OID. Returns a nil OID on error. */
	ZENCORE_API Oid AsObjectId();
	/** Access the field as a OID. Returns the provided default on error. */
	ZENCORE_API Oid AsObjectId(const Oid& Default);

	/** Access the field as a date/time tick count. Returns the provided default on error. */
	ZENCORE_API int64_t AsDateTimeTicks(int64_t Default = 0);

	/** Access the field as a date/time. Returns a date/time at the epoch on error. */
	ZENCORE_API DateTime AsDateTime();
	/** Access the field as a date/time. Returns the provided default on error. */
	ZENCORE_API DateTime AsDateTime(DateTime Default);

	/** Access the field as a timespan tick count. Returns the provided default on error. */
	ZENCORE_API int64_t AsTimeSpanTicks(int64_t Default = 0);

	/** Access the field as a timespan. Returns an empty timespan on error. */
	ZENCORE_API TimeSpan AsTimeSpan();
	/** Access the field as a timespan. Returns the provided default on error. */
	ZENCORE_API TimeSpan AsTimeSpan(TimeSpan Default);

	/** True if the field has a name. */
	constexpr inline bool HasName() const { return CbFieldTypeOps::HasFieldName(Type); }

	constexpr inline bool IsNull() const { return CbFieldTypeOps::IsNull(Type); }

	constexpr inline bool IsObject() const { return CbFieldTypeOps::IsObject(Type); }
	constexpr inline bool IsArray() const { return CbFieldTypeOps::IsArray(Type); }

	constexpr inline bool IsBinary() const { return CbFieldTypeOps::IsBinary(Type); }
	constexpr inline bool IsString() const { return CbFieldTypeOps::IsString(Type); }

	/** Whether the field is an integer of unspecified range and sign. */
	constexpr inline bool IsInteger() const { return CbFieldTypeOps::IsInteger(Type); }
	/** Whether the field is a float, or integer that supports implicit conversion. */
	constexpr inline bool IsFloat() const { return CbFieldTypeOps::IsFloat(Type); }
	constexpr inline bool IsBool() const { return CbFieldTypeOps::IsBool(Type); }

	constexpr inline bool IsCompactBinaryAttachment() const { return CbFieldTypeOps::IsCompactBinaryAttachment(Type); }
	constexpr inline bool IsBinaryAttachment() const { return CbFieldTypeOps::IsBinaryAttachment(Type); }
	constexpr inline bool IsAttachment() const { return CbFieldTypeOps::IsAttachment(Type); }

	constexpr inline bool IsHash() const { return CbFieldTypeOps::IsHash(Type); }
	constexpr inline bool IsUuid() const { return CbFieldTypeOps::IsUuid(Type); }
	constexpr inline bool IsObjectId() const { return CbFieldTypeOps::IsObjectId(Type); }

	constexpr inline bool IsDateTime() const { return CbFieldTypeOps::IsDateTime(Type); }
	constexpr inline bool IsTimeSpan() const { return CbFieldTypeOps::IsTimeSpan(Type); }

	/** Whether the field has a value. */
	constexpr inline explicit operator bool() const { return HasValue(); }

	/**
	 * Whether the field has a value.
	 *
	 * All fields in a valid object or array have a value. A field with no value is returned when
	 * finding a field by name fails or when accessing an iterator past the end.
	 */
	constexpr inline bool HasValue() const { return !CbFieldTypeOps::IsNone(Type); };

	/** Whether the last field access encountered an error. */
	constexpr inline bool HasError() const { return Error != CbFieldError::None; }

	/** The type of error that occurred on the last field access, or None. */
	constexpr inline CbFieldError GetError() const { return Error; }

	/** Returns the size of the field in bytes, including the type and name. */
	ZENCORE_API uint64_t GetSize() const;

	/** Calculate the hash of the field, including the type and name. */
	ZENCORE_API IoHash GetHash() const;

	ZENCORE_API void GetHash(IoHashStream& HashStream) const;

	/** Feed the field (including type and name) to the stream function */
	inline void WriteToStream(auto Hash) const
	{
		const CbFieldType SerializedType = CbFieldTypeOps::GetSerializedType(Type);
		Hash(&SerializedType, sizeof(SerializedType));
		auto View = GetViewNoType();
		Hash(View.GetData(), View.GetSize());
	}

	/** Copy the field into a buffer of exactly GetSize() bytes, including the type and name. */
	ZENCORE_API void CopyTo(MutableMemoryView Buffer) const;

	/** Copy the field into an archive, including its type and name. */
	ZENCORE_API void CopyTo(BinaryWriter& Ar) const;

	/**
	 * Whether this field is identical to the other field.
	 *
	 * Performs a deep comparison of any contained arrays or objects and their fields. Comparison
	 * assumes that both fields are valid and are written in the canonical format. Fields must be
	 * written in the same order in arrays and objects, and name comparison is case sensitive. If
	 * these assumptions do not hold, this may return false for equivalent inputs. Validation can
	 * be performed with ValidateCompactBinary, except for field order and field name case.
	 */
	ZENCORE_API bool Equals(const CbFieldView& Other) const;

	/** Returns a view of the field, including the type and name when present. */
	ZENCORE_API MemoryView GetView() const;

	/**
	 * Try to get a view of the field as it would be serialized, such as by CopyTo.
	 *
	 * A serialized view is not available if the field has an externally-provided type.
	 * Access the serialized form of such fields using CopyTo or FCbFieldRef::Clone.
	 */
	inline bool TryGetSerializedView(MemoryView& OutView) const
	{
		if (CbFieldTypeOps::HasFieldType(Type))
		{
			OutView = GetView();
			return true;
		}
		return false;
	}

protected:
	/** Returns a view of the name and value payload, which excludes the type. */
	ZENCORE_API MemoryView GetViewNoType() const;

	/** Returns a view of the value payload, which excludes the type and name. */
	inline MemoryView GetPayloadView() const { return MemoryView(Payload, GetPayloadSize()); }

	/** Returns the type of the field including flags. */
	constexpr inline CbFieldType GetType() const { return Type; }

	/** Returns the start of the value payload. */
	constexpr inline const void* GetPayload() const { return Payload; }

	/** Returns the end of the value payload. */
	inline const void* GetPayloadEnd() const { return static_cast<const uint8_t*>(Payload) + GetPayloadSize(); }

	/** Returns the size of the value payload in bytes, which is the field excluding the type and name. */
	ZENCORE_API uint64_t GetPayloadSize() const;

	/** Assign a field from a pointer to its data and an optional externally-provided type. */
	inline void Assign(const void* InData, const CbFieldType InType)
	{
		static_assert(std::is_trivially_destructible<CbFieldView>::value,
					  "This optimization requires CbField to be trivially destructible!");
		new (this) CbFieldView(InData, InType);
	}

private:
	/** Parameters for converting to an integer. */
	struct IntegerParams
	{
		/** Whether the output type has a sign bit. */
		uint32_t IsSigned : 1;
		/** Bits of magnitude. (7 for int8) */
		uint32_t MagnitudeBits : 31;
	};

	/** Make integer params for the given integer type. */
	template<typename IntType>
	static constexpr inline IntegerParams MakeIntegerParams()
	{
		IntegerParams Params;
		Params.IsSigned		 = IntType(-1) < IntType(0);
		Params.MagnitudeBits = 8 * sizeof(IntType) - Params.IsSigned;
		return Params;
	}

	/**
	 * Access the field as the given integer type.
	 *
	 * Returns the provided default if the value cannot be represented in the output type.
	 */
	template<typename IntType>
	inline IntType AsInteger(IntType Default)
	{
		return IntType(AsInteger(uint64_t(Default), MakeIntegerParams<IntType>()));
	}

	ZENCORE_API uint64_t AsInteger(uint64_t Default, IntegerParams Params);

	/** The field type, with the transient HasFieldType flag if the field contains its type. */
	CbFieldType Type = CbFieldType::None;
	/** The error (if any) that occurred on the last field access. */
	CbFieldError Error = CbFieldError::None;
	/** The number of bytes for the name stored before the payload. */
	uint32_t NameLen = 0;
	/** The value payload, which also points to the end of the name. */
	const void* Payload = nullptr;
};

template<typename FieldType>
class TCbFieldIterator : public FieldType
{
public:
	/** Construct an empty field range. */
	constexpr TCbFieldIterator() = default;

	inline TCbFieldIterator& operator++()
	{
		const void* const PayloadEnd	= FieldType::GetPayloadEnd();
		const int64_t	  AtEndMask		= int64_t(PayloadEnd == FieldsEnd) - 1;
		const CbFieldType NextType		= CbFieldType(int64_t(FieldType::GetType()) & AtEndMask);
		const void* const NextField		= reinterpret_cast<const void*>(int64_t(PayloadEnd) & AtEndMask);
		const void* const NextFieldsEnd = reinterpret_cast<const void*>(int64_t(FieldsEnd) & AtEndMask);

		FieldType::Assign(NextField, NextType);
		FieldsEnd = NextFieldsEnd;
		return *this;
	}

	inline TCbFieldIterator operator++(int)
	{
		TCbFieldIterator It(*this);
		++*this;
		return It;
	}

	constexpr inline FieldType& operator*() { return *this; }
	constexpr inline FieldType* operator->() { return this; }

	/** Reset this to an empty field range. */
	inline void Reset() { *this = TCbFieldIterator(); }

	/** Returns the size of the fields in the range in bytes. */
	ZENCORE_API uint64_t GetRangeSize() const;

	/** Calculate the hash of every field in the range. */
	ZENCORE_API IoHash GetRangeHash() const;
	ZENCORE_API void   GetRangeHash(IoHashStream& Hash) const;

	using FieldType::Equals;

	template<typename OtherFieldType>
	constexpr inline bool Equals(const TCbFieldIterator<OtherFieldType>& Other) const
	{
		return FieldType::GetPayload() == Other.OtherFieldType::GetPayload() && FieldsEnd == Other.FieldsEnd;
	}

	template<typename OtherFieldType>
	constexpr inline bool operator==(const TCbFieldIterator<OtherFieldType>& Other) const
	{
		return Equals(Other);
	}

	template<typename OtherFieldType>
	constexpr inline bool operator!=(const TCbFieldIterator<OtherFieldType>& Other) const
	{
		return !Equals(Other);
	}

	/** Copy the field range into a buffer of exactly GetRangeSize() bytes. */
	ZENCORE_API void CopyRangeTo(MutableMemoryView Buffer) const;

	/** Invoke the visitor for every attachment in the field range. */
	ZENCORE_API void IterateRangeAttachments(std::function<void(CbFieldView)> Visitor) const;

	/** Create a view of every field in the range. */
	inline MemoryView GetRangeView() const { return MemoryView(FieldType::GetView().GetData(), FieldsEnd); }

	/**
	 * Try to get a view of every field in the range as they would be serialized.
	 *
	 * A serialized view is not available if the underlying fields have an externally-provided type.
	 * Access the serialized form of such ranges using CbFieldRefIterator::CloneRange.
	 */
	inline bool TryGetSerializedRangeView(MemoryView& OutView) const
	{
		if (CbFieldTypeOps::HasFieldType(FieldType::GetType()))
		{
			OutView = GetRangeView();
			return true;
		}
		return false;
	}

protected:
	/** Construct a field range that contains exactly one field. */
	constexpr inline explicit TCbFieldIterator(FieldType InField) : FieldType(std::move(InField)), FieldsEnd(FieldType::GetPayloadEnd()) {}

	/**
	 * Construct a field range from the first field and a pointer to the end of the last field.
	 *
	 * @param InField The first field, or the default field if there are no fields.
	 * @param InFieldsEnd A pointer to the end of the payload of the last field, or null.
	 */
	constexpr inline TCbFieldIterator(FieldType&& InField, const void* InFieldsEnd) : FieldType(std::move(InField)), FieldsEnd(InFieldsEnd)
	{
	}

	/** Returns the end of the last field, or null for an iterator at the end. */
	template<typename OtherFieldType>
	static inline const void* GetFieldsEnd(const TCbFieldIterator<OtherFieldType>& It)
	{
		return It.FieldsEnd;
	}

private:
	friend inline TCbFieldIterator begin(const TCbFieldIterator& Iterator) { return Iterator; }
	friend inline TCbFieldIterator end(const TCbFieldIterator&) { return TCbFieldIterator(); }

private:
	template<typename OtherType>
	friend class TCbFieldIterator;

	friend class CbFieldViewIterator;

	/** Pointer to the first byte past the end of the last field. Set to null at the end. */
	const void* FieldsEnd = nullptr;
};

/**
 * Iterator for CbField.
 *
 * @see CbFieldIterator
 */
class CbFieldViewIterator : public TCbFieldIterator<CbFieldView>
{
public:
	constexpr CbFieldViewIterator() = default;

	/** Construct a field range that contains exactly one field. */
	static inline CbFieldViewIterator MakeSingle(const CbFieldView& Field) { return CbFieldViewIterator(Field); }

	/**
	 * Construct a field range from a buffer containing zero or more valid fields.
	 *
	 * @param View A buffer containing zero or more valid fields.
	 * @param Type HasFieldType means that View contains the type. Otherwise, use the given type.
	 */
	static inline CbFieldViewIterator MakeRange(MemoryView View, CbFieldType Type = CbFieldType::HasFieldType)
	{
		return !View.IsEmpty() ? TCbFieldIterator(CbFieldView(View.GetData(), Type), View.GetDataEnd()) : CbFieldViewIterator();
	}

	/** Construct an iterator from another iterator. */
	template<typename OtherFieldType>
	inline CbFieldViewIterator(const TCbFieldIterator<OtherFieldType>& It)
	: TCbFieldIterator(ImplicitConv<CbFieldView>(It), GetFieldsEnd(It))
	{
	}

private:
	using TCbFieldIterator::TCbFieldIterator;
};

/**
 * Array of CbField that have no names.
 *
 * Accessing a field of the array requires iteration. Access by index is not provided because the
 * cost of accessing an item by index scales linearly with the index.
 *
 * This type only provides a view into memory and does not perform any memory management itself.
 * Use CbArrayRef to hold a reference to the underlying memory when necessary.
 */
class CbArrayView : protected CbFieldView
{
	friend class CbFieldView;

public:
	/** @see CbField::CbField */
	using CbFieldView::CbFieldView;

	/** Construct an array with no fields. */
	ZENCORE_API CbArrayView();

	/** Returns the number of items in the array. */
	ZENCORE_API uint64_t Num() const;

	/** Create an iterator for the fields of this array. */
	ZENCORE_API CbFieldViewIterator CreateViewIterator() const;

	/** Visit the fields of this array. */
	ZENCORE_API void VisitFields(ICbVisitor& Visitor);

	/** Access the array as an array field. */
	inline CbFieldView AsFieldView() const { return static_cast<const CbFieldView&>(*this); }

	/** Construct an array from an array field. No type check is performed! */
	static inline CbArrayView FromFieldView(const CbFieldView& Field) { return CbArrayView(Field); }

	/** Returns the size of the array in bytes if serialized by itself with no name. */
	ZENCORE_API uint64_t GetSize() const;

	/** Calculate the hash of the array if serialized by itself with no name. */
	ZENCORE_API IoHash GetHash() const;

	ZENCORE_API void GetHash(IoHashStream& Stream) const;

	/**
	 * Whether this array is identical to the other array.
	 *
	 * Performs a deep comparison of any contained arrays or objects and their fields. Comparison
	 * assumes that both fields are valid and are written in the canonical format. Fields must be
	 * written in the same order in arrays and objects, and name comparison is case sensitive. If
	 * these assumptions do not hold, this may return false for equivalent inputs. Validation can
	 * be done with the All mode to check these assumptions about the format of the inputs.
	 */
	ZENCORE_API bool Equals(const CbArrayView& Other) const;

	/** Copy the array into a buffer of exactly GetSize() bytes, with no name. */
	ZENCORE_API void CopyTo(MutableMemoryView Buffer) const;

	/** Copy the array into an archive, including its type and name. */
	ZENCORE_API void CopyTo(BinaryWriter& Ar) const;

	///** Invoke the visitor for every attachment in the array. */
	inline void IterateAttachments(std::function<void(CbFieldView)> Visitor) const
	{
		CreateViewIterator().IterateRangeAttachments(Visitor);
	}

	/** Returns a view of the array, including the type and name when present. */
	using CbFieldView::GetView;

private:
	friend inline CbFieldViewIterator begin(const CbArrayView& Array) { return Array.CreateViewIterator(); }
	friend inline CbFieldViewIterator end(const CbArrayView&) { return CbFieldViewIterator(); }

	/** Construct an array from an array field. No type check is performed! Use via FromField. */
	inline explicit CbArrayView(const CbFieldView& Field) : CbFieldView(Field) {}
};

class CbObjectView : protected CbFieldView
{
	friend class CbFieldView;

public:
	/** @see CbField::CbField */
	using CbFieldView::CbFieldView;

	/** Construct an object with no fields. */
	ZENCORE_API CbObjectView();

	/** Create an iterator for the fields of this object. */
	ZENCORE_API CbFieldViewIterator CreateViewIterator() const;

	/** Visit the fields of this object. */
	ZENCORE_API void VisitFields(ICbVisitor& Visitor);

	/**
	 * Find a field by case-sensitive name comparison.
	 *
	 * The cost of this operation scales linearly with the number of fields in the object. Prefer
	 * to iterate over the fields only once when consuming an object.
	 *
	 * @param Name The name of the field.
	 * @return The matching field if found, otherwise a field with no value.
	 */
	ZENCORE_API CbFieldView FindView(std::string_view Name) const;

	/** Find a field by case-insensitive name comparison. */
	ZENCORE_API CbFieldView FindViewIgnoreCase(std::string_view Name) const;

	/** Find a field by case-sensitive name comparison. */
	inline CbFieldView operator[](std::string_view Name) const { return FindView(Name); }

	/** Access the object as an object field. */
	inline CbFieldView AsFieldView() const { return static_cast<const CbFieldView&>(*this); }

	/** Construct an object from an object field. No type check is performed! */
	static inline CbObjectView FromFieldView(const CbFieldView& Field) { return CbObjectView(Field); }

	/** Returns the size of the object in bytes if serialized by itself with no name. */
	ZENCORE_API uint64_t GetSize() const;

	/** Calculate the hash of the object if serialized by itself with no name. */
	ZENCORE_API IoHash GetHash() const;

	ZENCORE_API void GetHash(IoHashStream& HashStream) const;

	/**
	 * Whether this object is identical to the other object.
	 *
	 * Performs a deep comparison of any contained arrays or objects and their fields. Comparison
	 * assumes that both fields are valid and are written in the canonical format. Fields must be
	 * written in the same order in arrays and objects, and name comparison is case sensitive. If
	 * these assumptions do not hold, this may return false for equivalent inputs. Validation can
	 * be done with the All mode to check these assumptions about the format of the inputs.
	 */
	ZENCORE_API bool Equals(const CbObjectView& Other) const;

	/** Copy the object into a buffer of exactly GetSize() bytes, with no name. */
	ZENCORE_API void CopyTo(MutableMemoryView Buffer) const;

	/** Copy the field into an archive, including its type and name. */
	ZENCORE_API void CopyTo(BinaryWriter& Ar) const;

	///** Invoke the visitor for every attachment in the object. */
	inline void IterateAttachments(std::function<void(CbFieldView)> Visitor) const
	{
		CreateViewIterator().IterateRangeAttachments(Visitor);
	}

	/** Returns a view of the object, including the type and name when present. */
	using CbFieldView::GetView;

	/** Whether the field has a value. */
	using CbFieldView::operator bool;

private:
	friend inline CbFieldViewIterator begin(const CbObjectView& Object) { return Object.CreateViewIterator(); }
	friend inline CbFieldViewIterator end(const CbObjectView&) { return CbFieldViewIterator(); }

	/** Construct an object from an object field. No type check is performed! Use via FromField. */
	inline explicit CbObjectView(const CbFieldView& Field) : CbFieldView(Field) {}
};

//////////////////////////////////////////////////////////////////////////

/** A reference to a function that is used to allocate buffers for compact binary data. */
using BufferAllocator = std::function<UniqueBuffer(uint64_t Size)>;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** A wrapper that holds a reference to the buffer that contains its compact binary value. */
template<typename BaseType>
class CbBuffer : public BaseType
{
public:
	/** Construct a default value. */
	CbBuffer() = default;

	/**
	 * Construct a value from a pointer to its data and an optional externally-provided type.
	 *
	 * @param ValueBuffer A buffer that exactly contains the value.
	 * @param Type HasFieldType means that ValueBuffer contains the type. Otherwise, use the given type.
	 */
	inline explicit CbBuffer(SharedBuffer ValueBuffer, CbFieldType Type = CbFieldType::HasFieldType)
	{
		if (ValueBuffer)
		{
			BaseType::operator=(BaseType(ValueBuffer.GetData(), Type));
			ZEN_ASSERT(ValueBuffer.GetView().Contains(BaseType::GetView()));
			Buffer = std::move(ValueBuffer);
		}
	}

	/** Construct a value that holds a reference to the buffer that contains it. */
	inline CbBuffer(const BaseType& Value, SharedBuffer OuterBuffer) : BaseType(Value)
	{
		if (OuterBuffer)
		{
			ZEN_ASSERT(OuterBuffer.GetView().Contains(BaseType::GetView()));
			Buffer = std::move(OuterBuffer);
		}
	}

	/** Construct a value that holds a reference to the buffer of the outer that contains it. */
	template<typename OtherBaseType>
	inline CbBuffer(const BaseType& Value, CbBuffer<OtherBaseType> OuterRef) : CbBuffer(Value, std::move(OuterRef.Buffer))
	{
	}

	/** Reset this to a default value and null buffer. */
	inline void Reset() { *this = CbBuffer(); }

	/** Whether this reference has ownership of the memory in its buffer. */
	inline bool IsOwned() const { return Buffer && Buffer.IsOwned(); }

	/** Clone the value, if necessary, to a buffer that this reference has ownership of. */
	inline void MakeOwned()
	{
		if (!IsOwned())
		{
			UniqueBuffer MutableBuffer = UniqueBuffer::Alloc(BaseType::GetSize());
			BaseType::CopyTo(MutableBuffer);
			BaseType::operator=(BaseType(MutableBuffer.GetData()));
			Buffer			  = std::move(MutableBuffer);
		}
	}

	/** Returns a buffer that exactly contains this value. */
	inline SharedBuffer GetBuffer() const
	{
		const MemoryView	View		= BaseType::GetView();
		const SharedBuffer& OuterBuffer = GetOuterBuffer();
		return View == OuterBuffer.GetView() ? OuterBuffer : SharedBuffer::MakeView(View, OuterBuffer);
	}

	/** Returns the outer buffer (if any) that contains this value. */
	inline const SharedBuffer& GetOuterBuffer() const& { return Buffer; }
	inline SharedBuffer		   GetOuterBuffer() && { return std::move(Buffer); }

private:
	template<typename OtherType>
	friend class CbBuffer;

	SharedBuffer Buffer;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Factory functions for types derived from CbBuffer.
 *
 * This uses the curiously recurring template pattern to construct the correct type of reference.
 * The derived type inherits from CbBufferRef and this type to expose the factory functions.
 */
template<typename RefType, typename BaseType>
class CbBufferFactory
{
public:
	/** Construct a value from an owned clone of its memory. */
	static inline RefType Clone(const void* const Data) { return Clone(BaseType(Data)); }

	/** Construct a value from an owned clone of its memory. */
	static inline RefType Clone(const BaseType& Value)
	{
		RefType Ref = MakeView(Value);
		Ref.MakeOwned();
		return Ref;
	}

	/** Construct a value from a read-only view of its memory and its optional outer buffer. */
	static inline RefType MakeView(const void* const Data, SharedBuffer OuterBuffer = SharedBuffer())
	{
		return MakeView(BaseType(Data), std::move(OuterBuffer));
	}

	/** Construct a value from a read-only view of its memory and its optional outer buffer. */
	static inline RefType MakeView(const BaseType& Value, SharedBuffer OuterBuffer = SharedBuffer())
	{
		return RefType(Value, std::move(OuterBuffer));
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class CbArray;
class CbObject;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * A field that can hold a reference to the memory that contains it.
 *
 * @see CbBufferRef
 */
class CbField : public CbBuffer<CbFieldView>, public CbBufferFactory<CbField, CbFieldView>
{
public:
	using CbBuffer::CbBuffer;

	/** Access the field as an object. Defaults to an empty object on error. */
	inline CbObject AsObject() &;

	/** Access the field as an object. Defaults to an empty object on error. */
	inline CbObject AsObject() &&;

	/** Access the field as an array. Defaults to an empty array on error. */
	inline CbArray AsArray() &;

	/** Access the field as an array. Defaults to an empty array on error. */
	inline CbArray AsArray() &&;
};

/**
 * Iterator for CbFieldRef.
 *
 * @see CbFieldIterator
 */
class CbFieldIterator : public TCbFieldIterator<CbField>
{
public:
	/** Construct a field range from an owned clone of a range. */
	ZENCORE_API static CbFieldIterator CloneRange(const CbFieldViewIterator& It);

	/** Construct a field range from an owned clone of a range. */
	static inline CbFieldIterator CloneRange(const CbFieldIterator& It) { return CloneRange(CbFieldViewIterator(It)); }

	/** Construct a field range that contains exactly one field. */
	static inline CbFieldIterator MakeSingle(CbField Field) { return CbFieldIterator(std::move(Field)); }

	/**
	 * Construct a field range from a buffer containing zero or more valid fields.
	 *
	 * @param Buffer A buffer containing zero or more valid fields.
	 * @param Type HasFieldType means that Buffer contains the type. Otherwise, use the given type.
	 */
	static inline CbFieldIterator MakeRange(SharedBuffer Buffer, CbFieldType Type = CbFieldType::HasFieldType)
	{
		if (Buffer.GetSize())
		{
			const void* const DataEnd = Buffer.GetView().GetDataEnd();
			return CbFieldIterator(CbField(std::move(Buffer), Type), DataEnd);
		}
		return CbFieldIterator();
	}

	/** Construct a field range from an iterator and its optional outer buffer. */
	static inline CbFieldIterator MakeRangeView(const CbFieldViewIterator& It, SharedBuffer OuterBuffer = SharedBuffer())
	{
		return CbFieldIterator(CbField(It, std::move(OuterBuffer)), GetFieldsEnd(It));
	}

	/** Construct an empty field range. */
	constexpr CbFieldIterator() = default;

	/** Clone the range, if necessary, to a buffer that this reference has ownership of. */
	inline void MakeRangeOwned()
	{
		if (!IsOwned())
		{
			*this = CloneRange(*this);
		}
	}

	/** Returns a buffer that exactly contains the field range. */
	ZENCORE_API SharedBuffer GetRangeBuffer() const;

private:
	using TCbFieldIterator::TCbFieldIterator;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * An array that can hold a reference to the memory that contains it.
 *
 * @see CbBuffer
 */
class CbArray : public CbBuffer<CbArrayView>, public CbBufferFactory<CbArray, CbArrayView>
{
public:
	using CbBuffer::CbBuffer;

	/** Create an iterator for the fields of this array. */
	inline CbFieldIterator CreateIterator() const { return CbFieldIterator::MakeRangeView(CreateViewIterator(), GetOuterBuffer()); }

	/** Access the array as an array field. */
	inline CbField AsField() const& { return CbField(CbArrayView::AsFieldView(), *this); }

	/** Access the array as an array field. */
	inline CbField AsField() && { return CbField(CbArrayView::AsFieldView(), std::move(*this)); }

private:
	friend inline CbFieldIterator begin(const CbArray& Array) { return Array.CreateIterator(); }
	friend inline CbFieldIterator end(const CbArray&) { return CbFieldIterator(); }
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * An object that can hold a reference to the memory that contains it.
 *
 * @see CbBuffer
 */
class CbObject : public CbBuffer<CbObjectView>, public CbBufferFactory<CbObject, CbObjectView>
{
public:
	using CbBuffer::CbBuffer;

	/** Create an iterator for the fields of this object. */
	inline CbFieldIterator CreateIterator() const { return CbFieldIterator::MakeRangeView(CreateViewIterator(), GetOuterBuffer()); }

	/** Find a field by case-sensitive name comparison. */
	inline CbField Find(std::string_view Name) const
	{
		if (CbFieldView Field = FindView(Name))
		{
			return CbField(Field, *this);
		}
		return CbField();
	}

	/** Find a field by case-insensitive name comparison. */
	inline CbField FindIgnoreCase(std::string_view Name) const
	{
		if (CbFieldView Field = FindIgnoreCase(Name))
		{
			return CbField(Field, *this);
		}
		return CbField();
	}

	/** Find a field by case-sensitive name comparison. */
	inline CbFieldView operator[](std::string_view Name) const { return Find(Name); }

	/** Access the object as an object field. */
	inline CbField AsField() const& { return CbField(CbObjectView::AsFieldView(), *this); }

	/** Access the object as an object field. */
	inline CbField AsField() && { return CbField(CbObjectView::AsFieldView(), std::move(*this)); }

private:
	friend inline CbFieldIterator begin(const CbObject& Object) { return Object.CreateIterator(); }
	friend inline CbFieldIterator end(const CbObject&) { return CbFieldIterator(); }
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline CbObject
CbField::AsObject() &
{
	return IsObject() ? CbObject(AsObjectView(), *this) : CbObject();
}

inline CbObject
CbField::AsObject() &&
{
	return IsObject() ? CbObject(AsObjectView(), std::move(*this)) : CbObject();
}

inline CbArray
CbField::AsArray() &
{
	return IsArray() ? CbArray(AsArrayView(), *this) : CbArray();
}

inline CbArray
CbField::AsArray() &&
{
	return IsArray() ? CbArray(AsArrayView(), std::move(*this)) : CbArray();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ZENCORE_API CbField LoadCompactBinary(BinaryReader& Ar, BufferAllocator Allocator);

inline CbObject
LoadCompactBinaryObject(IoBuffer Payload)
{
	return CbObject{SharedBuffer::MakeView(Payload.Data(), Payload.Size())};
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Determine the size in bytes of the compact binary field at the start of the view.
 *
 * This may be called on an incomplete or invalid field, in which case the returned size is zero.
 * A size can always be extracted from a valid field with no name if a view of at least the first
 * 10 bytes is provided, regardless of field size. For fields with names, the size of view needed
 * to calculate a size is at most 10 + MaxNameLen + MeasureVarUInt(MaxNameLen).
 *
 * This function can be used when streaming a field, for example, to determine the size of buffer
 * to fill before attempting to construct a field from it.
 *
 * @param View A memory view that may contain the start of a field.
 * @param Type HasFieldType means that View contains the type. Otherwise, use the given type.
 */
ZENCORE_API uint64_t MeasureCompactBinary(MemoryView View, CbFieldType Type = CbFieldType::HasFieldType);

/**
 * Try to determine the type and size of the compact binary field at the start of the view.
 *
 * This may be called on an incomplete or invalid field, in which case it will return false, with
 * OutSize being 0 for invalid fields, otherwise the minimum view size necessary to make progress
 * in measuring the field on the next call to this function.
 *
 * @note A return of true from this function does not indicate that the entire field is valid.
 *
 * @param InView A memory view that may contain the start of a field.
 * @param OutType The type (with flags) of the field. None is written until a value is available.
 * @param OutSize The total field size for a return of true, 0 for invalid fields, or the size to
 *                make progress in measuring the field on the next call to this function.
 * @param InType HasFieldType means that InView contains the type. Otherwise, use the given type.
 * @return true if the size of the field was determined, otherwise false.
 */
ZENCORE_API bool TryMeasureCompactBinary(MemoryView	  InView,
										 CbFieldType& OutType,
										 uint64_t&	  OutSize,
										 CbFieldType  InType = CbFieldType::HasFieldType);

inline CbFieldViewIterator
begin(CbFieldView& View)
{
	if (View.IsArray())
	{
		return View.AsArrayView().CreateViewIterator();
	}
	else if (View.IsObject())
	{
		return View.AsObjectView().CreateViewIterator();
	}

	return CbFieldViewIterator();
}

inline CbFieldViewIterator
end(CbFieldView&)
{
	return CbFieldViewIterator();
}

void uson_forcelink();	// internal

}  // namespace zen

