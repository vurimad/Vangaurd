/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

namespace red
{
	namespace memory
	{
		class Pool;
	}
	
	/// List of tags, where each tag is just a name (CName, indexed strings).
	/// Each tag can occur only once in tag list, TagList makes sure of that internally.
	class RED_CORE_API TagList
	{
		RTTI_DECLARE_TYPE(TagList);

		/// Just the list/array of tag names, no Match/HasTag etc functionality here.
		typedef red::DynArray< CName > TagNamesList;

	protected:
		TagNamesList m_tags;             ///< List of tags
																	// TODO: use sorted list?
																	// TODO: use some kind of optimization for 1 element tag list (no allocation)?
	public:
		/// Default constructor
		TagList();

		/// Custom pool
		TagList(const memory::Pool& pool);

		/// Copy
		TagList(const TagList& other);

		/// Copy to custom pool
		TagList(const TagList& other, const memory::Pool& pool);

		/// Move
		RED_INLINE TagList(TagList&& other);

		/// Create from plain list of names
		TagList(const TagNamesList& tags);

		/// Create from plain list of names, custom pool
		TagList(const TagNamesList& tags, const memory::Pool& pool);

		/// Create from single name
		TagList(const CName& tag);
		/// Create list with two tags, for convenience.
		TagList(const CName& tag1, const CName& tag2);
		/// Create list with three tags, for convenience.
		TagList(const CName& tag1, const CName& tag2, const CName& tag3);


		/// Destructor
		~TagList();


		/// Returns true if tag list is empty
		RED_INLINE Bool Empty() const;

		/// Does this tag group contain a given tag
		RED_INLINE Bool HasTag(const CName& name) const;

		/// Returns the number of tags on the list.
		RED_INLINE Uint32 Size() const;


		/// Clear tag list
		void Clear();

		/// Set new tags, old tags will be replaced.
		void SetTags(const TagNamesList& tags);

		/// Set new tags, old tags will be replaced.
		void SetTags(const TagList& tags);

		/// Merge tag list with another tag list
		void AddTags(const TagList& tags);

		/// Add one tag to tag list
		void AddTag(const CName& tag);

		/// Add two tags to tag list
		void AddTags(const CName& tag1, const CName& tag2);
		/// Add three tags to tag list
		void AddTags(const CName& tag1, const CName& tag2, const CName& tag3);

		/// Reserve space for n tags
		void Reserve(Uint32 size);

		/// Remove tags given in argument tag list from the tag list
		void RemoveTags(const TagList& tags);

		/// Remove one tag from tag list
		void RemoveTag(const CName& tag);

		/// Remove two tags from tag list
		void RemoveTags(const CName& tag1, const CName& tag2);
		/// Remove three tags from tag list
		void RemoveTags(const CName& tag1, const CName& tag2, const CName& tag3);


		/// Get list of tag names as an array.
		const TagNamesList& GetTagNames() const;

		/// Direct access to tag names. Can be safely called even on empty tag list, will just return CName::NONE().
		RED_INLINE CName operator[](Uint32 index) const;

		/// Copy tags
		TagList& operator=(const TagList& other);

		/// Move tags
		RED_INLINE TagList& operator=(TagList&& other);

		/// Compare
		Bool operator==(const TagList& other) const;

		/// Compare
		Bool operator!=(const TagList& other) const;


		/// Initialize from string
		Bool FromString(const red::String& tags);

		/// Convert to string
		red::String ToString(bool withSpaces = true) const;

		/// Serialize to/from file
		void Serialize(IFile& ar);


		/// Match any tag from reference list in the other tagList
		static Bool MatchAny(const TagList& tagList, const TagList& referenceList);
		static Bool MatchAny( red::ArraySpan<const CName> toCheck, red::ArraySpan<const CName> referenceList );

		/// Match all tags from reference list in the other tagList
		static Bool MatchAll(const TagList& tagList, const TagList& referenceList);
		static Bool MatchAll( red::ArraySpan<const CName> toCheck, red::ArraySpan<const CName> referenceList );


		/// Match tag in the tagList
		static Bool Match(const CName& reference, const TagList& tagList);


		/// Serialize
		friend void operator<<(IFile& file, TagList& tagList)
		{
			tagList.Serialize(file);
		}
	};

	template<Uint32 Length>
	struct err::CrashDataTypeAdapter<TagList, Length>
	{
		// Possibly bump as neeeded to not truncate tags
		static_assert(Length <= 1024, "Length is too long");
		using StorageType = char[DefaultCrashDataCharArrayLength<Length>() + 1];
		using SetType = TagList;

		static CrashDataCopyResult Copy(StorageType& storage, const SetType& value)
		{
			Bool truncated = false;
			Bool error = false;
			Uint32 spaceLeft = RED_ARRAY_COUNT_U32(storage);
			char* buf = &storage[0];
			const auto& tagNames = value.GetTagNames();
			for ( Uint32 i : tagNames.Indices() )
			{
				if (spaceLeft == 0)
				{
					truncated = true;
					break;
				}

				const CName tag = tagNames[i];

				if (!red::Strcpy(buf, tag.AsChar(), spaceLeft, spaceLeft - 1 ) )
				{
					error = true;
					break;
				}

				if (i > 0 && buf > &storage[0])
				{
					buf[-1] = ','; // overwrite previous '\0' with list separator
				}

				const Uint32 srcLen = (Uint32)red::Strlen(tag.AsChar());
				if (srcLen + 1 > spaceLeft)
				{
					truncated = true;
					break;
				}

				buf += srcLen + 1;
				spaceLeft -= (srcLen + 1 );
			}

			if (error)
			{
				return CrashDataCopyResult::Error;
			}
			
			if (truncated)
			{
				return CrashDataCopyResult::Truncated;
			}

			return CrashDataCopyResult::Success;
		}

		static Bool Print(char* buffer, Uint32 bufferLen, const StorageType& val)
		{
			return red::Strcpy(buffer, val, bufferLen, bufferLen - 1 );
		}
	};

} // red

#include "tagList.inl"
