// Copyright Noah Games, Inc. All Rights Reserved.

#include "zencore/compactbinary.h"

#include <zencore/endian.h>
#include <zencore/stream.h>
#include <zencore/trace.h>

#include <doctest/doctest.h>
#include <ryml/ryml.hpp>
#include <string_view>

namespace zen {

const int DaysPerMonth[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
const int DaysToMonth[]	 = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365};

bool
IsLeapYear(int Year)
{
	if ((Year % 4) == 0)
	{
		return (((Year % 100) != 0) || ((Year % 400) == 0));
	}

	return false;
}

void
DateTime::Set(int Year, int Month, int Day, int Hour, int Minute, int Second, int MilliSecond)
{
	int TotalDays = 0;

	if ((Month > 2) && IsLeapYear(Year))
	{
		++TotalDays;
	}

	--Year;	  // the current year is not a full year yet
	--Month;  // the current month is not a full month yet

	TotalDays += Year * 365;
	TotalDays += Year / 4;			  // leap year day every four years...
	TotalDays -= Year / 100;		  // ...except every 100 years...
	TotalDays += Year / 400;		  // ...but also every 400 years
	TotalDays += DaysToMonth[Month];  // days in this year up to last month
	TotalDays += Day - 1;			  // days in this month minus today

	Ticks = TotalDays * TimeSpan::TicksPerDay + Hour * TimeSpan::TicksPerHour + Minute * TimeSpan::TicksPerMinute +
			Second * TimeSpan::TicksPerSecond + MilliSecond * TimeSpan::TicksPerMillisecond;
}

void
TimeSpan::Set(int Days, int Hours, int Minutes, int Seconds, int FractionNano)
{
	int64_t TotalTicks = 0;

	TotalTicks += Days * TicksPerDay;
	TotalTicks += Hours * TicksPerHour;
	TotalTicks += Minutes * TicksPerMinute;
	TotalTicks += Seconds * TicksPerSecond;
	TotalTicks += FractionNano / NanosecondsPerTick;

	Ticks = TotalTicks;
}

//////////////////////////////////////////////////////////////////////////

namespace usonprivate {

	static constexpr const uint8_t GEmptyObjectPayload[] = {uint8_t(CbFieldType::Object), 0x00};
	static constexpr const uint8_t GEmptyArrayPayload[]	 = {uint8_t(CbFieldType::Array), 0x01, 0x00};

	template<typename T>
	static constexpr inline T ReadUnaligned(const void* const Memory)
	{
#if PLATFORM_SUPPORTS_UNALIGNED_LOADS
		return *static_cast<const T*>(Memory);
#else
		T Value;
		memcpy(&Value, Memory, sizeof(Value));
		return Value;
#endif
	}
}  // namespace usonprivate

//////////////////////////////////////////////////////////////////////////

CbFieldView::CbFieldView(const void* DataPointer, CbFieldType FieldType)
{
	const uint8_t*	  Bytes		= static_cast<const uint8_t*>(DataPointer);
	const CbFieldType LocalType = CbFieldTypeOps::HasFieldType(FieldType) ? (CbFieldType(*Bytes++) | CbFieldType::HasFieldType) : FieldType;

	uint32_t	   NameLenByteCount = 0;
	const uint64_t NameLen64		= CbFieldTypeOps::HasFieldName(LocalType) ? ReadVarUInt(Bytes, NameLenByteCount) : 0;
	Bytes += NameLen64 + NameLenByteCount;

	Type	= LocalType;
	NameLen = uint32_t(std::clamp<uint64_t>(NameLen64, 0, ~uint32_t(0)));
	Payload = Bytes;
}

void
CbFieldView::IterateAttachments(std::function<void(CbFieldView)> Visitor) const
{
	switch (CbFieldTypeOps::GetType(Type))
	{
		case CbFieldType::Object:
		case CbFieldType::UniformObject:
			return CbObjectView::FromFieldView(*this).IterateAttachments(Visitor);
		case CbFieldType::Array:
		case CbFieldType::UniformArray:
			return CbArrayView::FromFieldView(*this).IterateAttachments(Visitor);
		case CbFieldType::CompactBinaryAttachment:
		case CbFieldType::BinaryAttachment:
			return Visitor(*this);
		default:
			return;
	}
}

CbObjectView
CbFieldView::AsObjectView()
{
	if (CbFieldTypeOps::IsObject(Type))
	{
		Error = CbFieldError::None;
		return CbObjectView::FromFieldView(*this);
	}
	else
	{
		Error = CbFieldError::TypeError;
		return CbObjectView();
	}
}

CbArrayView
CbFieldView::AsArrayView()
{
	if (CbFieldTypeOps::IsArray(Type))
	{
		Error = CbFieldError::None;
		return CbArrayView::FromFieldView(*this);
	}
	else
	{
		Error = CbFieldError::TypeError;
		return CbArrayView();
	}
}

MemoryView
CbFieldView::AsBinaryView(const MemoryView Default)
{
	if (CbFieldTypeOps::IsBinary(Type))
	{
		const uint8_t* const PayloadBytes = static_cast<const uint8_t*>(Payload);
		uint32_t			 ValueSizeByteCount;
		const uint64_t		 ValueSize = ReadVarUInt(PayloadBytes, ValueSizeByteCount);

		Error = CbFieldError::None;
		return MemoryView(PayloadBytes + ValueSizeByteCount, ValueSize);
	}
	else
	{
		Error = CbFieldError::TypeError;
		return Default;
	}
}

std::string_view
CbFieldView::AsString(const std::string_view Default)
{
	if (CbFieldTypeOps::IsString(Type))
	{
		const char* const PayloadChars = static_cast<const char*>(Payload);
		uint32_t		  ValueSizeByteCount;
		const uint64_t	  ValueSize = ReadVarUInt(PayloadChars, ValueSizeByteCount);

		if (ValueSize >= (uint64_t(1) << 31))
		{
			Error = CbFieldError::RangeError;
			return Default;
		}

		Error = CbFieldError::None;
		return std::string_view(PayloadChars + ValueSizeByteCount, ValueSize);
	}
	else
	{
		Error = CbFieldError::TypeError;
		return Default;
	}
}

uint64_t
CbFieldView::AsInteger(const uint64_t Default, const IntegerParams Params)
{
	if (CbFieldTypeOps::IsInteger(Type))
	{
		// A shift of a 64-bit value by 64 is undefined so shift by one less because magnitude is never zero.
		const uint64_t OutOfRangeMask = uint64_t(-2) << (Params.MagnitudeBits - 1);
		const uint64_t IsNegative	  = uint8_t(Type) & 1;

		uint32_t	   MagnitudeByteCount;
		const uint64_t Magnitude = ReadVarUInt(Payload, MagnitudeByteCount);
		const uint64_t Value	 = Magnitude ^ -int64_t(IsNegative);

		const uint64_t IsInRange = (!(Magnitude & OutOfRangeMask)) & ((!IsNegative) | Params.IsSigned);
		Error					 = IsInRange ? CbFieldError::None : CbFieldError::RangeError;

		const uint64_t UseValueMask = -int64_t(IsInRange);
		return (Value & UseValueMask) | (Default & ~UseValueMask);
	}
	else
	{
		Error = CbFieldError::TypeError;
		return Default;
	}
}

float
CbFieldView::AsFloat(const float Default)
{
	switch (CbFieldTypeOps::GetType(Type))
	{
		case CbFieldType::IntegerPositive:
		case CbFieldType::IntegerNegative:
			{
				const uint64_t	   IsNegative	  = uint8_t(Type) & 1;
				constexpr uint64_t OutOfRangeMask = ~((uint64_t(1) << /*FLT_MANT_DIG*/ 24) - 1);

				uint32_t	   MagnitudeByteCount;
				const int64_t  Magnitude = ReadVarUInt(Payload, MagnitudeByteCount) + IsNegative;
				const uint64_t IsInRange = !(Magnitude & OutOfRangeMask);
				Error					 = IsInRange ? CbFieldError::None : CbFieldError::RangeError;
				return IsInRange ? float(IsNegative ? -Magnitude : Magnitude) : Default;
			}
		case CbFieldType::Float32:
			{
				Error				 = CbFieldError::None;
				const uint32_t Value = FromNetworkOrder(usonprivate::ReadUnaligned<uint32_t>(Payload));
				return reinterpret_cast<const float&>(Value);
			}
		case CbFieldType::Float64:
			Error = CbFieldError::RangeError;
			return Default;
		default:
			Error = CbFieldError::TypeError;
			return Default;
	}
}

double
CbFieldView::AsDouble(const double Default)
{
	switch (CbFieldTypeOps::GetType(Type))
	{
		case CbFieldType::IntegerPositive:
		case CbFieldType::IntegerNegative:
			{
				const uint64_t	   IsNegative	  = uint8_t(Type) & 1;
				constexpr uint64_t OutOfRangeMask = ~((uint64_t(1) << /*DBL_MANT_DIG*/ 53) - 1);

				uint32_t	   MagnitudeByteCount;
				const int64_t  Magnitude = ReadVarUInt(Payload, MagnitudeByteCount) + IsNegative;
				const uint64_t IsInRange = !(Magnitude & OutOfRangeMask);
				Error					 = IsInRange ? CbFieldError::None : CbFieldError::RangeError;
				return IsInRange ? double(IsNegative ? -Magnitude : Magnitude) : Default;
			}
		case CbFieldType::Float32:
			{
				Error				 = CbFieldError::None;
				const uint32_t Value = FromNetworkOrder(usonprivate::ReadUnaligned<uint32_t>(Payload));
				return reinterpret_cast<const float&>(Value);
			}
		case CbFieldType::Float64:
			{
				Error				 = CbFieldError::None;
				const uint64_t Value = FromNetworkOrder(usonprivate::ReadUnaligned<uint64_t>(Payload));
				return reinterpret_cast<const double&>(Value);
			}
		default:
			Error = CbFieldError::TypeError;
			return Default;
	}
}

bool
CbFieldView::AsBool(const bool bDefault)
{
	const CbFieldType LocalType = Type;
	const bool		  bIsBool	= CbFieldTypeOps::IsBool(LocalType);
	Error						= bIsBool ? CbFieldError::None : CbFieldError::TypeError;
	return (uint8_t(bIsBool) & uint8_t(LocalType) & 1) | ((!bIsBool) & bDefault);
}

IoHash
CbFieldView::AsCompactBinaryAttachment(const IoHash& Default)
{
	if (CbFieldTypeOps::IsCompactBinaryAttachment(Type))
	{
		Error = CbFieldError::None;
		return IoHash::MakeFrom(Payload);
	}
	else
	{
		Error = CbFieldError::TypeError;
		return Default;
	}
}

IoHash
CbFieldView::AsBinaryAttachment(const IoHash& Default)
{
	if (CbFieldTypeOps::IsBinaryAttachment(Type))
	{
		Error = CbFieldError::None;
		return IoHash::MakeFrom(Payload);
	}
	else
	{
		Error = CbFieldError::TypeError;
		return Default;
	}
}

IoHash
CbFieldView::AsAttachment(const IoHash& Default)
{
	if (CbFieldTypeOps::IsAttachment(Type))
	{
		Error = CbFieldError::None;
		return IoHash::MakeFrom(Payload);
	}
	else
	{
		Error = CbFieldError::TypeError;
		return Default;
	}
}

IoHash
CbFieldView::AsHash(const IoHash& Default)
{
	if (CbFieldTypeOps::IsHash(Type))
	{
		Error = CbFieldError::None;
		return IoHash::MakeFrom(Payload);
	}
	else
	{
		Error = CbFieldError::TypeError;
		return Default;
	}
}

Guid
CbFieldView::AsUuid()
{
	return AsUuid(Guid());
}

Guid
CbFieldView::AsUuid(const Guid& Default)
{
	if (CbFieldTypeOps::IsUuid(Type))
	{
		Error = CbFieldError::None;
		Guid Value;
		memcpy(&Value, Payload, sizeof(Guid));
		Value.A = FromNetworkOrder(Value.A);
		Value.B = FromNetworkOrder(Value.B);
		Value.C = FromNetworkOrder(Value.C);
		Value.D = FromNetworkOrder(Value.D);
		return Value;
	}
	else
	{
		Error = CbFieldError::TypeError;
		return Default;
	}
}

Oid
CbFieldView::AsObjectId()
{
	return AsObjectId(Oid());
}

Oid
CbFieldView::AsObjectId(const Oid& Default)
{
	if (CbFieldTypeOps::IsObjectId(Type))
	{
		Error = CbFieldError::None;
		Oid Value;
		memcpy(&Value, Payload, sizeof(Oid));
		return Value;
	}
	else
	{
		Error = CbFieldError::TypeError;
		return Default;
	}
}

int64_t
CbFieldView::AsDateTimeTicks(const int64_t Default)
{
	if (CbFieldTypeOps::IsDateTime(Type))
	{
		Error = CbFieldError::None;
		return FromNetworkOrder(usonprivate::ReadUnaligned<int64_t>(Payload));
	}
	else
	{
		Error = CbFieldError::TypeError;
		return Default;
	}
}

DateTime
CbFieldView::AsDateTime()
{
	return DateTime(AsDateTimeTicks(0));
}

DateTime
CbFieldView::AsDateTime(DateTime Default)
{
	return DateTime(AsDateTimeTicks(Default.GetTicks()));
}

int64_t
CbFieldView::AsTimeSpanTicks(const int64_t Default)
{
	if (CbFieldTypeOps::IsTimeSpan(Type))
	{
		Error = CbFieldError::None;
		return FromNetworkOrder(usonprivate::ReadUnaligned<int64_t>(Payload));
	}
	else
	{
		Error = CbFieldError::TypeError;
		return Default;
	}
}

TimeSpan
CbFieldView::AsTimeSpan()
{
	return TimeSpan(AsTimeSpanTicks(0));
}

TimeSpan
CbFieldView::AsTimeSpan(TimeSpan Default)
{
	return TimeSpan(AsTimeSpanTicks(Default.GetTicks()));
}

uint64_t
CbFieldView::GetSize() const
{
	return sizeof(CbFieldType) + GetViewNoType().GetSize();
}

uint64_t
CbFieldView::GetPayloadSize() const
{
	switch (CbFieldTypeOps::GetType(Type))
	{
		case CbFieldType::None:
		case CbFieldType::Null:
			return 0;
		case CbFieldType::Object:
		case CbFieldType::UniformObject:
		case CbFieldType::Array:
		case CbFieldType::UniformArray:
		case CbFieldType::Binary:
		case CbFieldType::String:
			{
				uint32_t	   PayloadSizeByteCount;
				const uint64_t PayloadSize = ReadVarUInt(Payload, PayloadSizeByteCount);
				return PayloadSize + PayloadSizeByteCount;
			}
		case CbFieldType::IntegerPositive:
		case CbFieldType::IntegerNegative:
			return MeasureVarUInt(Payload);
		case CbFieldType::Float32:
			return 4;
		case CbFieldType::Float64:
			return 8;
		case CbFieldType::BoolFalse:
		case CbFieldType::BoolTrue:
			return 0;
		case CbFieldType::CompactBinaryAttachment:
		case CbFieldType::BinaryAttachment:
		case CbFieldType::Hash:
			return 20;
		case CbFieldType::Uuid:
			return 16;
		case CbFieldType::ObjectId:
			return 12;
		case CbFieldType::DateTime:
		case CbFieldType::TimeSpan:
			return 8;
		default:
			return 0;
	}
}

IoHash
CbFieldView::GetHash() const
{
	IoHashStream HashStream;
	GetHash(HashStream);
	return HashStream.GetHash();
}

void
CbFieldView::GetHash(IoHashStream& Hash) const
{
	const CbFieldType SerializedType = CbFieldTypeOps::GetSerializedType(Type);
	Hash.Append(&SerializedType, sizeof(SerializedType));
	auto View = GetViewNoType();
	Hash.Append(View.GetData(), View.GetSize());
}

bool
CbFieldView::Equals(const CbFieldView& Other) const
{
	return CbFieldTypeOps::GetSerializedType(Type) == CbFieldTypeOps::GetSerializedType(Other.Type) &&
		   GetViewNoType().EqualBytes(Other.GetViewNoType());
}

void
CbFieldView::CopyTo(MutableMemoryView Buffer) const
{
	const MemoryView Source = GetViewNoType();
	ZEN_ASSERT(Buffer.GetSize() == sizeof(CbFieldType) + Source.GetSize());
	//	   TEXT("A buffer of %" UINT64_FMT " bytes was provided when %" UINT64_FMT " bytes are required"),
	//	   Buffer.GetSize(),
	//	   sizeof(CbFieldType) + Source.GetSize());
	*static_cast<CbFieldType*>(Buffer.GetData()) = CbFieldTypeOps::GetSerializedType(Type);
	Buffer.RightChopInline(sizeof(CbFieldType));
	memcpy(Buffer.GetData(), Source.GetData(), Source.GetSize());
}

void
CbFieldView::CopyTo(BinaryWriter& Ar) const
{
	const MemoryView Source			= GetViewNoType();
	CbFieldType		 SerializedType = CbFieldTypeOps::GetSerializedType(Type);
	Ar.Write(&SerializedType, sizeof(SerializedType));
	Ar.Write(Source.GetData(), Source.GetSize());
}

MemoryView
CbFieldView::GetView() const
{
	const uint32_t TypeSize	   = CbFieldTypeOps::HasFieldType(Type) ? sizeof(CbFieldType) : 0;
	const uint32_t NameSize	   = CbFieldTypeOps::HasFieldName(Type) ? NameLen + MeasureVarUInt(NameLen) : 0;
	const uint64_t PayloadSize = GetPayloadSize();
	return MemoryView(static_cast<const uint8_t*>(Payload) - TypeSize - NameSize, TypeSize + NameSize + PayloadSize);
}

MemoryView
CbFieldView::GetViewNoType() const
{
	const uint32_t NameSize	   = CbFieldTypeOps::HasFieldName(Type) ? NameLen + MeasureVarUInt(NameLen) : 0;
	const uint64_t PayloadSize = GetPayloadSize();
	return MemoryView(static_cast<const uint8_t*>(Payload) - NameSize, NameSize + PayloadSize);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CbArrayView::CbArrayView() : CbFieldView(usonprivate::GEmptyArrayPayload)
{
}

uint64_t
CbArrayView::Num() const
{
	const uint8_t* PayloadBytes = static_cast<const uint8_t*>(GetPayload());
	PayloadBytes += MeasureVarUInt(PayloadBytes);
	uint32_t NumByteCount;
	return ReadVarUInt(PayloadBytes, NumByteCount);
}

CbFieldViewIterator
CbArrayView::CreateViewIterator() const
{
	const uint8_t* PayloadBytes = static_cast<const uint8_t*>(GetPayload());
	uint32_t	   PayloadSizeByteCount;
	const uint64_t PayloadSize = ReadVarUInt(PayloadBytes, PayloadSizeByteCount);
	PayloadBytes += PayloadSizeByteCount;
	const uint64_t NumByteCount = MeasureVarUInt(PayloadBytes);
	if (PayloadSize > NumByteCount)
	{
		const void* const PayloadEnd = PayloadBytes + PayloadSize;
		PayloadBytes += NumByteCount;
		const CbFieldType UniformType =
			CbFieldTypeOps::GetType(GetType()) == CbFieldType::UniformArray ? CbFieldType(*PayloadBytes++) : CbFieldType::HasFieldType;
		return CbFieldViewIterator::MakeRange(MemoryView(PayloadBytes, PayloadEnd), UniformType);
	}
	return CbFieldViewIterator();
}

void
CbArrayView::VisitFields(ICbVisitor&)
{
}

uint64_t
CbArrayView::GetSize() const
{
	return sizeof(CbFieldType) + GetPayloadSize();
}

IoHash
CbArrayView::GetHash() const
{
	IoHashStream Hash;
	GetHash(Hash);
	return Hash.GetHash();
}

void
CbArrayView::GetHash(IoHashStream& HashStream) const
{
	const CbFieldType SerializedType = CbFieldTypeOps::GetType(GetType());
	HashStream.Append(&SerializedType, sizeof(SerializedType));
	auto _ = GetPayloadView();
	HashStream.Append(_.GetData(), _.GetSize());
}

bool
CbArrayView::Equals(const CbArrayView& Other) const
{
	return CbFieldTypeOps::GetType(GetType()) == CbFieldTypeOps::GetType(Other.GetType()) &&
		   GetPayloadView().EqualBytes(Other.GetPayloadView());
}

void
CbArrayView::CopyTo(MutableMemoryView Buffer) const
{
	const MemoryView Source = GetPayloadView();
	ZEN_ASSERT(Buffer.GetSize() == sizeof(CbFieldType) + Source.GetSize());
	// TEXT("Buffer is %" UINT64_FMT " bytes but %" UINT64_FMT " is required."),
	// Buffer.GetSize(),
	// sizeof(CbFieldType) + Source.GetSize());

	*static_cast<CbFieldType*>(Buffer.GetData()) = CbFieldTypeOps::GetType(GetType());
	Buffer.RightChopInline(sizeof(CbFieldType));
	memcpy(Buffer.GetData(), Source.GetData(), Source.GetSize());
}

void
CbArrayView::CopyTo(BinaryWriter& Ar) const
{
	const MemoryView Source			= GetPayloadView();
	CbFieldType		 SerializedType = CbFieldTypeOps::GetType(GetType());
	Ar.Write(&SerializedType, sizeof(SerializedType));
	Ar.Write(Source.GetData(), Source.GetSize());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CbObjectView::CbObjectView() : CbFieldView(usonprivate::GEmptyObjectPayload)
{
}

CbFieldViewIterator
CbObjectView::CreateViewIterator() const
{
	const uint8_t* PayloadBytes = static_cast<const uint8_t*>(GetPayload());
	uint32_t	   PayloadSizeByteCount;
	const uint64_t PayloadSize = ReadVarUInt(PayloadBytes, PayloadSizeByteCount);

	PayloadBytes += PayloadSizeByteCount;

	if (PayloadSize)
	{
		const void* const PayloadEnd = PayloadBytes + PayloadSize;
		const CbFieldType UniformType =
			CbFieldTypeOps::GetType(GetType()) == CbFieldType::UniformObject ? CbFieldType(*PayloadBytes++) : CbFieldType::HasFieldType;
		return CbFieldViewIterator::MakeRange(MemoryView(PayloadBytes, PayloadEnd), UniformType);
	}

	return CbFieldViewIterator();
}

void
CbObjectView::VisitFields(ICbVisitor&)
{
}

CbFieldView
CbObjectView::FindView(const std::string_view Name) const
{
	for (const CbFieldView Field : *this)
	{
		if (Name == Field.GetName())
		{
			return Field;
		}
	}
	return CbFieldView();
}

CbFieldView
CbObjectView::FindViewIgnoreCase(const std::string_view Name) const
{
	for (const CbFieldView Field : *this)
	{
		if (Name == Field.GetName())
		{
			return Field;
		}
	}
	return CbFieldView();
}

uint64_t
CbObjectView::GetSize() const
{
	return sizeof(CbFieldType) + GetPayloadSize();
}

IoHash
CbObjectView::GetHash() const
{
	IoHashStream Hash;
	GetHash(Hash);
	return Hash.GetHash();
}

void
CbObjectView::GetHash(IoHashStream& HashStream) const
{
	const CbFieldType SerializedType = CbFieldTypeOps::GetType(GetType());
	HashStream.Append(&SerializedType, sizeof(SerializedType));
	HashStream.Append(GetPayloadView());
}

bool
CbObjectView::Equals(const CbObjectView& Other) const
{
	return CbFieldTypeOps::GetType(GetType()) == CbFieldTypeOps::GetType(Other.GetType()) &&
		   GetPayloadView().EqualBytes(Other.GetPayloadView());
}

void
CbObjectView::CopyTo(MutableMemoryView Buffer) const
{
	const MemoryView Source = GetPayloadView();
	ZEN_ASSERT(Buffer.GetSize() == (sizeof(CbFieldType) + Source.GetSize()));
	// TEXT("Buffer is %" UINT64_FMT " bytes but %" UINT64_FMT " is required."),
	// Buffer.GetSize(),
	// sizeof(CbFieldType) + Source.GetSize());
	*static_cast<CbFieldType*>(Buffer.GetData()) = CbFieldTypeOps::GetType(GetType());
	Buffer.RightChopInline(sizeof(CbFieldType));
	memcpy(Buffer.GetData(), Source.GetData(), Source.GetSize());
}

void
CbObjectView::CopyTo(BinaryWriter& Ar) const
{
	const MemoryView Source			= GetPayloadView();
	CbFieldType		 SerializedType = CbFieldTypeOps::GetType(GetType());
	Ar.Write(&SerializedType, sizeof(SerializedType));
	Ar.Write(Source.GetData(), Source.GetSize());
}

//////////////////////////////////////////////////////////////////////////

template<typename FieldType>
uint64_t
TCbFieldIterator<FieldType>::GetRangeSize() const
{
	MemoryView View;
	if (TryGetSerializedRangeView(View))
	{
		return View.GetSize();
	}
	else
	{
		uint64_t Size = 0;
		for (CbFieldViewIterator It(*this); It; ++It)
		{
			Size += It.GetSize();
		}
		return Size;
	}
}

template<typename FieldType>
IoHash
TCbFieldIterator<FieldType>::GetRangeHash() const
{
	IoHashStream Hash;
	GetRangeHash(Hash);
	return IoHash(Hash.GetHash());
}

template<typename FieldType>
void
TCbFieldIterator<FieldType>::GetRangeHash(IoHashStream& Hash) const
{
	MemoryView View;
	if (TryGetSerializedRangeView(View))
	{
		Hash.Append(View.GetData(), View.GetSize());
	}
	else
	{
		for (CbFieldViewIterator It(*this); It; ++It)
		{
			It.GetHash(Hash);
		}
	}
}

template<typename FieldType>
void
TCbFieldIterator<FieldType>::CopyRangeTo(MutableMemoryView InBuffer) const
{
	MemoryView Source;
	if (TryGetSerializedRangeView(Source))
	{
		ZEN_ASSERT(InBuffer.GetSize() == Source.GetSize());
		// TEXT("Buffer is %" UINT64_FMT " bytes but %" UINT64_FMT " is required."),
		// InBuffer.GetSize(),
		// Source.GetSize());
		memcpy(InBuffer.GetData(), Source.GetData(), Source.GetSize());
	}
	else
	{
		for (CbFieldViewIterator It(*this); It; ++It)
		{
			const uint64_t Size = It.GetSize();
			It.CopyTo(InBuffer.Left(Size));
			InBuffer.RightChopInline(Size);
		}
	}
}

template class TCbFieldIterator<CbFieldView>;
template class TCbFieldIterator<CbField>;

template<typename FieldType>
void
TCbFieldIterator<FieldType>::IterateRangeAttachments(std::function<void(CbFieldView)> Visitor) const
{
	if (CbFieldTypeOps::HasFieldType(FieldType::GetType()))
	{
		// Always iterate over non-uniform ranges because we do not know if they contain an attachment.
		for (CbFieldViewIterator It(*this); It; ++It)
		{
			if (CbFieldTypeOps::MayContainAttachments(It.GetType()))
			{
				It.IterateAttachments(Visitor);
			}
		}
	}
	else
	{
		// Only iterate over uniform ranges if the uniform type may contain an attachment.
		if (CbFieldTypeOps::MayContainAttachments(FieldType::GetType()))
		{
			for (CbFieldViewIterator It(*this); It; ++It)
			{
				It.IterateAttachments(Visitor);
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CbFieldIterator
CbFieldIterator::CloneRange(const CbFieldViewIterator& It)
{
	MemoryView View;
	if (It.TryGetSerializedRangeView(View))
	{
		return MakeRange(SharedBuffer::Clone(View));
	}
	else
	{
		UniqueBuffer Buffer = UniqueBuffer::Alloc(It.GetRangeSize());
		It.CopyRangeTo(MutableMemoryView(Buffer.GetData(), Buffer.GetSize()));
		return MakeRange(SharedBuffer(std::move(Buffer)));
	}
}

SharedBuffer
CbFieldIterator::GetRangeBuffer() const
{
	const MemoryView	RangeView	= GetRangeView();
	const SharedBuffer& OuterBuffer = GetOuterBuffer();
	return OuterBuffer.GetView() == RangeView ? OuterBuffer : SharedBuffer::MakeView(RangeView, OuterBuffer);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint64_t
MeasureCompactBinary(MemoryView View, CbFieldType Type)
{
	uint64_t Size;
	return TryMeasureCompactBinary(View, Type, Size, Type) ? Size : 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool
TryMeasureCompactBinary(MemoryView View, CbFieldType& OutType, uint64_t& OutSize, CbFieldType Type)
{
	uint64_t Size = 0;

	if (CbFieldTypeOps::HasFieldType(Type))
	{
		if (View.GetSize() == 0)
		{
			OutType = CbFieldType::None;
			OutSize = 1;
			return false;
		}

		Type = *static_cast<const CbFieldType*>(View.GetData());
		View.RightChopInline(1);
		Size += 1;
	}

	bool	 bDynamicSize = false;
	uint64_t FixedSize	  = 0;
	switch (CbFieldTypeOps::GetType(Type))
	{
		case CbFieldType::Null:
			break;
		case CbFieldType::Object:
		case CbFieldType::UniformObject:
		case CbFieldType::Array:
		case CbFieldType::UniformArray:
		case CbFieldType::Binary:
		case CbFieldType::String:
		case CbFieldType::IntegerPositive:
		case CbFieldType::IntegerNegative:
			bDynamicSize = true;
			break;
		case CbFieldType::Float32:
			FixedSize = 4;
			break;
		case CbFieldType::Float64:
			FixedSize = 8;
			break;
		case CbFieldType::BoolFalse:
		case CbFieldType::BoolTrue:
			break;
		case CbFieldType::CompactBinaryAttachment:
		case CbFieldType::BinaryAttachment:
		case CbFieldType::Hash:
			FixedSize = 20;
			break;
		case CbFieldType::Uuid:
			FixedSize = 16;
			break;
		case CbFieldType::ObjectId:
			FixedSize = 12;
			break;
		case CbFieldType::DateTime:
		case CbFieldType::TimeSpan:
			FixedSize = 8;
			break;
		case CbFieldType::None:
		default:
			OutType = CbFieldType::None;
			OutSize = 0;
			return false;
	}

	OutType = Type;

	if (CbFieldTypeOps::HasFieldName(Type))
	{
		if (View.GetSize() == 0)
		{
			OutSize = Size + 1;
			return false;
		}

		uint32_t NameLenByteCount = MeasureVarUInt(View.GetData());
		if (View.GetSize() < NameLenByteCount)
		{
			OutSize = Size + NameLenByteCount;
			return false;
		}

		const uint64_t NameLen	= ReadVarUInt(View.GetData(), NameLenByteCount);
		const uint64_t NameSize = NameLen + NameLenByteCount;

		if (bDynamicSize && View.GetSize() < NameSize)
		{
			OutSize = Size + NameSize;
			return false;
		}

		View.RightChopInline(NameSize);
		Size += NameSize;
	}

	switch (CbFieldTypeOps::GetType(Type))
	{
		case CbFieldType::Object:
		case CbFieldType::UniformObject:
		case CbFieldType::Array:
		case CbFieldType::UniformArray:
		case CbFieldType::Binary:
		case CbFieldType::String:
			if (View.GetSize() == 0)
			{
				OutSize = Size + 1;
				return false;
			}
			else
			{
				uint32_t PayloadSizeByteCount = MeasureVarUInt(View.GetData());
				if (View.GetSize() < PayloadSizeByteCount)
				{
					OutSize = Size + PayloadSizeByteCount;
					return false;
				}
				const uint64_t PayloadSize = ReadVarUInt(View.GetData(), PayloadSizeByteCount);
				OutSize					   = Size + PayloadSize + PayloadSizeByteCount;
			}
			return true;

		case CbFieldType::IntegerPositive:
		case CbFieldType::IntegerNegative:
			if (View.GetSize() == 0)
			{
				OutSize = Size + 1;
				return false;
			}
			OutSize = Size + MeasureVarUInt(View.GetData());
			return true;

		default:
			OutSize = Size + FixedSize;
			return true;
	}
}

//////////////////////////////////////////////////////////////////////////

CbField
LoadCompactBinary(BinaryReader& Ar, BufferAllocator Allocator)
{
	std::vector<uint8_t> HeaderBytes;
	CbFieldType			 FieldType;
	uint64_t			 FieldSize = 1;

	// Read in small increments until the total field size is known, to avoid reading too far.
	for (;;)
	{
		const int32_t ReadSize	 = int32_t(FieldSize - HeaderBytes.size());
		const size_t  ReadOffset = HeaderBytes.size();
		HeaderBytes.resize(ReadOffset + ReadSize);

		Ar.Read(HeaderBytes.data() + ReadOffset, ReadSize);
		if (TryMeasureCompactBinary(MakeMemoryView(HeaderBytes), FieldType, FieldSize))
		{
			break;
		}
		ZEN_ASSERT(FieldSize > 0, "Failed to load from invalid compact binary data.");
	}

	// Allocate the buffer, copy the header, and read the remainder of the field.
	UniqueBuffer Buffer = Allocator(FieldSize);
	ZEN_ASSERT(Buffer.GetSize() == FieldSize);
	MutableMemoryView View = Buffer.GetView();
	memcpy(View.GetData(), HeaderBytes.data(), HeaderBytes.size());
	View.RightChopInline(HeaderBytes.size());
	if (!View.IsEmpty())
	{
		Ar.Read(View.GetData(), View.GetSize());
	}
	return CbField(SharedBuffer(std::move(Buffer)));
}

//////////////////////////////////////////////////////////////////////////

void
SaveCompactBinary(BinaryWriter& Ar, const CbFieldView& Field)
{
	Field.CopyTo(Ar);
}

void
SaveCompactBinary(BinaryWriter& Ar, const CbArrayView& Array)
{
	Array.CopyTo(Ar);
}

void
SaveCompactBinary(BinaryWriter& Ar, const CbObjectView& Object)
{
	Object.CopyTo(Ar);
}

//////////////////////////////////////////////////////////////////////////

StringBuilderBase&
ToString(CbObjectView& Root, StringBuilderBase& OutString)
{
	ryml::Tree Tree;

	ryml::NodeRef r = Tree.rootref();
	r |= ryml::MAP;

	for (CbFieldViewIterator It = Root.CreateViewIterator(); It; ++It)
	{
	}

	return OutString;
}

//////////////////////////////////////////////////////////////////////////

void
uson_forcelink()
{
}

TEST_CASE("uson")
{
	using namespace std::literals;

	SUBCASE("CbField")
	{
		constexpr CbFieldView DefaultField;
		static_assert(!DefaultField.HasName(), "Error in HasName()");
		static_assert(!DefaultField.HasValue(), "Error in HasValue()");
		static_assert(!DefaultField.HasError(), "Error in HasError()");
		static_assert(DefaultField.GetError() == CbFieldError::None, "Error in GetError()");

		CHECK(DefaultField.GetSize() == 1);
		CHECK(DefaultField.GetName().size() == 0);
		CHECK(DefaultField.HasName() == false);
		CHECK(DefaultField.HasValue() == false);
		CHECK(DefaultField.HasError() == false);
		CHECK(DefaultField.GetError() == CbFieldError::None);

		const uint8_t Type = (uint8_t)CbFieldType::None;
		CHECK(DefaultField.GetHash() == IoHash::HashMemory(&Type, sizeof Type));

		CHECK(DefaultField.GetView() == MemoryView{});
		MemoryView SerializedView;
		CHECK(DefaultField.TryGetSerializedView(SerializedView) == false);
	}

	SUBCASE("CbField(None)")
	{
		CbFieldView NoneField(nullptr, CbFieldType::None);
		CHECK(NoneField.GetSize() == 1);
		CHECK(NoneField.GetName().size() == 0);
		CHECK(NoneField.HasName() == false);
		CHECK(NoneField.HasValue() == false);
		CHECK(NoneField.HasError() == false);
		CHECK(NoneField.GetError() == CbFieldError::None);
		CHECK(NoneField.GetHash() == CbFieldView().GetHash());
		CHECK(NoneField.GetView() == MemoryView());
		MemoryView SerializedView;
		CHECK(NoneField.TryGetSerializedView(SerializedView) == false);
	}

	SUBCASE("CbField(None|Type|Name)")
	{
		constexpr CbFieldType FieldType	  = CbFieldType::None | CbFieldType::HasFieldName;
		constexpr const char  NoneBytes[] = {char(FieldType), 4, 'N', 'a', 'm', 'e'};
		CbFieldView			  NoneField(NoneBytes);

		CHECK(NoneField.GetSize() == sizeof(NoneBytes));
		CHECK(NoneField.GetName().compare("Name"sv) == 0);
		CHECK(NoneField.HasName() == true);
		CHECK(NoneField.HasValue() == false);
		CHECK(NoneField.GetHash() == IoHash::HashMemory(NoneBytes, sizeof NoneBytes));
		CHECK(NoneField.GetView() == MemoryView(NoneBytes, sizeof NoneBytes));
		MemoryView SerializedView;
		CHECK(NoneField.TryGetSerializedView(SerializedView) == true);
		CHECK(SerializedView == MemoryView(NoneBytes, sizeof NoneBytes));

		uint8_t CopyBytes[sizeof(NoneBytes)];
		NoneField.CopyTo(MutableMemoryView(CopyBytes, sizeof CopyBytes));
		CHECK(MemoryView(NoneBytes, sizeof NoneBytes).EqualBytes(MemoryView(CopyBytes, sizeof CopyBytes)));
	}

	SUBCASE("CbField(None|Type)")
	{
		constexpr CbFieldType FieldType	  = CbFieldType::None;
		constexpr const char  NoneBytes[] = {char(FieldType)};
		CbFieldView			  NoneField(NoneBytes);

		CHECK(NoneField.GetSize() == sizeof NoneBytes);
		CHECK(NoneField.GetName().size() == 0);
		CHECK(NoneField.HasName() == false);
		CHECK(NoneField.HasValue() == false);
		CHECK(NoneField.GetHash() == CbFieldView().GetHash());
		CHECK(NoneField.GetView() == MemoryView(NoneBytes, sizeof NoneBytes));
		MemoryView SerializedView;
		CHECK(NoneField.TryGetSerializedView(SerializedView) == true);
		CHECK(SerializedView == MemoryView(NoneBytes, sizeof NoneBytes));
	}

	SUBCASE("CbField(None|Name)")
	{
		constexpr CbFieldType FieldType	  = CbFieldType::None | CbFieldType::HasFieldName;
		constexpr const char  NoneBytes[] = {char(FieldType), 4, 'N', 'a', 'm', 'e'};
		CbFieldView			  NoneField(NoneBytes + 1, FieldType);
		CHECK(NoneField.GetSize() == uint64_t(sizeof NoneBytes));
		CHECK(NoneField.GetName().compare("Name") == 0);
		CHECK(NoneField.HasName() == true);
		CHECK(NoneField.HasValue() == false);
		CHECK(NoneField.GetHash() == IoHash::HashMemory(NoneBytes, sizeof NoneBytes));
		CHECK(NoneField.GetView() == MemoryView(&NoneBytes[1], sizeof NoneBytes - 1));
		MemoryView SerializedView;
		CHECK(NoneField.TryGetSerializedView(SerializedView) == false);

		uint8_t CopyBytes[sizeof(NoneBytes)];
		NoneField.CopyTo(MutableMemoryView(CopyBytes, sizeof CopyBytes));
		CHECK(MemoryView(NoneBytes, sizeof NoneBytes).EqualBytes(MemoryView(CopyBytes, sizeof CopyBytes)));
	}

	SUBCASE("CbField(None|EmptyName)")
	{
		constexpr CbFieldType	FieldType	= CbFieldType::None | CbFieldType::HasFieldName;
		constexpr const uint8_t NoneBytes[] = {uint8_t(FieldType), 0};
		CbFieldView				NoneField(NoneBytes + 1, FieldType);
		CHECK(NoneField.GetSize() == sizeof NoneBytes);
		CHECK(NoneField.GetName().empty() == true);
		CHECK(NoneField.HasName() == true);
		CHECK(NoneField.HasValue() == false);
		CHECK(NoneField.GetHash() == IoHash::HashMemory(NoneBytes, sizeof NoneBytes));
		CHECK(NoneField.GetView() == MemoryView(&NoneBytes[1], sizeof NoneBytes - 1));
		MemoryView SerializedView;
		CHECK(NoneField.TryGetSerializedView(SerializedView) == false);
	}

	static_assert(!std::is_constructible<CbFieldView, const CbObjectView&>::value, "Invalid constructor for CbField");
	static_assert(!std::is_assignable<CbFieldView, const CbObjectView&>::value, "Invalid assignment for CbField");
	static_assert(!std::is_convertible<CbFieldView, CbObjectView>::value, "Invalid conversion to CbObject");
	static_assert(!std::is_assignable<CbObjectView, const CbFieldView&>::value, "Invalid assignment for CbObject");
}

TEST_CASE("uson.null")
{
	using namespace std::literals;

	SUBCASE("CbField(Null)")
	{
		CbFieldView NullField(nullptr, CbFieldType::Null);
		CHECK(NullField.GetSize() == 1);
		CHECK(NullField.IsNull() == true);
		CHECK(NullField.HasValue() == true);
		CHECK(NullField.HasError() == false);
		CHECK(NullField.GetError() == CbFieldError::None);
		const uint8_t Null[]{uint8_t(CbFieldType::Null)};
		CHECK(NullField.GetHash() == IoHash::HashMemory(Null, sizeof Null));
	}

	SUBCASE("CbField(None)")
	{
		CbFieldView Field;
		CHECK(Field.IsNull() == false);
	}
}

}  // namespace zen

