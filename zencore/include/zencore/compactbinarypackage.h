// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <zencore/zencore.h>

#include <zencore/compactbinary.h>
#include <zencore/iohash.h>

#include <functional>
#include <span>

namespace zen {

class CbWriter;
class BinaryReader;
class BinaryWriter;
class IoBuffer;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * An attachment is either binary or compact binary and is identified by its hash.
 *
 * A compact binary attachment is also a valid binary attachment and may be accessed as binary.
 *
 * Attachments are serialized as one or two compact binary fields with no name. A Binary field is
 * written first with its content. The content hash is omitted when the content size is zero, and
 * is otherwise written as a BinaryReference or CompactBinaryReference depending on the type.
 */
class CbAttachment
{
public:
	/** Construct a null attachment. */
	CbAttachment() = default;

	/** Construct a compact binary attachment. Value is cloned if not owned. */
	inline explicit CbAttachment(CbFieldIterator Value) : CbAttachment(std::move(Value), nullptr) {}

	/** Construct a compact binary attachment. Value is cloned if not owned. Hash must match Value. */
	inline explicit CbAttachment(CbFieldIterator Value, const IoHash& Hash) : CbAttachment(std::move(Value), &Hash) {}

	/** Construct a binary attachment. Value is cloned if not owned. */
	inline explicit CbAttachment(SharedBuffer Value) : CbAttachment(std::move(Value), nullptr) {}

	/** Construct a binary attachment. Value is cloned if not owned. Hash must match Value. */
	inline explicit CbAttachment(SharedBuffer Value, const IoHash& Hash) : CbAttachment(std::move(Value), &Hash) {}

	/** Reset this to a null attachment. */
	inline void Reset() { *this = CbAttachment(); }

	/** Whether the attachment has a value. */
	inline explicit operator bool() const { return !IsNull(); }

	/** Whether the attachment has a value. */
	inline bool IsNull() const { return !Buffer; }

	/** Access the attachment as binary. Defaults to a null buffer on error. */
	ZENCORE_API SharedBuffer AsBinaryView() const;

	/** Access the attachment as compact binary. Defaults to a field iterator with no value on error. */
	ZENCORE_API CbFieldIterator AsCompactBinary() const;

	/** Returns whether the attachment is binary or compact binary. */
	inline bool IsBinary() const { return !Buffer.IsNull(); }

	/** Returns whether the attachment is compact binary. */
	inline bool IsCompactBinary() const { return CompactBinary.HasValue(); }

	/** Returns the hash of the attachment value. */
	inline const IoHash& GetHash() const { return Hash; }

	/** Compares attachments by their hash. Any discrepancy in type must be handled externally. */
	inline bool operator==(const CbAttachment& Attachment) const { return Hash == Attachment.Hash; }
	inline bool operator!=(const CbAttachment& Attachment) const { return Hash != Attachment.Hash; }
	inline bool operator<(const CbAttachment& Attachment) const { return Hash < Attachment.Hash; }

	/**
	 * Load the attachment from compact binary as written by Save.
	 *
	 * The attachment references the input iterator if it is owned, and otherwise clones the value.
	 *
	 * The iterator is advanced as attachment fields are consumed from it.
	 */
	ZENCORE_API void Load(CbFieldIterator& Fields);

	/**
	 * Load the attachment from compact binary as written by Save.
	 */
	ZENCORE_API void Load(BinaryReader& Reader, BufferAllocator Allocator = UniqueBuffer::Alloc);

	/**
	 * Load the attachment from compact binary as written by Save.
	 */
	ZENCORE_API void Load(IoBuffer& Buffer, BufferAllocator Allocator = UniqueBuffer::Alloc);

	/** Save the attachment into the writer as a stream of compact binary fields. */
	ZENCORE_API void Save(CbWriter& Writer) const;

	/** Save the attachment into the writer as a stream of compact binary fields. */
	ZENCORE_API void Save(BinaryWriter& Writer) const;

private:
	ZENCORE_API CbAttachment(CbFieldIterator Value, const IoHash* Hash);
	ZENCORE_API CbAttachment(SharedBuffer Value, const IoHash* Hash);

	/** An owned buffer containing the binary or compact binary data. */
	SharedBuffer Buffer;
	/** A field iterator that is valid only for compact binary attachments. */
	CbFieldViewIterator CompactBinary;
	/** A hash of the attachment value. */
	IoHash Hash;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * A package is a compact binary object with attachments for its external references.
 *
 * A package is basically a Merkle tree with compact binary as its root and other non-leaf nodes,
 * and either binary or compact binary as its leaf nodes. A node references its child nodes using
 * BinaryHash or FieldHash fields in its compact binary representation.
 *
 * It is invalid for a package to include attachments that are not referenced by its object or by
 * one of its referenced compact binary attachments. When attachments are added explicitly, it is
 * the responsibility of the package creator to follow this requirement. Attachments that are not
 * referenced may not survive a round-trip through certain storage systems.
 *
 * It is valid for a package to exclude referenced attachments, but then it is the responsibility
 * of the package consumer to have a mechanism for resolving those references when necessary.
 *
 * A package is serialized as a sequence of compact binary fields with no name. The object may be
 * both preceded and followed by attachments. The object itself is written as an Object field and
 * followed by its hash in a CompactBinaryReference field when the object is non-empty. A package
 * ends with a Null field. The canonical order of components is the object and its hash, followed
 * by the attachments ordered by hash, followed by a Null field. It is valid for the a package to
 * have its components serialized in any order, provided there is at most one object and the null
 * field is written last.
 */
class CbPackage
{
public:
	/**
	 * A function that resolves a hash to a buffer containing the data matching that hash.
	 *
	 * The resolver may return a null buffer to skip resolving an attachment for the hash.
	 */
	using AttachmentResolver = std::function<SharedBuffer(const IoHash& Hash)>;

	/** Construct a null package. */
	CbPackage() = default;

	/**
	 * Construct a package from a root object without gathering attachments.
	 *
	 * @param InObject The root object, which will be cloned unless it is owned.
	 */
	inline explicit CbPackage(CbObject InObject) { SetObject(std::move(InObject)); }

	/**
	 * Construct a package from a root object and gather attachments using the resolver.
	 *
	 * @param InObject The root object, which will be cloned unless it is owned.
	 * @param InResolver A function that is invoked for every reference and binary reference field.
	 */
	inline explicit CbPackage(CbObject InObject, AttachmentResolver InResolver) { SetObject(std::move(InObject), InResolver); }

	/**
	 * Construct a package from a root object without gathering attachments.
	 *
	 * @param InObject The root object, which will be cloned unless it is owned.
	 * @param InObjectHash The hash of the object, which must match to avoid validation errors.
	 */
	inline explicit CbPackage(CbObject InObject, const IoHash& InObjectHash) { SetObject(std::move(InObject), InObjectHash); }

	/**
	 * Construct a package from a root object and gather attachments using the resolver.
	 *
	 * @param InObject The root object, which will be cloned unless it is owned.
	 * @param InObjectHash The hash of the object, which must match to avoid validation errors.
	 * @param InResolver A function that is invoked for every reference and binary reference field.
	 */
	inline explicit CbPackage(CbObject InObject, const IoHash& InObjectHash, AttachmentResolver InResolver)
	{
		SetObject(std::move(InObject), InObjectHash, InResolver);
	}

	/** Reset this to a null package. */
	inline void Reset() { *this = CbPackage(); }

	/** Whether the package has a non-empty object or attachments. */
	inline explicit operator bool() const { return !IsNull(); }

	/** Whether the package has an empty object and no attachments. */
	inline bool IsNull() const { return !Object.CreateIterator() && Attachments.size() == 0; }

	/** Returns the compact binary object for the package. */
	inline const CbObject& GetObject() const { return Object; }

	/** Returns the has of the compact binary object for the package. */
	inline const IoHash& GetObjectHash() const { return ObjectHash; }

	/**
	 * Set the root object without gathering attachments.
	 *
	 * @param InObject The root object, which will be cloned unless it is owned.
	 */
	inline void SetObject(CbObject InObject) { SetObject(std::move(InObject), nullptr, nullptr); }

	/**
	 * Set the root object and gather attachments using the resolver.
	 *
	 * @param InObject The root object, which will be cloned unless it is owned.
	 * @param InResolver A function that is invoked for every reference and binary reference field.
	 */
	inline void SetObject(CbObject InObject, AttachmentResolver InResolver) { SetObject(std::move(InObject), nullptr, &InResolver); }

	/**
	 * Set the root object without gathering attachments.
	 *
	 * @param InObject The root object, which will be cloned unless it is owned.
	 * @param InObjectHash The hash of the object, which must match to avoid validation errors.
	 */
	inline void SetObject(CbObject InObject, const IoHash& InObjectHash) { SetObject(std::move(InObject), &InObjectHash, nullptr); }

	/**
	 * Set the root object and gather attachments using the resolver.
	 *
	 * @param InObject The root object, which will be cloned unless it is owned.
	 * @param InObjectHash The hash of the object, which must match to avoid validation errors.
	 * @param InResolver A function that is invoked for every reference and binary reference field.
	 */
	inline void SetObject(CbObject InObject, const IoHash& InObjectHash, AttachmentResolver InResolver)
	{
		SetObject(std::move(InObject), &InObjectHash, &InResolver);
	}

	/** Returns the attachments in this package. */
	inline std::span<const CbAttachment> GetAttachments() const { return Attachments; }

	/**
	 * Find an attachment by its hash.
	 *
	 * @return The attachment, or null if the attachment is not found.
	 * @note The returned pointer is only valid until the attachments on this package are modified.
	 */
	ZENCORE_API const CbAttachment* FindAttachment(const IoHash& Hash) const;

	/** Find an attachment if it exists in the package. */
	inline const CbAttachment* FindAttachment(const CbAttachment& Attachment) const { return FindAttachment(Attachment.GetHash()); }

	/** Add the attachment to this package. */
	inline void AddAttachment(const CbAttachment& Attachment) { AddAttachment(Attachment, nullptr); }

	/** Add the attachment to this package, along with any references that can be resolved. */
	inline void AddAttachment(const CbAttachment& Attachment, AttachmentResolver Resolver) { AddAttachment(Attachment, &Resolver); }

	/**
	 * Remove an attachment by hash.
	 *
	 * @return Number of attachments removed, which will be either 0 or 1.
	 */
	ZENCORE_API int32_t RemoveAttachment(const IoHash& Hash);
	inline int32_t		RemoveAttachment(const CbAttachment& Attachment) { return RemoveAttachment(Attachment.GetHash()); }

	/** Compares packages by their object and attachment hashes. */
	ZENCORE_API bool Equals(const CbPackage& Package) const;
	inline bool		 operator==(const CbPackage& Package) const { return Equals(Package); }
	inline bool		 operator!=(const CbPackage& Package) const { return !Equals(Package); }

	/**
	 * Load the object and attachments from compact binary as written by Save.
	 *
	 * The object and attachments reference the input iterator, if it is owned, and otherwise clones
	 * the object and attachments individually to make owned copies.
	 *
	 * The iterator is advanced as object and attachment fields are consumed from it.
	 */
	ZENCORE_API void Load(CbFieldIterator& Fields);

	ZENCORE_API void Load(IoBuffer& Buffer, BufferAllocator Allocator = UniqueBuffer::Alloc);

	ZENCORE_API void Load(BinaryReader& Reader, BufferAllocator Allocator = UniqueBuffer::Alloc);

	/** Save the object and attachments into the writer as a stream of compact binary fields. */
	ZENCORE_API void Save(CbWriter& Writer) const;

	/** Save the object and attachments into the writer as a stream of compact binary fields. */
	ZENCORE_API void Save(BinaryWriter& Writer) const;

private:
	ZENCORE_API void SetObject(CbObject Object, const IoHash* Hash, AttachmentResolver* Resolver);
	ZENCORE_API void AddAttachment(const CbAttachment& Attachment, AttachmentResolver* Resolver);

	void GatherAttachments(const CbFieldViewIterator& Fields, AttachmentResolver Resolver);

	/** Attachments ordered by their hash. */
	std::vector<CbAttachment> Attachments;
	CbObject				  Object;
	IoHash					  ObjectHash;
};

void usonpackage_forcelink();  // internal

}  // namespace zen

