// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <zencore/zencore.h>

#include <zencore/compactbinary.h>

#include <zencore/enumflags.h>
#include <zencore/iobuffer.h>
#include <zencore/iohash.h>
#include <zencore/refcount.h>
#include <zencore/sha1.h>

#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <gsl/gsl-lite.hpp>

namespace zen {

class CbAttachment;
class BinaryWriter;

/**
 * A writer for compact binary object, arrays, and fields.
 *
 * The writer produces a sequence of fields that can be saved to a provided memory buffer or into
 * a new owned buffer. The typical use case is to write a single object, which can be accessed by
 * calling Save().AsObjectRef() or Save(Buffer).AsObject().
 *
 * The writer will assert on most incorrect usage and will always produce valid compact binary if
 * provided with valid input. The writer does not check for invalid UTF-8 string encoding, object
 * fields with duplicate names, or invalid compact binary being copied from another source.
 *
 * It is most convenient to use the streaming API for the writer, as demonstrated in the example.
 *
 * When writing a small amount of compact binary data, TCbWriter can be more efficient as it uses
 * a fixed-size stack buffer for storage before spilling onto the heap.
 *
 * @see TCbWriter
 *
 * Example:
 *
 * CbObjectRef WriteObject()
 * {
 *     CbWriter<256> Writer;
 *     Writer.BeginObject();
 *
 *     Writer << "Resize" << true;
 *     Writer << "MaxWidth" << 1024;
 *     Writer << "MaxHeight" << 1024;
 *
 *     Writer.BeginArray();
 *     Writer << "FormatA" << "FormatB" << "FormatC";
 *     Writer.EndArray();
 *
 *     Writer.EndObject();
 *     return Writer.Save().AsObjectRef();
 * }
 */
class CbWriter
{
public:
	ZENCORE_API CbWriter();
	ZENCORE_API ~CbWriter();

	CbWriter(const CbWriter&) = delete;
	CbWriter& operator=(const CbWriter&) = delete;

	/** Empty the writer without releasing any allocated memory. */
	ZENCORE_API void Reset();

	/**
	 * Serialize the field(s) to an owned buffer and return it as an iterator.
	 *
	 * It is not valid to call this function in the middle of writing an object, array, or field.
	 * The writer remains valid for further use when this function returns.
	 */
	ZENCORE_API CbFieldIterator Save();

	/**
	 * Serialize the field(s) to memory.
	 *
	 * It is not valid to call this function in the middle of writing an object, array, or field.
	 * The writer remains valid for further use when this function returns.
	 *
	 * @param Buffer A mutable memory view to write to. Must be exactly GetSaveSize() bytes.
	 * @return An iterator for the field(s) written to the buffer.
	 */
	ZENCORE_API CbFieldViewIterator Save(MutableMemoryView Buffer);

	ZENCORE_API void Save(BinaryWriter& Writer);

	/**
	 * The size of buffer (in bytes) required to serialize the fields that have been written.
	 *
	 * It is not valid to call this function in the middle of writing an object, array, or field.
	 */
	ZENCORE_API uint64_t GetSaveSize() const;

	/**
	 * Sets the name of the next field to be written.
	 *
	 * It is not valid to call this function when writing a field inside an array.
	 * Names must be valid UTF-8 and must be unique within an object.
	 */
	ZENCORE_API CbWriter& SetName(std::string_view Name);

	/** Copy the value (not the name) of an existing field. */
	inline void AddField(std::string_view Name, const CbFieldView& Value)
	{
		SetName(Name);
		AddField(Value);
	}

	ZENCORE_API void AddField(const CbFieldView& Value);

	/** Copy the value (not the name) of an existing field. Holds a reference if owned. */
	inline void AddField(std::string_view Name, const CbField& Value)
	{
		SetName(Name);
		AddField(Value);
	}
	ZENCORE_API void AddField(const CbField& Value);

	/** Begin a new object. Must have a matching call to EndObject. */
	inline void BeginObject(std::string_view Name)
	{
		SetName(Name);
		BeginObject();
	}
	ZENCORE_API void BeginObject();
	/** End an object after its fields have been written. */
	ZENCORE_API void EndObject();

	/** Copy the value (not the name) of an existing object. */
	inline void AddObject(std::string_view Name, const CbObjectView& Value)
	{
		SetName(Name);
		AddObject(Value);
	}
	ZENCORE_API void AddObject(const CbObjectView& Value);
	/** Copy the value (not the name) of an existing object. Holds a reference if owned. */
	inline void AddObject(std::string_view Name, const CbObject& Value)
	{
		SetName(Name);
		AddObject(Value);
	}
	ZENCORE_API void AddObject(const CbObject& Value);

	/** Begin a new array. Must have a matching call to EndArray. */
	inline void BeginArray(std::string_view Name)
	{
		SetName(Name);
		BeginArray();
	}
	ZENCORE_API void BeginArray();
	/** End an array after its fields have been written. */
	ZENCORE_API void EndArray();

	/** Copy the value (not the name) of an existing array. */
	inline void AddArray(std::string_view Name, const CbArrayView& Value)
	{
		SetName(Name);
		AddArray(Value);
	}
	ZENCORE_API void AddArray(const CbArrayView& Value);
	/** Copy the value (not the name) of an existing array. Holds a reference if owned. */
	inline void AddArray(std::string_view Name, const CbArray& Value)
	{
		SetName(Name);
		AddArray(Value);
	}
	ZENCORE_API void AddArray(const CbArray& Value);

	/** Write a null field. */
	inline void AddNull(std::string_view Name)
	{
		SetName(Name);
		AddNull();
	}
	ZENCORE_API void AddNull();

	/** Write a binary field by copying Size bytes from Value. */
	inline void AddBinary(std::string_view Name, const void* Value, uint64_t Size)
	{
		SetName(Name);
		AddBinary(Value, Size);
	}
	ZENCORE_API void AddBinary(const void* Value, uint64_t Size);
	/** Write a binary field by copying the view. */
	inline void AddBinary(std::string_view Name, MemoryView Value)
	{
		SetName(Name);
		AddBinary(Value);
	}
	inline void AddBinary(MemoryView Value) { AddBinary(Value.GetData(), Value.GetSize()); }

	/** Write a binary field by copying the buffer. Holds a reference if owned. */
	inline void AddBinary(std::string_view Name, IoBuffer Value)
	{
		SetName(Name);
		AddBinary(std::move(Value));
	}
	ZENCORE_API void AddBinary(IoBuffer Value);
	ZENCORE_API void AddBinary(SharedBuffer Value);

	/** Write a string field by copying the UTF-8 value. */
	inline void AddString(std::string_view Name, std::string_view Value)
	{
		SetName(Name);
		AddString(Value);
	}
	ZENCORE_API void AddString(std::string_view Value);
	/** Write a string field by converting the UTF-16 value to UTF-8. */
	inline void AddString(std::string_view Name, std::wstring_view Value)
	{
		SetName(Name);
		AddString(Value);
	}
	ZENCORE_API void AddString(std::wstring_view Value);

	/** Write an integer field. */
	inline void AddInteger(std::string_view Name, int32_t Value)
	{
		SetName(Name);
		AddInteger(Value);
	}
	ZENCORE_API void AddInteger(int32_t Value);
	/** Write an integer field. */
	inline void AddInteger(std::string_view Name, int64_t Value)
	{
		SetName(Name);
		AddInteger(Value);
	}
	ZENCORE_API void AddInteger(int64_t Value);
	/** Write an integer field. */
	inline void AddInteger(std::string_view Name, uint32_t Value)
	{
		SetName(Name);
		AddInteger(Value);
	}
	ZENCORE_API void AddInteger(uint32_t Value);
	/** Write an integer field. */
	inline void AddInteger(std::string_view Name, uint64_t Value)
	{
		SetName(Name);
		AddInteger(Value);
	}
	ZENCORE_API void AddInteger(uint64_t Value);

	/** Write a float field from a 32-bit float value. */
	inline void AddFloat(std::string_view Name, float Value)
	{
		SetName(Name);
		AddFloat(Value);
	}
	ZENCORE_API void AddFloat(float Value);

	/** Write a float field from a 64-bit float value. */
	inline void AddFloat(std::string_view Name, double Value)
	{
		SetName(Name);
		AddFloat(Value);
	}
	ZENCORE_API void AddFloat(double Value);

	/** Write a bool field. */
	inline void AddBool(std::string_view Name, bool bValue)
	{
		SetName(Name);
		AddBool(bValue);
	}
	ZENCORE_API void AddBool(bool bValue);

	/** Write a field referencing a compact binary attachment by its hash. */
	inline void AddCompactBinaryAttachment(std::string_view Name, const IoHash& Value)
	{
		SetName(Name);
		AddCompactBinaryAttachment(Value);
	}
	ZENCORE_API void AddCompactBinaryAttachment(const IoHash& Value);

	/** Write a field referencing a binary attachment by its hash. */
	inline void AddBinaryAttachment(std::string_view Name, const IoHash& Value)
	{
		SetName(Name);
		AddBinaryAttachment(Value);
	}
	ZENCORE_API void AddBinaryAttachment(const IoHash& Value);

	/** Write a field referencing the attachment by its hash. */
	inline void AddAttachment(std::string_view Name, const CbAttachment& Attachment)
	{
		SetName(Name);
		AddAttachment(Attachment);
	}
	ZENCORE_API void AddAttachment(const CbAttachment& Attachment);

	/** Write a hash field. */
	inline void AddHash(std::string_view Name, const IoHash& Value)
	{
		SetName(Name);
		AddHash(Value);
	}
	ZENCORE_API void AddHash(const IoHash& Value);

	/** Write a UUID field. */
	inline void AddUuid(std::string_view Name, const Guid& Value)
	{
		SetName(Name);
		AddUuid(Value);
	}
	ZENCORE_API void AddUuid(const Guid& Value);

	/** Write an ObjectId field. */
	inline void AddObjectId(std::string_view Name, const Oid& Value)
	{
		SetName(Name);
		AddObjectId(Value);
	}
	ZENCORE_API void AddObjectId(const Oid& Value);

	/** Write a date/time field with the specified count of 100ns ticks since the epoch. */
	inline void AddDateTimeTicks(std::string_view Name, int64_t Ticks)
	{
		SetName(Name);
		AddDateTimeTicks(Ticks);
	}
	ZENCORE_API void AddDateTimeTicks(int64_t Ticks);

	/** Write a date/time field. */
	inline void AddDateTime(std::string_view Name, DateTime Value)
	{
		SetName(Name);
		AddDateTime(Value);
	}
	ZENCORE_API void AddDateTime(DateTime Value);

	/** Write a time span field with the specified count of 100ns ticks. */
	inline void AddTimeSpanTicks(std::string_view Name, int64_t Ticks)
	{
		SetName(Name);
		AddTimeSpanTicks(Ticks);
	}
	ZENCORE_API void AddTimeSpanTicks(int64_t Ticks);

	/** Write a time span field. */
	inline void AddTimeSpan(std::string_view Name, TimeSpan Value)
	{
		SetName(Name);
		AddTimeSpan(Value);
	}
	ZENCORE_API void AddTimeSpan(TimeSpan Value);

	/** Private flags that are public to work with ENUM_CLASS_FLAGS. */
	enum class StateFlags : uint8_t;

protected:
	/** Reserve the specified size up front until the format is optimized. */
	ZENCORE_API explicit CbWriter(int64_t InitialSize);

private:
	friend CbWriter& operator<<(CbWriter& Writer, std::string_view NameOrValue);

	/** Begin writing a field. May be called twice for named fields. */
	void BeginField();

	/** Finish writing a field by writing its type. */
	void EndField(CbFieldType Type);

	/** Set the field name if valid in this state, otherwise write add a string field. */
	ZENCORE_API void SetNameOrAddString(std::string_view NameOrValue);

	/** Returns a view of the name of the active field, if any, otherwise the empty view. */
	std::string_view GetActiveName() const;

	/** Remove field types after the first to make the sequence uniform. */
	void MakeFieldsUniform(int64_t FieldBeginOffset, int64_t FieldEndOffset);

	/** State of the object, array, or top-level field being written. */
	struct WriterState
	{
		StateFlags Flags{};
		/** The type of the fields in the sequence if uniform, otherwise None. */
		CbFieldType UniformType{};
		/** The offset of the start of the current field. */
		int64_t Offset{};
		/** The number of fields written in this state. */
		uint64_t Count{};
	};

private:
	// This is a prototype-quality format for the writer. Using an array of bytes is inefficient,
	// and will lead to many unnecessary copies and moves of the data to resize the array, insert
	// object and array sizes, and remove field types for uniform objects and uniform arrays. The
	// optimized format will be a list of power-of-two blocks and an optional first block that is
	// provided externally, such as on the stack. That format will store the offsets that require
	// object or array sizes to be inserted and field types to be removed, and will perform those
	// operations only when saving to a buffer.
	std::vector<uint8_t>	 Data;
	std::vector<WriterState> States;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * A writer for compact binary object, arrays, and fields that uses a fixed-size stack buffer.
 *
 * @see CbWriter
 */
template<uint32_t InlineBufferSize>
class FixedCbWriter : public CbWriter
{
public:
	inline FixedCbWriter() : CbWriter(InlineBufferSize) {}

	FixedCbWriter(const FixedCbWriter&) = delete;
	FixedCbWriter& operator=(const FixedCbWriter&) = delete;

private:
	// Reserve the inline buffer now even though we are unable to use it. This will avoid causing
	// new stack overflows when this functionality is properly implemented in the future.
	uint8_t Buffer[InlineBufferSize];
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class CbObjectWriter : public CbWriter
{
public:
	CbObjectWriter() { BeginObject(); }

	ZENCORE_API CbObject Save()
	{
		Finalize();
		return CbWriter::Save().AsObject();
	}

	ZENCORE_API void Save(BinaryWriter& Writer)
	{
		Finalize();
		return CbWriter::Save(Writer);
	}

	uint64_t GetSaveSize() = delete;

	void Finalize()
	{
		if (m_Finalized == false)
		{
			EndObject();
			m_Finalized = true;
		}
	}

	CbObjectWriter(const CbWriter&) = delete;
	CbObjectWriter& operator=(const CbWriter&) = delete;

private:
	bool m_Finalized = false;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Write the field name if valid in this state, otherwise write the string value. */
inline CbWriter&
operator<<(CbWriter& Writer, std::string_view NameOrValue)
{
	Writer.SetNameOrAddString(NameOrValue);
	return Writer;
}

/** Write the field name if valid in this state, otherwise write the string value. */
inline CbWriter&
operator<<(CbWriter& Writer, const char* NameOrValue)
{
	return Writer << std::string_view(NameOrValue);
}

inline CbWriter&
operator<<(CbWriter& Writer, const CbFieldView& Value)
{
	Writer.AddField(Value);
	return Writer;
}

inline CbWriter&
operator<<(CbWriter& Writer, const CbField& Value)
{
	Writer.AddField(Value);
	return Writer;
}

inline CbWriter&
operator<<(CbWriter& Writer, const CbObjectView& Value)
{
	Writer.AddObject(Value);
	return Writer;
}

inline CbWriter&
operator<<(CbWriter& Writer, const CbObject& Value)
{
	Writer.AddObject(Value);
	return Writer;
}

inline CbWriter&
operator<<(CbWriter& Writer, const CbArrayView& Value)
{
	Writer.AddArray(Value);
	return Writer;
}

inline CbWriter&
operator<<(CbWriter& Writer, const CbArray& Value)
{
	Writer.AddArray(Value);
	return Writer;
}

inline CbWriter&
operator<<(CbWriter& Writer, nullptr_t)
{
	Writer.AddNull();
	return Writer;
}

inline CbWriter&
operator<<(CbWriter& Writer, std::wstring_view Value)
{
	Writer.AddString(Value);
	return Writer;
}

inline CbWriter&
operator<<(CbWriter& Writer, const wchar_t* Value)
{
	Writer.AddString(Value);
	return Writer;
}

inline CbWriter&
operator<<(CbWriter& Writer, int32_t Value)
{
	Writer.AddInteger(Value);
	return Writer;
}

inline CbWriter&
operator<<(CbWriter& Writer, int64_t Value)
{
	Writer.AddInteger(Value);
	return Writer;
}

inline CbWriter&
operator<<(CbWriter& Writer, uint32_t Value)
{
	Writer.AddInteger(Value);
	return Writer;
}

inline CbWriter&
operator<<(CbWriter& Writer, uint64_t Value)
{
	Writer.AddInteger(Value);
	return Writer;
}

inline CbWriter&
operator<<(CbWriter& Writer, float Value)
{
	Writer.AddFloat(Value);
	return Writer;
}

inline CbWriter&
operator<<(CbWriter& Writer, double Value)
{
	Writer.AddFloat(Value);
	return Writer;
}

inline CbWriter&
operator<<(CbWriter& Writer, bool Value)
{
	Writer.AddBool(Value);
	return Writer;
}

inline CbWriter&
operator<<(CbWriter& Writer, const CbAttachment& Attachment)
{
	Writer.AddAttachment(Attachment);
	return Writer;
}

inline CbWriter&
operator<<(CbWriter& Writer, const IoHash& Value)
{
	Writer.AddHash(Value);
	return Writer;
}

inline CbWriter&
operator<<(CbWriter& Writer, const Guid& Value)
{
	Writer.AddUuid(Value);
	return Writer;
}

inline CbWriter&
operator<<(CbWriter& Writer, const Oid& Value)
{
	Writer.AddObjectId(Value);
	return Writer;
}

ZENCORE_API CbWriter& operator<<(CbWriter& Writer, DateTime Value);
ZENCORE_API CbWriter& operator<<(CbWriter& Writer, TimeSpan Value);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void usonbuilder_forcelink();  // internal

}  // namespace zen

