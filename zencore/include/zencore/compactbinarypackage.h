// Copyright Noah Games, Inc. All Rights Reserved.

#include "zencore/compactbinarypackage.h"
#include <zencore/compactbinarybuilder.h>
#include <zencore/compactbinaryvalidation.h>
#include <zencore/endian.h>
#include <zencore/stream.h>
#include <zencore/trace.h>

#include <doctest/doctest.h>

namespace zen {

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CbAttachment::CbAttachment(CbFieldIterator InValue, const IoHash* const InHash)
{
	if (InValue)
	{
		if (!InValue.IsOwned())
		{
			InValue = CbFieldIterator::CloneRange(InValue);
		}

		CompactBinary = CbFieldViewIterator(InValue);
		Buffer		  = std::move(InValue).GetOuterBuffer();
	}

	if (InHash)
	{
		Hash = *InHash;
		if (CompactBinary)
		{
			ZEN_ASSERT_SLOW(Hash == CompactBinary.GetRangeHash());
		}
		else
		{
#if 0
			zenfs_assertSlow(Hash.IsZero(), TEXT("A null or empty field range must use a hash of zero."));
#endif
		}
	}
	else if (CompactBinary)
	{
		Hash = CompactBinary.GetRangeHash();
	}
}

CbAttachment::CbAttachment(SharedBuffer InBuffer, const IoHash* const InHash) : Buffer(std::move(InBuffer))
{
	Buffer.MakeOwned();
	if (InHash)
	{
		Hash = *InHash;
		if (Buffer.GetSize())
		{
			ZEN_ASSERT_SLOW(Hash == IoHash::HashMemory(Buffer.GetData(), Buffer.GetSize()));
		}
		else
		{
			ZEN_ASSERT_SLOW(Hash == IoHash::Zero, TEXT("A null or empty buffer must use a hash of zero."));
		}
	}
	else if (Buffer.GetSize())
	{
		Hash = IoHash::HashMemory(Buffer.GetData(), Buffer.GetSize());
	}
	else
	{
		Buffer.Reset();
	}
}

SharedBuffer
CbAttachment::AsBinaryView() const
{
	if (!CompactBinary)
	{
		return Buffer;
	}

	MemoryView SerializedView;
	if (CompactBinary.TryGetSerializedRangeView(SerializedView))
	{
		return SerializedView == Buffer.GetView() ? Buffer : SharedBuffer::MakeView(SerializedView, Buffer);
	}

	return CbFieldIterator::CloneRange(CompactBinary).GetRangeBuffer();
}

CbFieldIterator
CbAttachment::AsCompactBinary() const
{
	return CompactBinary ? CbFieldIterator::MakeRangeView(CompactBinary, Buffer) : CbFieldIterator();
}

void
CbAttachment::Load(IoBuffer& InBuffer, BufferAllocator Allocator)
{
	MemoryInStream InStream(InBuffer.Data(), InBuffer.Size());
	BinaryReader   Reader(InStream);

	Load(Reader, Allocator);
}

void
CbAttachment::Load(CbFieldIterator& Fields)
{
	ZEN_ASSERT(Fields.IsBinary());	//, TEXT("Attachments must start with a binary field."));
	const MemoryView View = Fields.AsBinaryView();
	if (View.GetSize() > 0)
	{
		Buffer = SharedBuffer::MakeView(View, Fields.GetOuterBuffer());
		Buffer.MakeOwned();
		++Fields;
		Hash = Fields.AsAttachment();
		ZEN_ASSERT(!Fields.HasError());	 // TEXT("Attachments must be a non-empty binary value with a content hash."));
		if (Fields.IsCompactBinaryAttachment())
		{
			CompactBinary = CbFieldViewIterator::MakeRange(Buffer);
		}
		++Fields;
	}
	else
	{
		++Fields;
		Buffer.Reset();
		CompactBinary.Reset();
		Hash = IoHash::Zero;
	}
}

void
CbAttachment::Load(BinaryReader& Reader, BufferAllocator Allocator)
{
	CbField BufferField = LoadCompactBinary(Reader, Allocator);
	ZEN_ASSERT(BufferField.IsBinary(), "Attachments must start with a binary field");
	const MemoryView View = BufferField.AsBinaryView();
	if (View.GetSize() > 0)
	{
		Buffer = SharedBuffer::MakeView(View, BufferField.GetOuterBuffer());
		Buffer.MakeOwned();
		CompactBinary = CbFieldViewIterator();

		std::vector<uint8_t> HashBuffer;
		CbField				 HashField = LoadCompactBinary(Reader, [&HashBuffer](uint64_t Size) -> UniqueBuffer {
			 HashBuffer.resize(Size);
			 return UniqueBuffer::MakeView(HashBuffer.data(), Size);
		 });
		Hash						   = HashField.AsAttachment();
		ZEN_ASSERT(!HashField.HasError(), "Attachments must be a non-empty binary value with a content hash.");
		if (HashField.IsCompactBinaryAttachment())
		{
			CompactBinary = CbFieldViewIterator::MakeRange(Buffer);
		}
	}
	else
	{
		Buffer.Reset();
		CompactBinary.Reset();
		Hash = IoHash::Zero;
	}
}

void
CbAttachment::Save(CbWriter& Writer) const
{
	if (CompactBinary)
	{
		MemoryView SerializedView;
		if (CompactBinary.TryGetSerializedRangeView(SerializedView))
		{
			Writer.AddBinary(SerializedView);
		}
		else
		{
			Writer.AddBinary(AsBinaryView());
		}
		Writer.AddCompactBinaryAttachment(Hash);
	}
	else if (Buffer && Buffer.GetSize())
	{
		Writer.AddBinary(Buffer);
		Writer.AddBinaryAttachment(Hash);
	}
	else  // Null
	{
		Writer.AddBinary(MemoryView());
	}
}

void
CbAttachment::Save(BinaryWriter& Writer) const
{
	CbWriter TempWriter;
	Save(TempWriter);
	TempWriter.Save(Writer);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void
CbPackage::SetObject(CbObject InObject, const IoHash* InObjectHash, AttachmentResolver* InResolver)
{
	if (InObject.CreateIterator())
	{
		Object = InObject.IsOwned() ? std::move(InObject) : CbObject::Clone(InObject);
		if (InObjectHash)
		{
			ObjectHash = *InObjectHash;
			ZEN_ASSERT_SLOW(ObjectHash == Object.GetHash());
		}
		else
		{
			ObjectHash = Object.GetHash();
		}
		if (InResolver)
		{
			GatherAttachments(Object.CreateIterator(), *InResolver);
		}
	}
	else
	{
		Object.Reset();
		ObjectHash = IoHash::Zero;
	}
}

void
CbPackage::AddAttachment(const CbAttachment& Attachment, AttachmentResolver* Resolver)
{
	if (!Attachment.IsNull())
	{
		auto It = std::lower_bound(begin(Attachments), end(Attachments), Attachment);
		if (It != Attachments.end() && *It == Attachment)
		{
			CbAttachment& Existing = *It;
			if (Attachment.IsCompactBinary() && !Existing.IsCompactBinary())
			{
				Existing = CbAttachment(CbFieldIterator::MakeRange(Existing.AsBinaryView()));
			}
		}
		else
		{
			Attachments.insert(It, Attachment);
		}

		if (Attachment.IsCompactBinary() && Resolver)
		{
			GatherAttachments(Attachment.AsCompactBinary(), *Resolver);
		}
	}
}

int32_t
CbPackage::RemoveAttachment(const IoHash& Hash)
{
	return gsl::narrow_cast<int32_t>(
		std::erase_if(Attachments, [&Hash](const CbAttachment& Attachment) -> bool { return Attachment.GetHash() == Hash; }));
}

bool
CbPackage::Equals(const CbPackage& Package) const
{
	return ObjectHash == Package.ObjectHash && Attachments == Package.Attachments;
}

const CbAttachment*
CbPackage::FindAttachment(const IoHash& Hash) const
{
	auto It = std::find_if(begin(Attachments), end(Attachments), [&Hash](const CbAttachment& Attachment) -> bool {
		return Attachment.GetHash() == Hash;
	});

	if (It == end(Attachments))
		return nullptr;

	return &*It;
}

void
CbPackage::GatherAttachments(const CbFieldViewIterator& Fields, AttachmentResolver Resolver)
{
	Fields.IterateRangeAttachments([this, &Resolver](CbFieldView Field) {
		const IoHash& Hash = Field.AsAttachment();

		if (SharedBuffer Buffer = Resolver(Hash))
		{
			if (Field.IsCompactBinaryAttachment())
			{
				AddAttachment(CbAttachment(CbFieldIterator::MakeRange(std::move(Buffer)), Hash), &Resolver);
			}
			else
			{
				AddAttachment(CbAttachment(std::move(Buffer), Hash));
			}
		}
	});
}

void
CbPackage::Load(IoBuffer& InBuffer, BufferAllocator Allocator, AttachmentResolver* Mapper)
{
	MemoryInStream InStream(InBuffer.Data(), InBuffer.Size());
	BinaryReader   Reader(InStream);

	Load(Reader, Allocator, Mapper);
}

void
CbPackage::Load(CbFieldIterator& Fields)
{
	*this = CbPackage();
	while (Fields)
	{
		if (Fields.IsNull())
		{
			++Fields;
			break;
		}
		else if (Fields.IsBinary())
		{
			CbAttachment Attachment;
			Attachment.Load(Fields);
			AddAttachment(Attachment);
		}
		else
		{
			ZEN_ASSERT(Fields.IsObject(), TEXT("Expected Object, Binary, or Null field when loading a package."));
			Object = Fields.AsObject();
			Object.MakeOwned();
			++Fields;
			if (Object.CreateIterator())
			{
				ObjectHash = Fields.AsCompactBinaryAttachment();
				ZEN_ASSERT(!Fields.HasError(), TEXT("Object must be followed by a CompactBinaryReference with the object hash."));
				++Fields;
			}
			else
			{
				Object.Reset();
			}
		}
	}
}

void
CbPackage::Load(BinaryReader& Reader, BufferAllocator Allocator, AttachmentResolver* Mapper)
{
	uint8_t	   StackBuffer[64];
	const auto StackAllocator = [&Allocator, &StackBuffer](uint64_t Size) -> UniqueBuffer {
		if (Size <= sizeof(StackBuffer))
		{
			return UniqueBuffer::MakeView(StackBuffer, Size);
		}

		return Allocator(Size);
	};

	*this = CbPackage();

	for (;;)
	{
		CbField ValueField = LoadCompactBinary(Reader, StackAllocator);
		if (ValueField.IsNull())
		{
			break;
		}
		else if (ValueField.IsBinary())
		{
			const MemoryView View = ValueField.AsBinaryView();
			if (View.GetSize() > 0)
			{
				SharedBuffer Buffer = SharedBuffer::MakeView(View, ValueField.GetOuterBuffer());
				Buffer.MakeOwned();
				CbField		  HashField = LoadCompactBinary(Reader, StackAllocator);
				const IoHash& Hash		= HashField.AsAttachment();
				ZEN_ASSERT(!HashField.HasError(), "Attachments must be a non-empty binary value with a content hash.");
				if (HashField.IsCompactBinaryAttachment())
				{
					AddAttachment(CbAttachment(CbFieldIterator::MakeRange(std::move(Buffer)), Hash));
				}
				else
				{
					AddAttachment(CbAttachment(std::move(Buffer), Hash));
				}
			}
		}
		else if (ValueField.IsHash())
		{
			const IoHash Hash = ValueField.AsHash();

			ZEN_ASSERT(Mapper);

			AddAttachment(CbAttachment((*Mapper)(Hash), Hash));
		}
		else
		{
			ZEN_ASSERT(ValueField.IsObject(), "Expected Object, Binary, or Null field when loading a package");
			Object = ValueField.AsObject();
			Object.MakeOwned();
			if (Object.CreateViewIterator())
			{
				CbField HashField = LoadCompactBinary(Reader, StackAllocator);
				ObjectHash		  = HashField.AsCompactBinaryAttachment();
				ZEN_ASSERT(!HashField.HasError(), "Object must be followed by a CompactBinaryAttachment with the object hash.");
			}
			else
			{
				Object.Reset();
			}
		}
	}
}

void
CbPackage::Save(CbWriter& Writer) const
{
	if (Object.CreateIterator())
	{
		Writer.AddObject(Object);
		Writer.AddCompactBinaryAttachment(ObjectHash);
	}
	for (const CbAttachment& Attachment : Attachments)
	{
		Attachment.Save(Writer);
	}
	Writer.AddNull();
}

void
CbPackage::Save(BinaryWriter& StreamWriter) const
{
	CbWriter Writer;
	Save(Writer);
	Writer.Save(StreamWriter);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void
usonpackage_forcelink()
{
}

TEST_CASE("usonpackage")
{
	using namespace std::literals;

	const auto TestSaveLoadValidate = [&](const char* Test, const CbAttachment& Attachment) {
		ZEN_UNUSED(Test);

		CbWriter Writer;
		Attachment.Save(Writer);
		CbFieldIterator Fields = Writer.Save();

		MemoryOutStream WriteStream;
		BinaryWriter	StreamWriter{WriteStream};
		Attachment.Save(StreamWriter);

		CHECK(MakeMemoryView(WriteStream).EqualBytes(Fields.GetRangeBuffer().GetView()));
		CHECK(ValidateCompactBinaryRange(MakeMemoryView(WriteStream), CbValidateMode::All) == CbValidateError::None);
		CHECK(ValidateCompactBinaryAttachment(MakeMemoryView(WriteStream), CbValidateMode::All) == CbValidateError::None);

		CbAttachment FromFields;
		FromFields.Load(Fields);
		CHECK(!bool(Fields));
		CHECK(FromFields == Attachment);

		CbAttachment   FromArchive;
		MemoryInStream InStream(MakeMemoryView(WriteStream));
		BinaryReader   Reader(InStream);
		FromArchive.Load(Reader);
		CHECK(Reader.CurrentOffset() == InStream.Size());
		CHECK(FromArchive == Attachment);
	};

	SUBCASE("Empty Attachment")
	{
		CbAttachment Attachment;
		CHECK(Attachment.IsNull());
		CHECK_FALSE(bool(Attachment));
		CHECK_FALSE(bool(Attachment.AsBinaryView()));
		CHECK_FALSE(Attachment.AsCompactBinary().HasValue());
		CHECK_FALSE(Attachment.IsBinary());
		CHECK_FALSE(Attachment.IsCompactBinary());
		CHECK(Attachment.GetHash() == IoHash::Zero);
		TestSaveLoadValidate("Null", Attachment);
	}

	SUBCASE("Binary Attachment")
	{
		const SharedBuffer Buffer = SharedBuffer::Clone(MakeMemoryView<uint8_t>({0, 1, 2, 3}));
		CbAttachment	   Attachment(Buffer);
		CHECK_FALSE(Attachment.IsNull());
		CHECK(bool(Attachment));
		CHECK(Attachment.AsBinaryView() == Buffer);
		CHECK_FALSE(Attachment.AsCompactBinary().HasValue());
		CHECK(Attachment.IsBinary());
		CHECK_FALSE(Attachment.IsCompactBinary());
		CHECK(Attachment.GetHash() == IoHash::HashMemory(Buffer));
		TestSaveLoadValidate("Binary", Attachment);
	}

	SUBCASE("Compact Binary Attachment")
	{
		CbWriter Writer;
		Writer << "Name"sv << 42;
		CbFieldIterator Fields = Writer.Save();
		CbAttachment	Attachment(Fields);

		CHECK_FALSE(Attachment.IsNull());
		CHECK(bool(Attachment));
		CHECK(Attachment.AsBinaryView() == Fields.GetRangeBuffer());
		CHECK(Attachment.AsCompactBinary() == Fields);
		CHECK(Attachment.IsBinary());
		CHECK(Attachment.IsCompactBinary());
		CHECK(Attachment.GetHash() == Fields.GetRangeHash());
		TestSaveLoadValidate("CompactBinary", Attachment);
	}

	SUBCASE("Binary View")
	{
		const uint8_t Value[]{0, 1, 2, 3};
		SharedBuffer  Buffer = SharedBuffer::MakeView(MakeMemoryView(Value));
		CbAttachment  Attachment(Buffer);
		CHECK_FALSE(Attachment.IsNull());
		CHECK(bool(Attachment));
		CHECK(Attachment.AsBinaryView().GetView().EqualBytes(Buffer.GetView()));
		CHECK_FALSE(Attachment.AsCompactBinary().HasValue());
		CHECK(Attachment.IsBinary());
		CHECK_FALSE(Attachment.IsCompactBinary());
		CHECK(Attachment.GetHash() == IoHash::HashMemory(Buffer));
	}

	SUBCASE("Compact Binary View")
	{
		CbWriter Writer;
		Writer << "Name"sv << 42;
		CbFieldIterator Fields	   = Writer.Save();
		CbFieldIterator FieldsView = CbFieldIterator::MakeRangeView(CbFieldViewIterator(Fields));
		CbAttachment	Attachment(FieldsView);

		CHECK_FALSE(Attachment.IsNull());
		CHECK(bool(Attachment));

		CHECK(Attachment.AsBinaryView() != FieldsView.GetRangeBuffer());

		CHECK(Attachment.AsCompactBinary().GetRangeView().EqualBytes(Fields.GetRangeView()));
		CHECK(Attachment.IsBinary());
		CHECK(Attachment.GetHash() == Fields.GetRangeHash());
	}

	SUBCASE("Binary Load from View")
	{
		const uint8_t	   Value[]{0, 1, 2, 3};
		const SharedBuffer Buffer = SharedBuffer::MakeView(MakeMemoryView(Value));
		CbAttachment	   Attachment(Buffer);

		CbWriter Writer;
		Attachment.Save(Writer);
		CbFieldIterator Fields	   = Writer.Save();
		CbFieldIterator FieldsView = CbFieldIterator::MakeRangeView(CbFieldViewIterator(Fields));
		Attachment.Load(FieldsView);

		CHECK_FALSE(Attachment.IsNull());
		CHECK(bool(Attachment));
		CHECK_FALSE(FieldsView.GetRangeBuffer().GetView().Contains(Attachment.AsBinaryView().GetView()));
		CHECK(Attachment.AsBinaryView().GetView().EqualBytes(Buffer.GetView()));
		CHECK_FALSE(Attachment.AsCompactBinary().HasValue());
		CHECK(Attachment.IsBinary());
		CHECK_FALSE(Attachment.IsCompactBinary());
		CHECK(Attachment.GetHash() == IoHash::HashMemory(MakeMemoryView(Value)));
	}

	SUBCASE("Compact Binary Load from View")
	{
		CbWriter ValueWriter;
		ValueWriter << "Name"sv << 42;
		const CbFieldIterator Value = ValueWriter.Save();

		CHECK(ValidateCompactBinaryRange(Value.GetRangeView(), CbValidateMode::All) == CbValidateError::None);
		CbAttachment Attachment(Value);

		CbWriter Writer;
		Attachment.Save(Writer);
		CbFieldIterator Fields	   = Writer.Save();
		CbFieldIterator FieldsView = CbFieldIterator::MakeRangeView(CbFieldViewIterator(Fields));

		Attachment.Load(FieldsView);

		CHECK_FALSE(Attachment.IsNull());
		CHECK(bool(Attachment));

		CHECK(Attachment.AsBinaryView().GetView().EqualBytes(Value.GetRangeView()));
		CHECK_FALSE(FieldsView.GetRangeBuffer().GetView().Contains(Attachment.AsCompactBinary().GetRangeBuffer().GetView()));
		CHECK(Attachment.IsBinary());
		CHECK(Attachment.IsCompactBinary());

		CHECK(Attachment.GetHash() == Value.GetRangeHash());
	}

	SUBCASE("Compact Binary Uniform Sub-View")
	{
		const SharedBuffer		  Buffer	  = SharedBuffer::Clone(MakeMemoryView<uint8_t>({0, 1, 2, 3}));
		const CbFieldViewIterator FieldViews  = CbFieldViewIterator::MakeRange(Buffer.GetView().RightChop(2), CbFieldType::IntegerPositive);
		const CbFieldIterator	  SavedFields = CbFieldIterator::CloneRange(FieldViews);
		CbFieldIterator			  Fields	  = CbFieldIterator::MakeRangeView(FieldViews, Buffer);
		CbAttachment			  Attachment(Fields);
		const SharedBuffer		  Binary = Attachment.AsBinaryView();
		CHECK(Attachment.AsCompactBinary() == Fields);
		CHECK(Binary.GetSize() == SavedFields.GetRangeSize());
		CHECK(Binary.GetView().EqualBytes(SavedFields.GetRangeView()));
		CHECK(Attachment.GetHash() == SavedFields.GetRangeHash());
		TestSaveLoadValidate("CompactBinaryUniformSubView", Attachment);
	}

	SUBCASE("Binary Null")
	{
		const CbAttachment Attachment(SharedBuffer{});

		CHECK(Attachment.IsNull());
		CHECK_FALSE(Attachment.IsBinary());
		CHECK_FALSE(Attachment.IsCompactBinary());
		CHECK(Attachment.GetHash() == IoHash::Zero);
	}

	SUBCASE("Binary Empty")
	{
		const CbAttachment Attachment(SharedBuffer(UniqueBuffer::Alloc(0)));

		CHECK(Attachment.IsNull());
		CHECK_FALSE(Attachment.IsBinary());
		CHECK_FALSE(Attachment.IsCompactBinary());
		CHECK(Attachment.GetHash() == IoHash::Zero);
	}

	SUBCASE("Compact Binary Empty")
	{
		const CbAttachment Attachment(CbFieldIterator{});

		CHECK(Attachment.IsNull());
		CHECK_FALSE(Attachment.IsBinary());
		CHECK_FALSE(Attachment.IsCompactBinary());
		CHECK(Attachment.GetHash() == IoHash::Zero);
	}
}

TEST_CASE("usonpackage.serialization")
{
	using namespace std::literals;

	const auto TestSaveLoadValidate = [&](const char* Test, const CbPackage& Package) {
		ZEN_UNUSED(Test);

		CbWriter Writer;
		Package.Save(Writer);
		CbFieldIterator Fields = Writer.Save();

		MemoryOutStream MemStream;
		BinaryWriter	WriteAr(MemStream);
		Package.Save(WriteAr);

		CHECK(MakeMemoryView(MemStream).EqualBytes(Fields.GetRangeBuffer().GetView()));
		CHECK(ValidateCompactBinaryRange(MakeMemoryView(MemStream), CbValidateMode::All) == CbValidateError::None);
		CHECK(ValidateCompactBinaryPackage(MakeMemoryView(MemStream), CbValidateMode::All) == CbValidateError::None);

		CbPackage FromFields;
		FromFields.Load(Fields);
		CHECK_FALSE(bool(Fields));
		CHECK(FromFields == Package);

		CbPackage	   FromArchive;
		MemoryInStream ReadMemStream(MakeMemoryView(MemStream));
		BinaryReader   ReadAr(ReadMemStream);
		FromArchive.Load(ReadAr);
		CHECK(ReadAr.CurrentOffset() == ReadMemStream.Size());
		CHECK(FromArchive == Package);
	};

	SUBCASE("Empty")
	{
		CbPackage Package;
		CHECK(Package.IsNull());
		CHECK_FALSE(bool(Package));
		CHECK(Package.GetAttachments().size() == 0);
		TestSaveLoadValidate("Empty", Package);
	}

	SUBCASE("Object Only")
	{
		CbWriter Writer;
		Writer.BeginObject();
		Writer << "Field" << 42;
		Writer.EndObject();

		const CbObject Object = Writer.Save().AsObject();
		CbPackage	   Package(Object);
		CHECK_FALSE(Package.IsNull());
		CHECK(bool(Package));
		CHECK(Package.GetAttachments().size() == 0);
		CHECK(Package.GetObject().GetOuterBuffer() == Object.GetOuterBuffer());
		CHECK(Package.GetObject()["Field"].AsInt32() == 42);
		CHECK(Package.GetObjectHash() == Package.GetObject().GetHash());
		TestSaveLoadValidate("Object", Package);
	}

	// Object View Only
	{
		CbWriter Writer;
		Writer.BeginObject();
		Writer << "Field" << 42;
		Writer.EndObject();

		const CbObject Object = Writer.Save().AsObject();
		CbPackage	   Package(CbObject::MakeView(Object));
		CHECK_FALSE(Package.IsNull());
		CHECK(bool(Package));
		CHECK(Package.GetAttachments().size() == 0);
		CHECK(Package.GetObject().GetOuterBuffer() != Object.GetOuterBuffer());
		CHECK(Package.GetObject()["Field"].AsInt32() == 42);
		CHECK(Package.GetObjectHash() == Package.GetObject().GetHash());
		TestSaveLoadValidate("Object", Package);
	}

	// Attachment Only
	{
		CbObject Object;
		{
			CbWriter Writer;
			Writer.BeginObject();
			Writer << "Field" << 42;
			Writer.EndObject();
			Object = Writer.Save().AsObject();
		}
		CbField Field = CbField::Clone(Object["Field"]);

		CbPackage Package;
		Package.AddAttachment(CbAttachment(CbFieldIterator::MakeSingle(Object.AsField())));
		Package.AddAttachment(CbAttachment(Field.GetBuffer()));

		CHECK_FALSE(Package.IsNull());
		CHECK(bool(Package));
		CHECK(Package.GetAttachments().size() == 2);
		CHECK(Package.GetObject().Equals(CbObject()));
		CHECK(Package.GetObjectHash() == IoHash());
		TestSaveLoadValidate("Attachments", Package);

		const CbAttachment* const ObjectAttachment = Package.FindAttachment(Object.GetHash());
		REQUIRE(ObjectAttachment);

		const CbAttachment* const FieldAttachment = Package.FindAttachment(Field.GetHash());
		REQUIRE(FieldAttachment);

		CHECK(ObjectAttachment->AsCompactBinary().AsObject().Equals(Object));
		CHECK(FieldAttachment->AsBinaryView() == Field.GetBuffer());

		Package.AddAttachment(CbAttachment(SharedBuffer::Clone(Object.GetView())));
		Package.AddAttachment(CbAttachment(CbFieldIterator::CloneRange(CbFieldViewIterator::MakeSingle(Field))));

		CHECK(Package.GetAttachments().size() == 2);
		CHECK(Package.FindAttachment(Object.GetHash()) == ObjectAttachment);
		CHECK(Package.FindAttachment(Field.GetHash()) == FieldAttachment);

		CHECK(ObjectAttachment->AsCompactBinary().AsObject().Equals(Object));
		CHECK(ObjectAttachment->AsBinaryView() == Object.GetBuffer());
		CHECK(FieldAttachment->AsCompactBinary().Equals(Field));
		CHECK(FieldAttachment->AsBinaryView() == Field.GetBuffer());

		CHECK(std::is_sorted(begin(Package.GetAttachments()), end(Package.GetAttachments())));
	}

	// Shared Values
	const uint8_t Level4Values[]{0, 1, 2, 3};
	SharedBuffer  Level4	 = SharedBuffer::MakeView(MakeMemoryView(Level4Values));
	const IoHash  Level4Hash = IoHash::HashMemory(Level4);

	CbField Level3;
	{
		CbWriter Writer;
		Writer.SetName("Level4").AddBinaryAttachment(Level4Hash);
		Level3 = Writer.Save();
	}
	const IoHash Level3Hash = Level3.GetHash();

	CbArray Level2;
	{
		CbWriter Writer;
		Writer.SetName("Level3");
		Writer.BeginArray();
		Writer.AddCompactBinaryAttachment(Level3Hash);
		Writer.EndArray();
		Level2 = Writer.Save().AsArray();
	}
	const IoHash Level2Hash = Level2.AsFieldView().GetHash();

	CbObject Level1;
	{
		CbWriter Writer;
		Writer.BeginObject();
		Writer.SetName("Level2").AddCompactBinaryAttachment(Level2Hash);
		Writer.EndObject();
		Level1 = Writer.Save().AsObject();
	}
	const IoHash Level1Hash = Level1.AsFieldView().GetHash();

	const auto Resolver = [&Level2, &Level2Hash, &Level3, &Level3Hash, &Level4, &Level4Hash](const IoHash& Hash) -> SharedBuffer {
		return Hash == Level2Hash	? Level2.GetBuffer()
			   : Hash == Level3Hash ? Level3.GetBuffer()
			   : Hash == Level4Hash ? Level4
									: SharedBuffer();
	};

	// Object + Attachments
	{
		CbPackage Package;
		Package.SetObject(Level1, Level1Hash, Resolver);

		CHECK_FALSE(Package.IsNull());
		CHECK(bool(Package));
		CHECK(Package.GetAttachments().size() == 3);
		CHECK(Package.GetObject().GetBuffer() == Level1.GetBuffer());
		CHECK(Package.GetObjectHash() == Level1Hash);
		TestSaveLoadValidate("Object+Attachments", Package);

		const CbAttachment* const Level2Attachment = Package.FindAttachment(Level2Hash);
		const CbAttachment* const Level3Attachment = Package.FindAttachment(Level3Hash);
		const CbAttachment* const Level4Attachment = Package.FindAttachment(Level4Hash);
		CHECK((Level2Attachment && Level2Attachment->AsCompactBinary().AsArray().Equals(Level2)));
		CHECK((Level3Attachment && Level3Attachment->AsCompactBinary().Equals(Level3)));
		CHECK((Level4Attachment && Level4Attachment->AsBinaryView() != Level4 &&
			   Level4Attachment->AsBinaryView().GetView().EqualBytes(Level4.GetView())));

		CHECK(std::is_sorted(begin(Package.GetAttachments()), end(Package.GetAttachments())));

		const CbPackage PackageCopy = Package;
		CHECK(PackageCopy == Package);

		CHECK(Package.RemoveAttachment(Level1Hash) == 0);
		CHECK(Package.RemoveAttachment(Level2Hash) == 1);
		CHECK(Package.RemoveAttachment(Level3Hash) == 1);
		CHECK(Package.RemoveAttachment(Level4Hash) == 1);
		CHECK(Package.RemoveAttachment(Level4Hash) == 0);
		CHECK(Package.GetAttachments().size() == 0);

		CHECK(PackageCopy != Package);
		Package = PackageCopy;
		CHECK(PackageCopy == Package);
		Package.SetObject(CbObject());
		CHECK(PackageCopy != Package);
		CHECK(Package.GetObjectHash() == IoHash());
	}

	// Out of Order
	{
		CbWriter Writer;
		Writer.AddBinary(Level2.GetBuffer());
		Writer.AddCompactBinaryAttachment(Level2Hash);
		Writer.AddBinary(Level4);
		Writer.AddBinaryAttachment(Level4Hash);
		Writer.AddObject(Level1);
		Writer.AddCompactBinaryAttachment(Level1Hash);
		Writer.AddBinary(Level3.GetBuffer());
		Writer.AddCompactBinaryAttachment(Level3Hash);
		Writer.AddNull();

		CbFieldIterator Fields = Writer.Save();
		CbPackage		FromFields;
		FromFields.Load(Fields);

		const CbAttachment* const Level2Attachment = FromFields.FindAttachment(Level2Hash);
		REQUIRE(Level2Attachment);
		const CbAttachment* const Level3Attachment = FromFields.FindAttachment(Level3Hash);
		REQUIRE(Level3Attachment);
		const CbAttachment* const Level4Attachment = FromFields.FindAttachment(Level4Hash);
		REQUIRE(Level4Attachment);

		CHECK(FromFields.GetObject().Equals(Level1));
		CHECK(FromFields.GetObject().GetOuterBuffer() == Fields.GetOuterBuffer());
		CHECK(FromFields.GetObjectHash() == Level1Hash);

		const MemoryView FieldsOuterBufferView = Fields.GetOuterBuffer().GetView();

		CHECK(Level2Attachment->AsCompactBinary().AsArray().Equals(Level2));
		CHECK(FieldsOuterBufferView.Contains(Level2Attachment->AsBinaryView().GetView()));
		CHECK(Level2Attachment->GetHash() == Level2Hash);

		CHECK(Level3Attachment->AsCompactBinary().Equals(Level3));
		CHECK(FieldsOuterBufferView.Contains(Level3Attachment->AsBinaryView().GetView()));
		CHECK(Level3Attachment->GetHash() == Level3Hash);

		CHECK(Level4Attachment->AsBinaryView().GetView().EqualBytes(Level4.GetView()));
		CHECK(FieldsOuterBufferView.Contains(Level4Attachment->AsBinaryView().GetView()));
		CHECK(Level4Attachment->GetHash() == Level4Hash);

		MemoryOutStream WriteStream;
		BinaryWriter	WriteAr(WriteStream);
		Writer.Save(WriteAr);
		CbPackage	   FromArchive;
		MemoryInStream ReadStream(MakeMemoryView(WriteStream));
		BinaryReader   ReadAr(ReadStream);
		FromArchive.Load(ReadAr);

		Writer.Reset();
		FromArchive.Save(Writer);
		CbFieldIterator Saved = Writer.Save();
		CHECK(Saved.AsObject().Equals(Level1));
		++Saved;
		CHECK(Saved.AsCompactBinaryAttachment() == Level1Hash);
		++Saved;
		CHECK(Saved.AsBinaryView().EqualBytes(Level2.GetView()));
		++Saved;
		CHECK(Saved.AsCompactBinaryAttachment() == Level2Hash);
		++Saved;
		CHECK(Saved.AsBinaryView().EqualBytes(Level3.GetView()));
		++Saved;
		CHECK(Saved.AsCompactBinaryAttachment() == Level3Hash);
		++Saved;
		CHECK(Saved.AsBinaryView().EqualBytes(Level4.GetView()));
		++Saved;
		CHECK(Saved.AsBinaryAttachment() == Level4Hash);
		++Saved;
		CHECK(Saved.IsNull());
		++Saved;
		CHECK(!Saved);
	}

	// Null Attachment
	{
		const CbAttachment NullAttachment;
		CbPackage		   Package;
		Package.AddAttachment(NullAttachment);
		CHECK(Package.IsNull());
		CHECK_FALSE(bool(Package));
		CHECK(Package.GetAttachments().size() == 0);
		CHECK_FALSE(Package.FindAttachment(NullAttachment));
	}

	// Resolve After Merge
	{
		bool	  bResolved = false;
		CbPackage Package;
		Package.AddAttachment(CbAttachment(Level3.GetBuffer()));
		Package.AddAttachment(CbAttachment(CbFieldIterator::MakeSingle(Level3)), [&bResolved](const IoHash& Hash) -> SharedBuffer {
			ZEN_UNUSED(Hash);
			bResolved = true;
			return SharedBuffer();
		});
		CHECK(bResolved);
	}
}

}  // namespace zen