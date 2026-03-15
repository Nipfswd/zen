// Copyright Noah Games, Inc. All Rights Reserved.

#include "zencore/compactbinaryvalidation.h"

#include <zencore/compactbinarypackage.h>
#include <zencore/endian.h>
#include <zencore/memory.h>
#include <zencore/string.h>

#include <algorithm>

#include <doctest/doctest.h>

namespace zen {

namespace CbValidationPrivate {

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

}  // namespace CbValidationPrivate

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Adds the given error(s) to the error mask.
 *
 * This function exists to make validation errors easier to debug by providing one location to set a breakpoint.
 */
ZEN_NOINLINE static void
AddError(CbValidateError& OutError, const CbValidateError InError)
{
	OutError |= InError;
}

/**
 * Validate and read a field type from the view.
 *
 * A type argument with the HasFieldType flag indicates that the type will not be read from the view.
 */
static CbFieldType
ValidateCbFieldType(MemoryView& View, CbValidateMode Mode, CbValidateError& Error, CbFieldType Type = CbFieldType::HasFieldType)
{
	ZEN_UNUSED(Mode);
	if (CbFieldTypeOps::HasFieldType(Type))
	{
		if (View.GetSize() >= 1)
		{
			Type = *static_cast<const CbFieldType*>(View.GetData());
			View += 1;
			if (CbFieldTypeOps::HasFieldType(Type))
			{
				AddError(Error, CbValidateError::InvalidType);
			}
		}
		else
		{
			AddError(Error, CbValidateError::OutOfBounds);
			View.Reset();
			return CbFieldType::None;
		}
	}

	if (CbFieldTypeOps::GetSerializedType(Type) != Type)
	{
		AddError(Error, CbValidateError::InvalidType);
		View.Reset();
	}

	return Type;
}

/**
 * Validate and read an unsigned integer from the view.
 *
 * Modifies the view to start at the end of the value, and adds error flags if applicable.
 */
static uint64_t
ValidateCbUInt(MemoryView& View, CbValidateMode Mode, CbValidateError& Error)
{
	if (View.GetSize() > 0 && View.GetSize() >= MeasureVarUInt(View.GetData()))
	{
		uint32_t	   ValueByteCount;
		const uint64_t Value = ReadVarUInt(View.GetData(), ValueByteCount);
		if (EnumHasAnyFlags(Mode, CbValidateMode::Format) && ValueByteCount > MeasureVarUInt(Value))
		{
			AddError(Error, CbValidateError::InvalidInteger);
		}
		View += ValueByteCount;
		return Value;
	}
	else
	{
		AddError(Error, CbValidateError::OutOfBounds);
		View.Reset();
		return 0;
	}
}

/**
 * Validate a 64-bit floating point value from the view.
 *
 * Modifies the view to start at the end of the value, and adds error flags if applicable.
 */
static void
ValidateCbFloat64(MemoryView& View, CbValidateMode Mode, CbValidateError& Error)
{
	if (View.GetSize() >= sizeof(double))
	{
		if (EnumHasAnyFlags(Mode, CbValidateMode::Format))
		{
			const uint64_t RawValue = FromNetworkOrder(CbValidationPrivate::ReadUnaligned<uint64_t>(View.GetData()));
			const double   Value	= reinterpret_cast<const double&>(RawValue);
			if (Value == double(float(Value)))
			{
				AddError(Error, CbValidateError::InvalidFloat);
			}
		}
		View += sizeof(double);
	}
	else
	{
		AddError(Error, CbValidateError::OutOfBounds);
		View.Reset();
	}
}

/**
 * Validate and read a string from the view.
 *
 * Modifies the view to start at the end of the string, and adds error flags if applicable.
 */
static std::string_view
ValidateCbString(MemoryView& View, CbValidateMode Mode, CbValidateError& Error)
{
	const uint64_t NameSize = ValidateCbUInt(View, Mode, Error);
	if (View.GetSize() >= NameSize)
	{
		const std::string_view Name(static_cast<const char*>(View.GetData()), static_cast<int32_t>(NameSize));
		View += NameSize;
		return Name;
	}
	else
	{
		AddError(Error, CbValidateError::OutOfBounds);
		View.Reset();
		return std::string_view();
	}
}

static CbFieldView ValidateCbField(MemoryView& View, CbValidateMode Mode, CbValidateError& Error, CbFieldType ExternalType);

/** A type that checks whether all validated fields are of the same type. */
class CbUniformFieldsValidator
{
public:
	inline explicit CbUniformFieldsValidator(CbFieldType InExternalType) : ExternalType(InExternalType) {}

	inline CbFieldView ValidateField(MemoryView& View, CbValidateMode Mode, CbValidateError& Error)
	{
		const void* const FieldData = View.GetData();
		if (CbFieldView Field = ValidateCbField(View, Mode, Error, ExternalType))
		{
			++FieldCount;
			if (CbFieldTypeOps::HasFieldType(ExternalType))
			{
				const CbFieldType FieldType = *static_cast<const CbFieldType*>(FieldData);
				if (FieldCount == 1)
				{
					FirstType = FieldType;
				}
				else if (FieldType != FirstType)
				{
					bUniform = false;
				}
			}
			return Field;
		}

		// It may not safe to check for uniformity if the field was invalid.
		bUniform = false;
		return CbFieldView();
	}

	inline bool IsUniform() const { return FieldCount > 0 && bUniform; }

private:
	uint32_t	FieldCount = 0;
	bool		bUniform   = true;
	CbFieldType FirstType  = CbFieldType::None;
	CbFieldType ExternalType;
};

static void
ValidateCbObject(MemoryView& View, CbValidateMode Mode, CbValidateError& Error, CbFieldType ObjectType)
{
	const uint64_t Size		  = ValidateCbUInt(View, Mode, Error);
	MemoryView	   ObjectView = View.Left(Size);
	View += Size;

	if (Size > 0)
	{
		std::vector<std::string_view> Names;

		const bool				 bUniformObject = CbFieldTypeOps::GetType(ObjectType) == CbFieldType::UniformObject;
		const CbFieldType		 ExternalType	= bUniformObject ? ValidateCbFieldType(ObjectView, Mode, Error) : CbFieldType::HasFieldType;
		CbUniformFieldsValidator UniformValidator(ExternalType);
		do
		{
			if (CbFieldView Field = UniformValidator.ValidateField(ObjectView, Mode, Error))
			{
				if (EnumHasAnyFlags(Mode, CbValidateMode::Names))
				{
					if (Field.HasName())
					{
						Names.push_back(Field.GetName());
					}
					else
					{
						AddError(Error, CbValidateError::MissingName);
					}
				}
			}
		} while (!ObjectView.IsEmpty());

		if (EnumHasAnyFlags(Mode, CbValidateMode::Names) && Names.size() > 1)
		{
			std::sort(begin(Names), end(Names), [](std::string_view L, std::string_view R) { return L.compare(R) < 0; });

			for (const std::string_view *NamesIt = Names.data(), *NamesEnd = NamesIt + Names.size() - 1; NamesIt != NamesEnd; ++NamesIt)
			{
				if (NamesIt[0] == NamesIt[1])
				{
					AddError(Error, CbValidateError::DuplicateName);
					break;
				}
			}
		}

		if (!bUniformObject && EnumHasAnyFlags(Mode, CbValidateMode::Format) && UniformValidator.IsUniform())
		{
			AddError(Error, CbValidateError::NonUniformObject);
		}
	}
}

static void
ValidateCbArray(MemoryView& View, CbValidateMode Mode, CbValidateError& Error, CbFieldType ArrayType)
{
	const uint64_t Size		 = ValidateCbUInt(View, Mode, Error);
	MemoryView	   ArrayView = View.Left(Size);
	View += Size;

	const uint64_t			 Count		   = ValidateCbUInt(ArrayView, Mode, Error);
	const uint64_t			 FieldsSize	   = ArrayView.GetSize();
	const bool				 bUniformArray = CbFieldTypeOps::GetType(ArrayType) == CbFieldType::UniformArray;
	const CbFieldType		 ExternalType  = bUniformArray ? ValidateCbFieldType(ArrayView, Mode, Error) : CbFieldType::HasFieldType;
	CbUniformFieldsValidator UniformValidator(ExternalType);

	for (uint64_t Index = 0; Index < Count; ++Index)
	{
		if (CbFieldView Field = UniformValidator.ValidateField(ArrayView, Mode, Error))
		{
			if (Field.HasName() && EnumHasAnyFlags(Mode, CbValidateMode::Names))
			{
				AddError(Error, CbValidateError::ArrayName);
			}
		}
	}

	if (!bUniformArray && EnumHasAnyFlags(Mode, CbValidateMode::Format) && UniformValidator.IsUniform() && FieldsSize > Count)
	{
		AddError(Error, CbValidateError::NonUniformArray);
	}
}

static CbFieldView
ValidateCbField(MemoryView& View, CbValidateMode Mode, CbValidateError& Error, const CbFieldType ExternalType = CbFieldType::HasFieldType)
{
	const MemoryView	   FieldView = View;
	const CbFieldType	   Type		 = ValidateCbFieldType(View, Mode, Error, ExternalType);
	const std::string_view Name		 = CbFieldTypeOps::HasFieldName(Type) ? ValidateCbString(View, Mode, Error) : std::string_view();

	auto ValidateFixedPayload = [&View, &Error](uint32_t PayloadSize) {
		if (View.GetSize() >= PayloadSize)
		{
			View += PayloadSize;
		}
		else
		{
			AddError(Error, CbValidateError::OutOfBounds);
			View.Reset();
		}
	};

	if (EnumHasAnyFlags(Error, CbValidateError::OutOfBounds | CbValidateError::InvalidType))
	{
		return CbFieldView();
	}

	switch (CbFieldType FieldType = CbFieldTypeOps::GetType(Type))
	{
		default:
		case CbFieldType::None:
			AddError(Error, CbValidateError::InvalidType);
			View.Reset();
			break;
		case CbFieldType::Null:
		case CbFieldType::BoolFalse:
		case CbFieldType::BoolTrue:
			if (FieldView == View)
			{
				// Reset the view because a zero-sized field can cause infinite field iteration.
				AddError(Error, CbValidateError::InvalidType);
				View.Reset();
			}
			break;
		case CbFieldType::Object:
		case CbFieldType::UniformObject:
			ValidateCbObject(View, Mode, Error, FieldType);
			break;
		case CbFieldType::Array:
		case CbFieldType::UniformArray:
			ValidateCbArray(View, Mode, Error, FieldType);
			break;
		case CbFieldType::Binary:
			{
				const uint64_t ValueSize = ValidateCbUInt(View, Mode, Error);
				if (View.GetSize() < ValueSize)
				{
					AddError(Error, CbValidateError::OutOfBounds);
					View.Reset();
				}
				else
				{
					View += ValueSize;
				}
				break;
			}
		case CbFieldType::String:
			ValidateCbString(View, Mode, Error);
			break;
		case CbFieldType::IntegerPositive:
			ValidateCbUInt(View, Mode, Error);
			break;
		case CbFieldType::IntegerNegative:
			ValidateCbUInt(View, Mode, Error);
			break;
		case CbFieldType::Float32:
			ValidateFixedPayload(4);
			break;
		case CbFieldType::Float64:
			ValidateCbFloat64(View, Mode, Error);
			break;
		case CbFieldType::CompactBinaryAttachment:
		case CbFieldType::BinaryAttachment:
		case CbFieldType::Hash:
			ValidateFixedPayload(20);
			break;
		case CbFieldType::Uuid:
			ValidateFixedPayload(16);
			break;
		case CbFieldType::DateTime:
		case CbFieldType::TimeSpan:
			ValidateFixedPayload(8);
			break;
		case CbFieldType::ObjectId:
			ValidateFixedPayload(12);
			break;
		case CbFieldType::CustomById:
		case CbFieldType::CustomByName:
			ZEN_NOT_IMPLEMENTED();	// TODO: FIX!
			break;
	}

	if (EnumHasAnyFlags(Error, CbValidateError::OutOfBounds | CbValidateError::InvalidType))
	{
		return CbFieldView();
	}

	return CbFieldView(FieldView.GetData(), ExternalType);
}

static CbFieldView
ValidateCbPackageField(MemoryView& View, CbValidateMode Mode, CbValidateError& Error)
{
	if (View.IsEmpty())
	{
		if (EnumHasAnyFlags(Mode, CbValidateMode::Package))
		{
			AddError(Error, CbValidateError::InvalidPackageFormat);
		}
		return CbFieldView();
	}
	if (CbFieldView Field = ValidateCbField(View, Mode, Error))
	{
		if (Field.HasName() && EnumHasAnyFlags(Mode, CbValidateMode::Package))
		{
			AddError(Error, CbValidateError::InvalidPackageFormat);
		}
		return Field;
	}
	return CbFieldView();
}

static IoHash
ValidateCbPackageAttachment(CbFieldView& Value, MemoryView& View, CbValidateMode Mode, CbValidateError& Error)
{
	const MemoryView ValueView = Value.AsBinaryView();
	if (Value.HasError() && EnumHasAnyFlags(Mode, CbValidateMode::Package))
	{
		if (EnumHasAnyFlags(Mode, CbValidateMode::Package))
		{
			AddError(Error, CbValidateError::InvalidPackageFormat);
		}
	}
	else if (ValueView.GetSize())
	{
		if (CbFieldView HashField = ValidateCbPackageField(View, Mode, Error))
		{
			const IoHash Hash = HashField.AsAttachment();
			if (EnumHasAnyFlags(Mode, CbValidateMode::Package))
			{
				if (HashField.HasError())
				{
					AddError(Error, CbValidateError::InvalidPackageFormat);
				}
				else if (Hash != IoHash::HashMemory(ValueView.GetData(), ValueView.GetSize()))
				{
					AddError(Error, CbValidateError::InvalidPackageHash);
				}
			}
			return Hash;
		}
	}
	return IoHash();
}

static IoHash
ValidateCbPackageObject(CbFieldView& Value, MemoryView& View, CbValidateMode Mode, CbValidateError& Error)
{
	CbObjectView Object = Value.AsObjectView();
	if (Value.HasError())
	{
		if (EnumHasAnyFlags(Mode, CbValidateMode::Package))
		{
			AddError(Error, CbValidateError::InvalidPackageFormat);
		}
	}
	else if (CbFieldView HashField = ValidateCbPackageField(View, Mode, Error))
	{
		const IoHash Hash = HashField.AsAttachment();
		if (EnumHasAnyFlags(Mode, CbValidateMode::Package))
		{
			if (!Object.CreateViewIterator())
			{
				AddError(Error, CbValidateError::NullPackageObject);
			}
			if (HashField.HasError())
			{
				AddError(Error, CbValidateError::InvalidPackageFormat);
			}
			else if (Hash != Value.GetHash())
			{
				AddError(Error, CbValidateError::InvalidPackageHash);
			}
		}
		return Hash;
	}
	return IoHash();
}

CbValidateError
ValidateCompactBinary(MemoryView View, CbValidateMode Mode, CbFieldType Type)
{
	CbValidateError Error = CbValidateError::None;
	if (EnumHasAnyFlags(Mode, CbValidateMode::All))
	{
		ValidateCbField(View, Mode, Error, Type);
		if (!View.IsEmpty() && EnumHasAnyFlags(Mode, CbValidateMode::Padding))
		{
			AddError(Error, CbValidateError::Padding);
		}
	}
	return Error;
}

CbValidateError
ValidateCompactBinaryRange(MemoryView View, CbValidateMode Mode)
{
	CbValidateError Error = CbValidateError::None;
	if (EnumHasAnyFlags(Mode, CbValidateMode::All))
	{
		while (!View.IsEmpty())
		{
			ValidateCbField(View, Mode, Error);
		}
	}
	return Error;
}

CbValidateError
ValidateCompactBinaryAttachment(MemoryView View, CbValidateMode Mode)
{
	CbValidateError Error = CbValidateError::None;
	if (EnumHasAnyFlags(Mode, CbValidateMode::All))
	{
		if (CbFieldView Value = ValidateCbPackageField(View, Mode, Error))
		{
			ValidateCbPackageAttachment(Value, View, Mode, Error);
		}
		if (!View.IsEmpty() && EnumHasAnyFlags(Mode, CbValidateMode::Padding))
		{
			AddError(Error, CbValidateError::Padding);
		}
	}
	return Error;
}

CbValidateError
ValidateCompactBinaryPackage(MemoryView View, CbValidateMode Mode)
{
	std::vector<IoHash> Attachments;
	CbValidateError		Error = CbValidateError::None;
	if (EnumHasAnyFlags(Mode, CbValidateMode::All))
	{
		uint32_t ObjectCount = 0;
		while (CbFieldView Value = ValidateCbPackageField(View, Mode, Error))
		{
			if (Value.IsBinary())
			{
				const IoHash Hash = ValidateCbPackageAttachment(Value, View, Mode, Error);
				if (EnumHasAnyFlags(Mode, CbValidateMode::Package))
				{
					Attachments.push_back(Hash);
					if (Value.AsBinaryView().IsEmpty())
					{
						AddError(Error, CbValidateError::NullPackageAttachment);
					}
				}
			}
			else if (Value.IsObject())
			{
				ValidateCbPackageObject(Value, View, Mode, Error);
				if (++ObjectCount > 1 && EnumHasAnyFlags(Mode, CbValidateMode::Package))
				{
					AddError(Error, CbValidateError::MultiplePackageObjects);
				}
			}
			else if (Value.IsNull())
			{
				break;
			}
			else if (EnumHasAnyFlags(Mode, CbValidateMode::Package))
			{
				AddError(Error, CbValidateError::InvalidPackageFormat);
			}

			if (EnumHasAnyFlags(Error, CbValidateError::OutOfBounds))
			{
				break;
			}
		}

		if (!View.IsEmpty() && EnumHasAnyFlags(Mode, CbValidateMode::Padding))
		{
			AddError(Error, CbValidateError::Padding);
		}

		if (Attachments.size() && EnumHasAnyFlags(Mode, CbValidateMode::Package))
		{
			std::sort(begin(Attachments), end(Attachments));
			for (const IoHash *It = Attachments.data(), *End = It + Attachments.size() - 1; It != End; ++It)
			{
				if (It[0] == It[1])
				{
					AddError(Error, CbValidateError::DuplicateAttachments);
					break;
				}
			}
		}
	}
	return Error;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void
usonvalidation_forcelink()
{
}

TEST_CASE("usonvalidation")
{
	SUBCASE("Basic") {}
}

}  // namespace zen

