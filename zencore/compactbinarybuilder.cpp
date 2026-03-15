// Copyright Noah Games, Inc. All Rights Reserved.

#include "zencore/compactbinarybuilder.h"

#include <zencore/compactbinarypackage.h>
#include <zencore/compactbinaryvalidation.h>
#include <zencore/endian.h>
#include <zencore/stream.h>
#include <zencore/string.h>

#define _USE_MATH_DEFINES
#include <math.h>

#include <doctest/doctest.h>

namespace zen {

template<typename T>
uint64_t
AddUninitialized(std::vector<T>& Vector, uint64_t Count)
{
	const uint64_t Offset = Vector.size();
	Vector.resize(Offset + Count);
	return Offset;
}

template<typename T>
uint64_t
Append(std::vector<T>& Vector, const T* Data, uint64_t Count)
{
	const uint64_t Offset = Vector.size();
	Vector.resize(Offset + Count);

	memcpy(Vector.data() + Offset, Data, sizeof(T) * Count);

	return Offset;
}

//////////////////////////////////////////////////////////////////////////

enum class CbWriter::StateFlags : uint8_t
{
	None = 0,
	/** Whether a name has been written for the current field. */
	Name = 1 << 0,
	/** Whether this state is in the process of writing a field. */
	Field = 1 << 1,
	/** Whether this state is for array fields. */
	Array = 1 << 2,
	/** Whether this state is for object fields. */
	Object = 1 << 3,
};

ENUM_CLASS_FLAGS(CbWriter::StateFlags);

/** Whether the field type can be used in a uniform array or uniform object. */
static constexpr bool
IsUniformType(const CbFieldType Type)
{
	if (CbFieldTypeOps::HasFieldName(Type))
	{
		return true;
	}

	switch (Type)
	{
		case CbFieldType::None:
		case CbFieldType::Null:
		case CbFieldType::BoolFalse:
		case CbFieldType::BoolTrue:
			return false;
		default:
			return true;
	}
}

/** Append the payload from the compact binary value to the array and return its type. */
static inline CbFieldType
AppendCompactBinary(const CbFieldView& Value, std::vector<uint8_t>& OutData)
{
	struct FCopy : public CbFieldView
	{
		using CbFieldView::GetPayloadView;
		using CbFieldView::GetType;
	};
	const FCopy&	 ValueCopy	  = static_cast<const FCopy&>(Value);
	const MemoryView SourceView	  = ValueCopy.GetPayloadView();
	const uint64_t	 TargetOffset = OutData.size();
	OutData.resize(TargetOffset + SourceView.GetSize());
	memcpy(OutData.data() + TargetOffset, SourceView.GetData(), SourceView.GetSize());
	return CbFieldTypeOps::GetType(ValueCopy.GetType());
}

CbWriter::CbWriter()
{
	States.emplace_back();
}

CbWriter::CbWriter(const int64_t InitialSize) : CbWriter()
{
	Data.reserve(InitialSize);
}

CbWriter::~CbWriter()
{
}

void
CbWriter::Reset()
{
	Data.resize(0);
	States.resize(0);
	States.emplace_back();
}

CbFieldIterator
CbWriter::Save()
{
	const uint64_t			  Size	 = GetSaveSize();
	UniqueBuffer			  Buffer = UniqueBuffer::Alloc(Size);
	const CbFieldViewIterator Output = Save(MutableMemoryView(Buffer.GetData(), Buffer.GetSize()));
	SharedBuffer			  SharedBuf(std::move(Buffer));

	return CbFieldIterator::MakeRangeView(Output, SharedBuf);
}

CbFieldViewIterator
CbWriter::Save(const MutableMemoryView Buffer)
{
	ZEN_ASSERT(States.size() == 1 && States.back().Flags == StateFlags::None);
	// TEXT("It is invalid to save while there are incomplete write operations."));
	ZEN_ASSERT(Data.size() > 0);  // TEXT("It is invalid to save when nothing has been written."));
	ZEN_ASSERT(Buffer.GetSize() == Data.size());
	// TEXT("Buffer is %" UINT64_FMT " bytes but %" INT64_FMT " is required."),
	// Buffer.GetSize(),
	// Data.Num());
	memcpy(Buffer.GetData(), Data.data(), Data.size());
	return CbFieldViewIterator::MakeRange(Buffer);
}

void
CbWriter::Save(BinaryWriter& Writer)
{
	ZEN_ASSERT(States.size() == 1 && States.back().Flags == StateFlags::None);
	// TEXT("It is invalid to save while there are incomplete write operations."));
	ZEN_ASSERT(Data.size() > 0);  // TEXT("It is invalid to save when nothing has been written."));
	Writer.Write(Data.data(), Data.size());
}

uint64_t
CbWriter::GetSaveSize() const
{
	return Data.size();
}

void
CbWriter::BeginField()
{
	WriterState& State = States.back();
	if ((State.Flags & StateFlags::Field) == StateFlags::None)
	{
		State.Flags |= StateFlags::Field;
		State.Offset = Data.size();
		Data.push_back(0);
	}
	else
	{
		ZEN_ASSERT((State.Flags & StateFlags::Name) == StateFlags::Name);
		// TEXT("A new field cannot be written until the previous field '%.*hs' is finished."),
		// GetActiveName().Len(),
		// GetActiveName().GetData());
	}
}

void
CbWriter::EndField(CbFieldType Type)
{
	WriterState& State = States.back();

	if ((State.Flags & StateFlags::Name) == StateFlags::Name)
	{
		Type |= CbFieldType::HasFieldName;
	}
	else
	{
		ZEN_ASSERT((State.Flags & StateFlags::Object) == StateFlags::None);
		// TEXT("It is invalid to write an object field without a unique non-empty name."));
	}

	if (State.Count == 0)
	{
		State.UniformType = Type;
	}
	else if (State.UniformType != Type)
	{
		State.UniformType = CbFieldType::None;
	}

	State.Flags &= ~(StateFlags::Name | StateFlags::Field);
	++State.Count;
	Data[State.Offset] = uint8_t(Type);
}

ZEN_NOINLINE
CbWriter&
CbWriter::SetName(const std::string_view Name)
{
	WriterState& State = States.back();
	ZEN_ASSERT((State.Flags & StateFlags::Array) != StateFlags::Array);
	// TEXT("It is invalid to write a name for an array field. Name '%.*hs'"),
	// Name.Len(),
	// Name.GetData());
	ZEN_ASSERT(!Name.empty());
	// TEXT("%s"),
	//(State.Flags & EStateFlags::Object) == EStateFlags::Object
	// ? TEXT("It is invalid to write an empty name for an object field. Specify a unique non-empty name.")
	// : TEXT("It is invalid to write an empty name for a top-level field. Specify a name or avoid this call."));
	ZEN_ASSERT((State.Flags & (StateFlags::Name | StateFlags::Field)) == StateFlags::None);
	// TEXT("A new field '%.*hs' cannot be written until the previous field '%.*hs' is finished."),
	// Name.Len(),
	// Name.GetData(),
	// GetActiveName().Len(),
	// GetActiveName().GetData());

	BeginField();
	State.Flags |= StateFlags::Name;
	const uint32_t NameLenByteCount = MeasureVarUInt(uint32_t(Name.size()));
	const int64_t  NameLenOffset	= Data.size();
	Data.resize(NameLenOffset + NameLenByteCount);

	WriteVarUInt(uint64_t(Name.size()), Data.data() + NameLenOffset);

	const uint8_t* NamePtr = reinterpret_cast<const uint8_t*>(Name.data());
	Data.insert(Data.end(), NamePtr, NamePtr + Name.size());
	return *this;
}

void
CbWriter::SetNameOrAddString(const std::string_view NameOrValue)
{
	// A name is only written if it would begin a new field inside of an object.
	if ((States.back().Flags & (StateFlags::Name | StateFlags::Field | StateFlags::Object)) == StateFlags::Object)
	{
		SetName(NameOrValue);
	}
	else
	{
		AddString(NameOrValue);
	}
}

std::string_view
CbWriter::GetActiveName() const
{
	const WriterState& State = States.back();
	if ((State.Flags & StateFlags::Name) == StateFlags::Name)
	{
		const uint8_t* const EncodedName = Data.data() + State.Offset + sizeof(CbFieldType);
		uint32_t			 NameLenByteCount;
		const uint64_t		 NameLen		= ReadVarUInt(EncodedName, NameLenByteCount);
		const size_t		 ClampedNameLen = std::clamp<uint64_t>(NameLen, 0, ~uint64_t(0));
		return std::string_view(reinterpret_cast<const char*>(EncodedName + NameLenByteCount), ClampedNameLen);
	}
	return std::string_view();
}

void
CbWriter::MakeFieldsUniform(const int64_t FieldBeginOffset, const int64_t FieldEndOffset)
{
	MutableMemoryView SourceView(Data.data() + FieldBeginOffset, uint64_t(FieldEndOffset - FieldBeginOffset));
	MutableMemoryView TargetView = SourceView;
	TargetView.RightChopInline(sizeof(CbFieldType));

	while (!SourceView.IsEmpty())
	{
		const uint64_t FieldSize = MeasureCompactBinary(SourceView) - sizeof(CbFieldType);
		SourceView.RightChopInline(sizeof(CbFieldType));
		if (TargetView.GetData() != SourceView.GetData())
		{
			memmove(TargetView.GetData(), SourceView.GetData(), FieldSize);
		}
		SourceView.RightChopInline(FieldSize);
		TargetView.RightChopInline(FieldSize);
	}

	if (!TargetView.IsEmpty())
	{
		const auto EraseBegin = Data.begin() + (FieldEndOffset - TargetView.GetSize());
		const auto EraseEnd	  = EraseBegin + TargetView.GetSize();

		Data.erase(EraseBegin, EraseEnd);
	}
}

void
CbWriter::AddField(const CbFieldView& Value)
{
	ZEN_ASSERT(Value.HasValue());  // , TEXT("It is invalid to write a field with no value."));
	BeginField();
	EndField(AppendCompactBinary(Value, Data));
}

void
CbWriter::AddField(const CbField& Value)
{
	AddField(CbFieldView(Value));
}

void
CbWriter::BeginObject()
{
	BeginField();
	States.push_back(WriterState());
	States.back().Flags |= StateFlags::Object;
}

void
CbWriter::EndObject()
{
	ZEN_ASSERT(States.size() > 1 && (States.back().Flags & StateFlags::Object) == StateFlags::Object);

	// TEXT("It is invalid to end an object when an object is not at the top of the stack."));
	ZEN_ASSERT((States.back().Flags & StateFlags::Field) == StateFlags::None);
	// TEXT("It is invalid to end an object until the previous field is finished."));

	const bool	   bUniform = IsUniformType(States.back().UniformType);
	const uint64_t Count	= States.back().Count;
	States.pop_back();

	// Calculate the offset of the payload.
	const WriterState& State		 = States.back();
	int64_t			   PayloadOffset = State.Offset + 1;
	if ((State.Flags & StateFlags::Name) == StateFlags::Name)
	{
		uint32_t	   NameLenByteCount;
		const uint64_t NameLen = ReadVarUInt(Data.data() + PayloadOffset, NameLenByteCount);
		PayloadOffset += NameLen + NameLenByteCount;
	}

	// Remove redundant field types for uniform objects.
	if (bUniform && Count > 1)
	{
		MakeFieldsUniform(PayloadOffset, Data.size());
	}

	// Insert the object size.
	const uint64_t Size			 = uint64_t(Data.size() - PayloadOffset);
	const uint32_t SizeByteCount = MeasureVarUInt(Size);
	Data.insert(Data.begin() + PayloadOffset, SizeByteCount, 0);
	WriteVarUInt(Size, Data.data() + PayloadOffset);

	EndField(bUniform ? CbFieldType::UniformObject : CbFieldType::Object);
}

void
CbWriter::AddObject(const CbObjectView& Value)
{
	BeginField();
	EndField(AppendCompactBinary(Value.AsFieldView(), Data));
}

void
CbWriter::AddObject(const CbObject& Value)
{
	AddObject(CbObjectView(Value));
}

ZEN_NOINLINE
void
CbWriter::BeginArray()
{
	BeginField();
	States.push_back(WriterState());
	States.back().Flags |= StateFlags::Array;
}

void
CbWriter::EndArray()
{
	ZEN_ASSERT(States.size() > 1 && (States.back().Flags & StateFlags::Array) == StateFlags::Array);
	// TEXT("Invalid attempt to end an array when an array is not at the top of the stack."));
	ZEN_ASSERT((States.back().Flags & StateFlags::Field) == StateFlags::None);
	// TEXT("It is invalid to end an array until the previous field is finished."));
	const bool	   bUniform = IsUniformType(States.back().UniformType);
	const uint64_t Count	= States.back().Count;
	States.pop_back();

	// Calculate the offset of the payload.
	const WriterState& State		 = States.back();
	int64_t			   PayloadOffset = State.Offset + 1;
	if ((State.Flags & StateFlags::Name) == StateFlags::Name)
	{
		uint32_t	   NameLenByteCount;
		const uint64_t NameLen = ReadVarUInt(Data.data() + PayloadOffset, NameLenByteCount);
		PayloadOffset += NameLen + NameLenByteCount;
	}

	// Remove redundant field types for uniform arrays.
	if (bUniform && Count > 1)
	{
		MakeFieldsUniform(PayloadOffset, Data.size());
	}

	// Insert the array size and field count.
	const uint32_t CountByteCount = MeasureVarUInt(Count);
	const uint64_t Size			  = uint64_t(Data.size() - PayloadOffset) + CountByteCount;
	const uint32_t SizeByteCount  = MeasureVarUInt(Size);
	Data.insert(Data.begin() + PayloadOffset, SizeByteCount + CountByteCount, 0);
	WriteVarUInt(Size, Data.data() + PayloadOffset);
	WriteVarUInt(Count, Data.data() + PayloadOffset + SizeByteCount);

	EndField(bUniform ? CbFieldType::UniformArray : CbFieldType::Array);
}

void
CbWriter::AddArray(const CbArrayView& Value)
{
	BeginField();
	EndField(AppendCompactBinary(Value.AsFieldView(), Data));
}

void
CbWriter::AddArray(const CbArray& Value)
{
	AddArray(CbArrayView(Value));
}

void
CbWriter::AddNull()
{
	BeginField();
	EndField(CbFieldType::Null);
}

void
CbWriter::AddBinary(const void* const Value, const uint64_t Size)
{
	BeginField();
	const uint32_t SizeByteCount = MeasureVarUInt(Size);
	const int64_t  SizeOffset	 = Data.size();
	Data.resize(Data.size() + SizeByteCount);
	WriteVarUInt(Size, Data.data() + SizeOffset);
	Data.insert(Data.end(), static_cast<const uint8_t*>(Value), static_cast<const uint8_t*>(Value) + Size);
	EndField(CbFieldType::Binary);
}

void
CbWriter::AddBinary(IoBuffer Buffer)
{
	AddBinary(Buffer.Data(), Buffer.Size());
}

void
CbWriter::AddBinary(SharedBuffer Buffer)
{
	AddBinary(Buffer.GetData(), Buffer.GetSize());
}

void
CbWriter::AddString(const std::string_view Value)
{
	BeginField();
	const uint64_t Size			 = uint64_t(Value.size());
	const uint32_t SizeByteCount = MeasureVarUInt(Size);
	const int64_t  Offset		 = Data.size();

	Data.resize(Offset + SizeByteCount + Size);

	uint8_t* StringData = Data.data() + Offset;
	WriteVarUInt(Size, StringData);
	StringData += SizeByteCount;
	if (Size > 0)
	{
		memcpy(StringData, Value.data(), Value.size() * sizeof(char));
	}
	EndField(CbFieldType::String);
}

void
CbWriter::AddString(const std::wstring_view Value)
{
	BeginField();
	ExtendableStringBuilder<128> Utf8;
	WideToUtf8(Value, Utf8);

	const uint32_t Size			 = uint32_t(Utf8.Size());
	const uint32_t SizeByteCount = MeasureVarUInt(Size);
	const int64_t  Offset		 = Data.size();
	Data.resize(Offset + SizeByteCount + Size);
	uint8_t* StringData = Data.data() + Offset;
	WriteVarUInt(Size, StringData);
	StringData += SizeByteCount;
	if (Size > 0)
	{
		memcpy(reinterpret_cast<char*>(StringData), Utf8.Data(), Utf8.Size());
	}
	EndField(CbFieldType::String);
}

ZEN_NOINLINE
void
CbWriter::AddInteger(const int32_t Value)
{
	if (Value >= 0)
	{
		return AddInteger(uint32_t(Value));
	}
	BeginField();
	const uint32_t Magnitude		  = ~uint32_t(Value);
	const uint32_t MagnitudeByteCount = MeasureVarUInt(Magnitude);
	const int64_t  Offset			  = Data.size();
	Data.resize(Offset + MagnitudeByteCount);
	WriteVarUInt(Magnitude, Data.data() + Offset);
	EndField(CbFieldType::IntegerNegative);
}

void
CbWriter::AddInteger(const int64_t Value)
{
	if (Value >= 0)
	{
		return AddInteger(uint64_t(Value));
	}
	BeginField();
	const uint64_t Magnitude		  = ~uint64_t(Value);
	const uint32_t MagnitudeByteCount = MeasureVarUInt(Magnitude);
	const uint64_t Offset			  = AddUninitialized(Data, MagnitudeByteCount);
	WriteVarUInt(Magnitude, Data.data() + Offset);
	EndField(CbFieldType::IntegerNegative);
}

ZEN_NOINLINE
void
CbWriter::AddInteger(const uint32_t Value)
{
	BeginField();
	const uint32_t ValueByteCount = MeasureVarUInt(Value);
	const uint64_t Offset		  = AddUninitialized(Data, ValueByteCount);
	WriteVarUInt(Value, Data.data() + Offset);
	EndField(CbFieldType::IntegerPositive);
}

ZEN_NOINLINE
void
CbWriter::AddInteger(const uint64_t Value)
{
	BeginField();
	const uint32_t ValueByteCount = MeasureVarUInt(Value);
	const uint64_t Offset		  = AddUninitialized(Data, ValueByteCount);
	WriteVarUInt(Value, Data.data() + Offset);
	EndField(CbFieldType::IntegerPositive);
}

ZEN_NOINLINE
void
CbWriter::AddFloat(const float Value)
{
	BeginField();
	const uint32_t RawValue = FromNetworkOrder(reinterpret_cast<const uint32_t&>(Value));
	Append(Data, reinterpret_cast<const uint8_t*>(&RawValue), sizeof(uint32_t));
	EndField(CbFieldType::Float32);
}

ZEN_NOINLINE
void
CbWriter::AddFloat(const double Value)
{
	const float Value32 = float(Value);
	if (Value == double(Value32))
	{
		return AddFloat(Value32);
	}
	BeginField();
	const uint64_t RawValue = FromNetworkOrder(reinterpret_cast<const uint64_t&>(Value));
	Append(Data, reinterpret_cast<const uint8_t*>(&RawValue), sizeof(uint64_t));
	EndField(CbFieldType::Float64);
}

ZEN_NOINLINE
void
CbWriter::AddBool(const bool bValue)
{
	BeginField();
	EndField(bValue ? CbFieldType::BoolTrue : CbFieldType::BoolFalse);
}

ZEN_NOINLINE
void
CbWriter::AddCompactBinaryAttachment(const IoHash& Value)
{
	BeginField();
	Append(Data, Value.Hash, sizeof Value.Hash);
	EndField(CbFieldType::CompactBinaryAttachment);
}

ZEN_NOINLINE
void
CbWriter::AddBinaryAttachment(const IoHash& Value)
{
	BeginField();
	Append(Data, Value.Hash, sizeof Value.Hash);
	EndField(CbFieldType::BinaryAttachment);
}

ZEN_NOINLINE
void
CbWriter::AddAttachment(const CbAttachment& Attachment)
{
	BeginField();
	const IoHash& Value = Attachment.GetHash();
	Append(Data, Value.Hash, sizeof Value.Hash);
	EndField(CbFieldType::BinaryAttachment);
}

ZEN_NOINLINE
void
CbWriter::AddHash(const IoHash& Value)
{
	BeginField();
	Append(Data, Value.Hash, sizeof Value.Hash);
	EndField(CbFieldType::Hash);
}

void
CbWriter::AddUuid(const Guid& Value)
{
	const auto AppendSwappedBytes = [this](uint32_t In) {
		In = FromNetworkOrder(In);
		Append(Data, reinterpret_cast<const uint8_t*>(&In), sizeof In);
	};
	BeginField();
	AppendSwappedBytes(Value.A);
	AppendSwappedBytes(Value.B);
	AppendSwappedBytes(Value.C);
	AppendSwappedBytes(Value.D);
	EndField(CbFieldType::Uuid);
}

void
CbWriter::AddObjectId(const Oid& Value)
{
	BeginField();
	Append(Data, reinterpret_cast<const uint8_t*>(&Value.OidBits), sizeof Value.OidBits);
	EndField(CbFieldType::ObjectId);
}

void
CbWriter::AddDateTimeTicks(const int64_t Ticks)
{
	BeginField();
	const uint64_t RawValue = FromNetworkOrder(uint64_t(Ticks));
	Append(Data, reinterpret_cast<const uint8_t*>(&RawValue), sizeof(uint64_t));
	EndField(CbFieldType::DateTime);
}

void
CbWriter::AddDateTime(const DateTime Value)
{
	AddDateTimeTicks(Value.GetTicks());
}

void
CbWriter::AddTimeSpanTicks(const int64_t Ticks)
{
	BeginField();
	const uint64_t RawValue = FromNetworkOrder(uint64_t(Ticks));
	Append(Data, reinterpret_cast<const uint8_t*>(&RawValue), sizeof(uint64_t));
	EndField(CbFieldType::TimeSpan);
}

void
CbWriter::AddTimeSpan(const TimeSpan Value)
{
	AddTimeSpanTicks(Value.GetTicks());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CbWriter&
operator<<(CbWriter& Writer, const DateTime Value)
{
	Writer.AddDateTime(Value);
	return Writer;
}

CbWriter&
operator<<(CbWriter& Writer, const TimeSpan Value)
{
	Writer.AddTimeSpan(Value);
	return Writer;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void
usonbuilder_forcelink()
{
}

doctest::String
toString(const DateTime&)
{
	// TODO:implement
	return "";
}

doctest::String
toString(const TimeSpan&)
{
	// TODO:implement
	return "";
}

TEST_CASE("usonbuilder.object")
{
	using namespace std::literals;

	FixedCbWriter<256> Writer;

	SUBCASE("EmptyObject")
	{
		Writer.BeginObject();
		Writer.EndObject();
		CbField Field = Writer.Save();

		CHECK(ValidateCompactBinary(Field.GetBuffer(), CbValidateMode::All) == CbValidateError::None);
		CHECK(Field.IsObject() == true);
		CHECK(Field.AsObjectView().CreateViewIterator().HasValue() == false);
	}

	SUBCASE("NamedEmptyObject")
	{
		Writer.SetName("Object"sv);
		Writer.BeginObject();
		Writer.EndObject();
		CbField Field = Writer.Save();

		CHECK(ValidateCompactBinary(Field.GetBuffer(), CbValidateMode::All) == CbValidateError::None);
		CHECK(Field.IsObject() == true);
		CHECK(Field.AsObjectView().CreateViewIterator().HasValue() == false);
	}

	SUBCASE("BasicObject")
	{
		Writer.BeginObject();
		Writer.SetName("Integer"sv).AddInteger(0);
		Writer.SetName("Float"sv).AddFloat(0.0f);
		Writer.EndObject();
		CbField Field = Writer.Save();

		CHECK(ValidateCompactBinary(Field.GetBuffer(), CbValidateMode::All) == CbValidateError::None);
		CHECK(Field.IsObject() == true);

		CbObjectView Object = Field.AsObjectView();
		CHECK(Object["Integer"sv].IsInteger() == true);
		CHECK(Object["Float"sv].IsFloat() == true);
	}

	SUBCASE("UniformObject")
	{
		Writer.BeginObject();
		Writer.SetName("Field1"sv).AddInteger(0);
		Writer.SetName("Field2"sv).AddInteger(1);
		Writer.EndObject();
		CbField Field = Writer.Save();

		CHECK(ValidateCompactBinary(Field.GetBuffer(), CbValidateMode::All) == CbValidateError::None);
		CHECK(Field.IsObject() == true);

		CbObjectView Object = Field.AsObjectView();
		CHECK(Object["Field1"sv].IsInteger() == true);
		CHECK(Object["Field2"sv].IsInteger() == true);
	}
}

TEST_CASE("usonbuilder.array")
{
	using namespace std::literals;

	FixedCbWriter<256> Writer;

	SUBCASE("EmptyArray")
	{
		Writer.BeginArray();
		Writer.EndArray();
		CbField Field = Writer.Save();

		CHECK(ValidateCompactBinary(Field.GetBuffer(), CbValidateMode::All) == CbValidateError::None);
		CHECK(Field.IsArray() == true);
		CHECK(Field.AsArrayView().Num() == 0);
	}

	SUBCASE("NamedEmptyArray")
	{
		Writer.SetName("Array"sv);
		Writer.BeginArray();
		Writer.EndArray();
		CbField Field = Writer.Save();

		CHECK(ValidateCompactBinary(Field.GetBuffer(), CbValidateMode::All) == CbValidateError::None);
		CHECK(Field.IsArray() == true);
		CHECK(Field.AsArrayView().Num() == 0);
	}

	SUBCASE("BasicArray")
	{
		Writer.BeginArray();
		Writer.AddInteger(0);
		Writer.AddFloat(0.0f);
		Writer.EndArray();
		CbField Field = Writer.Save();

		CHECK(ValidateCompactBinary(Field.GetBuffer(), CbValidateMode::All) == CbValidateError::None);
		CHECK(Field.IsArray() == true);
		CbFieldViewIterator Iterator = Field.AsArrayView().CreateViewIterator();
		CHECK(Iterator.IsInteger() == true);
		++Iterator;
		CHECK(Iterator.IsFloat() == true);
		++Iterator;
		CHECK(Iterator.HasValue() == false);
	}

	SUBCASE("UniformArray")
	{
		Writer.BeginArray();
		Writer.AddInteger(0);
		Writer.AddInteger(1);
		Writer.EndArray();

		CbField Field = Writer.Save();

		CHECK(ValidateCompactBinary(Field.GetBuffer(), CbValidateMode::All) == CbValidateError::None);
		CHECK(Field.IsArray() == true);
		CbFieldViewIterator Iterator = Field.AsArrayView().CreateViewIterator();
		CHECK(Iterator.IsInteger() == true);
		++Iterator;
		CHECK(Iterator.IsInteger() == true);
		++Iterator;
		CHECK(Iterator.HasValue() == false);
	}
}

TEST_CASE("usonbuilder.null")
{
	using namespace std::literals;

	FixedCbWriter<256> Writer;

	SUBCASE("Null")
	{
		Writer.AddNull();
		CbField Field = Writer.Save();
		CHECK(ValidateCompactBinary(Field.GetBuffer(), CbValidateMode::All) == CbValidateError::None);
		CHECK(Field.HasName() == false);
		CHECK(Field.IsNull() == true);
	}

	SUBCASE("NullWithName")
	{
		Writer.SetName("Null"sv);
		Writer.AddNull();
		CbField Field = Writer.Save();
		CHECK(ValidateCompactBinary(Field.GetBuffer(), CbValidateMode::All) == CbValidateError::None);
		CHECK(Field.HasName() == true);
		CHECK(Field.GetName().compare("Null"sv) == 0);
		CHECK(Field.IsNull() == true);
	}

	SUBCASE("Null Array/Object Uniformity")
	{
		Writer.BeginArray();
		Writer.AddNull();
		Writer.AddNull();
		Writer.AddNull();
		Writer.EndArray();

		Writer.BeginObject();
		Writer.SetName("N1"sv).AddNull();
		Writer.SetName("N2"sv).AddNull();
		Writer.SetName("N3"sv).AddNull();
		Writer.EndObject();

		CbFieldIterator Fields = Writer.Save();

		CHECK(ValidateCompactBinary(Fields.GetBuffer(), CbValidateMode::All) == CbValidateError::None);
	}

	SUBCASE("Null with Save(Buffer)")
	{
		constexpr int NullCount = 3;
		for (int Index = 0; Index < NullCount; ++Index)
		{
			Writer.AddNull();
		}
		uint8_t				Buffer[NullCount]{};
		MutableMemoryView	BufferView(Buffer, sizeof Buffer);
		CbFieldViewIterator Fields = Writer.Save(BufferView);

		CHECK(ValidateCompactBinaryRange(BufferView, CbValidateMode::All) == CbValidateError::None);

		for (int Index = 0; Index < NullCount; ++Index)
		{
			CHECK(Fields.IsNull() == true);
			++Fields;
		}
		CHECK(Fields.HasValue() == false);
	}
}

TEST_CASE("usonbuilder.binary")
{
	using namespace std::literals;

	FixedCbWriter<256> Writer;
}

TEST_CASE("usonbuilder.string")
{
	using namespace std::literals;

	FixedCbWriter<256> Writer;

	SUBCASE("Empty Strings")
	{
		Writer.AddString(std::string_view());
		Writer.AddString(std::wstring_view());

		CbFieldIterator Fields = Writer.Save();

		CHECK(ValidateCompactBinary(Fields.GetBuffer(), CbValidateMode::All) == CbValidateError::None);

		for (CbFieldView Field : Fields)
		{
			CHECK(Field.HasName() == false);
			CHECK(Field.IsString() == true);
			CHECK(Field.AsString().empty() == true);
		}
	}

	SUBCASE("Test Basic Strings")
	{
		Writer.SetName("String"sv).AddString("Value"sv);
		Writer.SetName("String"sv).AddString(L"Value"sv);

		CbFieldIterator Fields = Writer.Save();

		CHECK(ValidateCompactBinary(Fields.GetBuffer(), CbValidateMode::All) == CbValidateError::None);

		for (CbFieldView Field : Fields)
		{
			CHECK(Field.GetName().compare("String"sv) == 0);
			CHECK(Field.HasName() == true);
			CHECK(Field.IsString() == true);
			CHECK(Field.AsString().compare("Value"sv) == 0);
		}
	}

	SUBCASE("Long Strings")
	{
		constexpr int				DotCount = 256;
		StringBuilder<DotCount + 1> Dots;
		for (int Index = 0; Index < DotCount; ++Index)
		{
			Dots.Append('.');
		}
		Writer.AddString(Dots);
		Writer.AddString(std::wstring().append(256, L'.'));
		CbFieldIterator Fields = Writer.Save();

		CHECK(ValidateCompactBinary(Fields.GetBuffer(), CbValidateMode::All) == CbValidateError::None);

		for (CbFieldView Field : Fields)
		{
			CHECK((Field.AsString() == std::string_view(Dots)));
		}
	}

	SUBCASE("Non-ASCII String")
	{
		wchar_t Value[2] = {0xd83d, 0xde00};
		Writer.AddString("\xf0\x9f\x98\x80"sv);
		Writer.AddString(std::wstring_view(Value, ZEN_ARRAY_COUNT(Value)));
		CbFieldIterator Fields = Writer.Save();

		CHECK(ValidateCompactBinary(Fields.GetBuffer(), CbValidateMode::All) == CbValidateError::None);

		for (CbFieldView Field : Fields)
		{
			CHECK((Field.AsString() == "\xf0\x9f\x98\x80"sv));
		}
	}
}

TEST_CASE("usonbuilder.integer")
{
	using namespace std::literals;

	FixedCbWriter<256> Writer;

	auto TestInt32 = [&Writer](int32_t Value) {
		Writer.Reset();
		Writer.AddInteger(Value);
		CbField Field = Writer.Save();

		CHECK(ValidateCompactBinary(Field.GetBuffer(), CbValidateMode::All) == CbValidateError::None);

		CHECK(Field.AsInt32() == Value);
		CHECK(Field.HasError() == false);
	};

	auto TestUInt32 = [&Writer](uint32_t Value) {
		Writer.Reset();
		Writer.AddInteger(Value);
		CbField Field = Writer.Save();

		CHECK(ValidateCompactBinary(Field.GetBuffer(), CbValidateMode::All) == CbValidateError::None);

		CHECK(Field.AsUInt32() == Value);
		CHECK(Field.HasError() == false);
	};

	auto TestInt64 = [&Writer](int64_t Value) {
		Writer.Reset();
		Writer.AddInteger(Value);
		CbField Field = Writer.Save();

		CHECK(ValidateCompactBinary(Field.GetBuffer(), CbValidateMode::All) == CbValidateError::None);

		CHECK(Field.AsInt64() == Value);
		CHECK(Field.HasError() == false);
	};

	auto TestUInt64 = [&Writer](uint64_t Value) {
		Writer.Reset();
		Writer.AddInteger(Value);
		CbField Field = Writer.Save();

		CHECK(ValidateCompactBinary(Field.GetBuffer(), CbValidateMode::All) == CbValidateError::None);

		CHECK(Field.AsUInt64() == Value);
		CHECK(Field.HasError() == false);
	};

	TestUInt32(uint32_t(0x00));
	TestUInt32(uint32_t(0x7f));
	TestUInt32(uint32_t(0x80));
	TestUInt32(uint32_t(0xff));
	TestUInt32(uint32_t(0x0100));
	TestUInt32(uint32_t(0x7fff));
	TestUInt32(uint32_t(0x8000));
	TestUInt32(uint32_t(0xffff));
	TestUInt32(uint32_t(0x0001'0000));
	TestUInt32(uint32_t(0x7fff'ffff));
	TestUInt32(uint32_t(0x8000'0000));
	TestUInt32(uint32_t(0xffff'ffff));

	TestUInt64(uint64_t(0x0000'0001'0000'0000));
	TestUInt64(uint64_t(0x7fff'ffff'ffff'ffff));
	TestUInt64(uint64_t(0x8000'0000'0000'0000));
	TestUInt64(uint64_t(0xffff'ffff'ffff'ffff));

	TestInt32(int32_t(0x01));
	TestInt32(int32_t(0x80));
	TestInt32(int32_t(0x81));
	TestInt32(int32_t(0x8000));
	TestInt32(int32_t(0x8001));
	TestInt32(int32_t(0x7fff'ffff));
	TestInt32(int32_t(0x8000'0000));
	TestInt32(int32_t(0x8000'0001));

	TestInt64(int64_t(0x0000'0001'0000'0000));
	TestInt64(int64_t(0x8000'0000'0000'0000));
	TestInt64(int64_t(0x7fff'ffff'ffff'ffff));
	TestInt64(int64_t(0x8000'0000'0000'0001));
	TestInt64(int64_t(0xffff'ffff'ffff'ffff));
}

TEST_CASE("usonbuilder.float")
{
	using namespace std::literals;

	FixedCbWriter<256> Writer;

	SUBCASE("Float32")
	{
		constexpr float Values[] = {
			0.0f,
			1.0f,
			-1.0f,
			3.14159265358979323846f,  // PI
			3.402823466e+38f,		  // FLT_MAX
			1.175494351e-38f		  // FLT_MIN
		};

		for (float Value : Values)
		{
			Writer.AddFloat(Value);
		}
		CbFieldIterator Fields = Writer.Save();

		CHECK(ValidateCompactBinary(Fields.GetBuffer(), CbValidateMode::All) == CbValidateError::None);

		const float* CheckValue = Values;
		for (CbFieldView Field : Fields)
		{
			CHECK(Field.AsFloat() == *CheckValue++);
			CHECK(Field.HasError() == false);
		}
	}

	SUBCASE("Float64")
	{
		constexpr double Values[] = {
			0.0f,
			1.0f,
			-1.0f,
			3.14159265358979323846,	 // PI
			1.9999998807907104,
			1.9999999403953552,
			3.4028234663852886e38,
			6.8056469327705771e38,
			2.2250738585072014e-308,  // DBL_MIN
			1.7976931348623158e+308	  // DBL_MAX
		};

		for (double Value : Values)
		{
			Writer.AddFloat(Value);
		}

		CbFieldIterator Fields = Writer.Save();

		CHECK(ValidateCompactBinary(Fields.GetBuffer(), CbValidateMode::All) == CbValidateError::None);

		const double* CheckValue = Values;
		for (CbFieldView Field : Fields)
		{
			CHECK(Field.AsDouble() == *CheckValue++);
			CHECK(Field.HasError() == false);
		}
	}
}

TEST_CASE("usonbuilder.bool")
{
	using namespace std::literals;

	FixedCbWriter<256> Writer;

	SUBCASE("Bool")
	{
		Writer.AddBool(true);
		Writer.AddBool(false);

		CbFieldIterator Fields = Writer.Save();

		CHECK(ValidateCompactBinary(Fields.GetBuffer(), CbValidateMode::All) == CbValidateError::None);

		CHECK(Fields.AsBool() == true);
		CHECK(Fields.HasError() == false);
		++Fields;
		CHECK(Fields.AsBool() == false);
		CHECK(Fields.HasError() == false);
		++Fields;
		CHECK(Fields.HasValue() == false);
	}

	SUBCASE("Bool Array/Object Uniformity")
	{
		Writer.BeginArray();
		Writer.AddBool(false);
		Writer.AddBool(false);
		Writer.AddBool(false);
		Writer.EndArray();

		Writer.BeginObject();
		Writer.SetName("B1"sv).AddBool(false);
		Writer.SetName("B2"sv).AddBool(false);
		Writer.SetName("B3"sv).AddBool(false);
		Writer.EndObject();

		CbFieldIterator Fields = Writer.Save();

		CHECK(ValidateCompactBinary(Fields.GetBuffer(), CbValidateMode::All) == CbValidateError::None);
	}
}

TEST_CASE("usonbuilder.usonattachment")
{
	using namespace std::literals;

	FixedCbWriter<256> Writer;
}

TEST_CASE("usonbuilder.binaryattachment")
{
	using namespace std::literals;

	FixedCbWriter<256> Writer;
}

TEST_CASE("usonbuilder.hash")
{
	using namespace std::literals;

	FixedCbWriter<256> Writer;
}

TEST_CASE("usonbuilder.uuid")
{
	using namespace std::literals;

	FixedCbWriter<256> Writer;
}

TEST_CASE("usonbuilder.datetime")
{
	using namespace std::literals;

	FixedCbWriter<256> Writer;

	const DateTime Values[] = {DateTime(0), DateTime(2020, 5, 13, 15, 10)};
	for (DateTime Value : Values)
	{
		Writer.AddDateTime(Value);
	}

	CbFieldIterator Fields = Writer.Save();

	CHECK(ValidateCompactBinary(Fields.GetBuffer(), CbValidateMode::All) == CbValidateError::None);

	const DateTime* CheckValue = Values;
	for (CbFieldView Field : Fields)
	{
		CHECK(Field.AsDateTime() == *CheckValue++);
		CHECK(Field.HasError() == false);
	}
}

TEST_CASE("usonbuilder.timespan")
{
	using namespace std::literals;

	FixedCbWriter<256> Writer;

	const TimeSpan Values[] = {TimeSpan(0), TimeSpan(1, 2, 4, 8)};
	for (TimeSpan Value : Values)
	{
		Writer.AddTimeSpan(Value);
	}

	CbFieldIterator Fields = Writer.Save();

	CHECK(ValidateCompactBinary(Fields.GetBuffer(), CbValidateMode::All) == CbValidateError::None);

	const TimeSpan* CheckValue = Values;
	for (CbFieldView Field : Fields)
	{
		CHECK(Field.AsTimeSpan() == *CheckValue++);
		CHECK(Field.HasError() == false);
	}
}

TEST_CASE("usonbuilder.complex")
{
	using namespace std::literals;

	FixedCbWriter<256> Writer;

	SUBCASE("complex")
	{
		CbObject Object;

		{
			Writer.BeginObject();

			const uint8_t LocalField[] = {uint8_t(CbFieldType::IntegerPositive | CbFieldType::HasFieldName), 1, 'I', 42};
			Writer.AddField("FieldCopy"sv, CbFieldView(LocalField));
			Writer.AddField("FieldRefCopy"sv, CbField(SharedBuffer::Clone(MakeMemoryView(LocalField))));

			const uint8_t LocalObject[] = {uint8_t(CbFieldType::Object | CbFieldType::HasFieldName),
										   1,
										   'O',
										   7,
										   uint8_t(CbFieldType::IntegerPositive | CbFieldType::HasFieldName),
										   1,
										   'I',
										   42,
										   uint8_t(CbFieldType::Null | CbFieldType::HasFieldName),
										   1,
										   'N'};
			Writer.AddObject("ObjectCopy"sv, CbObjectView(LocalObject));
			Writer.AddObject("ObjectRefCopy"sv, CbObject(SharedBuffer::Clone(MakeMemoryView(LocalObject))));

			const uint8_t LocalArray[] = {uint8_t(CbFieldType::UniformArray | CbFieldType::HasFieldName),
										  1,
										  'A',
										  4,
										  2,
										  uint8_t(CbFieldType::IntegerPositive),
										  42,
										  21};
			Writer.AddArray("ArrayCopy"sv, CbArrayView(LocalArray));
			Writer.AddArray("ArrayRefCopy"sv, CbArray(SharedBuffer::Clone(MakeMemoryView(LocalArray))));

			Writer.AddNull("Null"sv);

			Writer.BeginObject("Binary"sv);
			{
				Writer.AddBinary("Empty"sv, MemoryView());
				Writer.AddBinary("Value"sv, MakeMemoryView("BinaryValue"));
				Writer.AddBinary("LargeValue"sv, MakeMemoryView(std::wstring().append(256, L'.')));
				Writer.AddBinary("LargeRefValue"sv, SharedBuffer::Clone(MakeMemoryView(std::wstring().append(256, L'!'))));
			}
			Writer.EndObject();

			Writer.BeginObject("Strings"sv);
			{
				Writer.AddString("AnsiString"sv, "AnsiValue"sv);
				Writer.AddString("WideString"sv, std::wstring().append(256, L'.'));
				Writer.AddString("EmptyAnsiString"sv, std::string_view());
				Writer.AddString("EmptyWideString"sv, std::wstring_view());
				Writer.AddString("AnsiStringLiteral", "AnsiValue");
				Writer.AddString("WideStringLiteral", L"AnsiValue");
			}
			Writer.EndObject();

			Writer.BeginArray("Integers"sv);
			{
				Writer.AddInteger(int32_t(-1));
				Writer.AddInteger(int64_t(-1));
				Writer.AddInteger(uint32_t(1));
				Writer.AddInteger(uint64_t(1));
				Writer.AddInteger(std::numeric_limits<int32_t>::min());
				Writer.AddInteger(std::numeric_limits<int32_t>::max());
				Writer.AddInteger(std::numeric_limits<uint32_t>::max());
				Writer.AddInteger(std::numeric_limits<int64_t>::min());
				Writer.AddInteger(std::numeric_limits<int64_t>::max());
				Writer.AddInteger(std::numeric_limits<uint64_t>::max());
			}
			Writer.EndArray();

			Writer.BeginArray("UniformIntegers"sv);
			{
				Writer.AddInteger(0);
				Writer.AddInteger(std::numeric_limits<int32_t>::max());
				Writer.AddInteger(std::numeric_limits<uint32_t>::max());
				Writer.AddInteger(std::numeric_limits<int64_t>::max());
				Writer.AddInteger(std::numeric_limits<uint64_t>::max());
			}
			Writer.EndArray();

			Writer.AddFloat("Float32"sv, 1.0f);
			Writer.AddFloat("Float64as32"sv, 2.0);
			Writer.AddFloat("Float64"sv, 3.0e100);

			Writer.AddBool("False"sv, false);
			Writer.AddBool("True"sv, true);

			Writer.AddCompactBinaryAttachment("CompactBinaryAttachment"sv, IoHash());
			Writer.AddBinaryAttachment("BinaryAttachment"sv, IoHash());
			Writer.AddAttachment("Attachment"sv, CbAttachment());

			Writer.AddHash("Hash"sv, IoHash());
			Writer.AddUuid("Uuid"sv, Guid());

			Writer.AddDateTimeTicks("DateTimeZero"sv, 0);
			Writer.AddDateTime("DateTime2020"sv, DateTime(2020, 5, 13, 15, 10));

			Writer.AddTimeSpanTicks("TimeSpanZero"sv, 0);
			Writer.AddTimeSpan("TimeSpan"sv, TimeSpan(1, 2, 4, 8));

			Writer.BeginObject("NestedObjects"sv);
			{
				Writer.BeginObject("Empty"sv);
				Writer.EndObject();

				Writer.BeginObject("Null"sv);
				Writer.AddNull("Null"sv);
				Writer.EndObject();
			}
			Writer.EndObject();

			Writer.BeginArray("NestedArrays"sv);
			{
				Writer.BeginArray();
				Writer.EndArray();

				Writer.BeginArray();
				Writer.AddNull();
				Writer.AddNull();
				Writer.AddNull();
				Writer.EndArray();

				Writer.BeginArray();
				Writer.AddBool(false);
				Writer.AddBool(false);
				Writer.AddBool(false);
				Writer.EndArray();

				Writer.BeginArray();
				Writer.AddBool(true);
				Writer.AddBool(true);
				Writer.AddBool(true);
				Writer.EndArray();
			}
			Writer.EndArray();

			Writer.BeginArray("ArrayOfObjects"sv);
			{
				Writer.BeginObject();
				Writer.EndObject();

				Writer.BeginObject();
				Writer.AddNull("Null"sv);
				Writer.EndObject();
			}
			Writer.EndArray();

			Writer.BeginArray("LargeArray"sv);
			for (int Index = 0; Index < 256; ++Index)
			{
				Writer.AddInteger(Index - 128);
			}
			Writer.EndArray();

			Writer.BeginArray("LargeUniformArray"sv);
			for (int Index = 0; Index < 256; ++Index)
			{
				Writer.AddInteger(Index);
			}
			Writer.EndArray();

			Writer.BeginArray("NestedUniformArray"sv);
			for (int Index = 0; Index < 16; ++Index)
			{
				Writer.BeginArray();
				for (int Value = 0; Value < 4; ++Value)
				{
					Writer.AddInteger(Value);
				}
				Writer.EndArray();
			}
			Writer.EndArray();

			Writer.EndObject();
			Object = Writer.Save().AsObject();
		}
		CHECK(ValidateCompactBinary(Object.GetBuffer(), CbValidateMode::All) == CbValidateError::None);
	}
}

TEST_CASE("usonbuilder.stream")
{
	using namespace std::literals;

	FixedCbWriter<256> Writer;

	SUBCASE("basic")
	{
		CbObject Object;
		{
			Writer.BeginObject();

			const uint8_t LocalField[] = {uint8_t(CbFieldType::IntegerPositive | CbFieldType::HasFieldName), 1, 'I', 42};
			Writer << "FieldCopy"sv << CbFieldView(LocalField);

			const uint8_t LocalObject[] = {uint8_t(CbFieldType::Object | CbFieldType::HasFieldName),
										   1,
										   'O',
										   7,
										   uint8_t(CbFieldType::IntegerPositive | CbFieldType::HasFieldName),
										   1,
										   'I',
										   42,
										   uint8_t(CbFieldType::Null | CbFieldType::HasFieldName),
										   1,
										   'N'};
			Writer << "ObjectCopy"sv << CbObjectView(LocalObject);

			const uint8_t LocalArray[] = {uint8_t(CbFieldType::UniformArray | CbFieldType::HasFieldName),
										  1,
										  'A',
										  4,
										  2,
										  uint8_t(CbFieldType::IntegerPositive),
										  42,
										  21};
			Writer << "ArrayCopy"sv << CbArrayView(LocalArray);

			Writer << "Null"sv << nullptr;

			Writer << "Strings"sv;
			Writer.BeginObject();
			Writer << "AnsiString"sv
				   << "AnsiValue"sv
				   << "AnsiStringLiteral"sv
				   << "AnsiValue"
				   << "WideString"sv << L"WideValue"sv << "WideStringLiteral"sv << L"WideValue";
			Writer.EndObject();

			Writer << "Integers"sv;
			Writer.BeginArray();
			Writer << int32_t(-1) << int64_t(-1) << uint32_t(1) << uint64_t(1);
			Writer.EndArray();

			Writer << "Float32"sv << 1.0f;
			Writer << "Float64"sv << 2.0;

			Writer << "False"sv << false << "True"sv << true;

			Writer << "Attachment"sv << CbAttachment();

			Writer << "Hash"sv << IoHash();
			Writer << "Uuid"sv << Guid();

			Writer << "DateTime"sv << DateTime(2020, 5, 13, 15, 10);
			Writer << "TimeSpan"sv << TimeSpan(1, 2, 4, 8);

			Writer << "LiteralName" << nullptr;

			Writer.EndObject();
			Object = Writer.Save().AsObject();
		}

		CHECK(ValidateCompactBinary(Object.GetBuffer(), CbValidateMode::All) == CbValidateError::None);
	}
}

}  // namespace zen

