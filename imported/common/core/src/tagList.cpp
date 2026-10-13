/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "tagList.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE(TagList, red);
	RTTI_PROPERTY( m_tags ).editable().resizable().instanceEditable().replicated();
RTTI_END_TYPE();

namespace red
{
	using red::DynArray;
	using red::String;

	TagList::TagList()
		: m_tags( PoolCore() )
	{
	}

	TagList::TagList(const memory::Pool& pool)
		: m_tags(pool)
	{
	}

	TagList::TagList(const TagList& other)
		: m_tags( PoolCore() )
	{
		SetTags(other);
	}

	TagList::TagList(const TagList& other, const memory::Pool& pool)
		: m_tags(pool)
	{
		SetTags(other);
	}

	TagList::TagList(const TagNamesList& tags)
		: m_tags( PoolCore() )
	{
		SetTags(tags);
	}

	TagList::TagList(const TagNamesList& tags, const memory::Pool& pool)
		: m_tags(pool)
	{
		SetTags(tags);
	}

	TagList::TagList(const CName& tag)
		: m_tags( PoolCore() )
	{
		AddTag(tag);
	}

	TagList::TagList(const CName& tag1, const CName& tag2)
		: m_tags( PoolCore() )
	{
		AddTags(tag1, tag2);
	}

	TagList::TagList(const CName& tag1, const CName& tag2, const CName& tag3)
		: m_tags( PoolCore() )
	{
		AddTags(tag1, tag2, tag3);
	}

	TagList::~TagList()
	{
	}

	const TagList::TagNamesList& TagList::GetTagNames() const
	{
		return m_tags;
	}

	void TagList::Clear()
	{
		m_tags.Clear();
	}

	void TagList::SetTags(const TagNamesList& tags)
	{
		m_tags = tags;
		red::alg::RemoveDuplicates(m_tags);
	}

	void TagList::SetTags(const TagList& other)
	{
		SetTags(other.GetTagNames());
	}

	void TagList::AddTags(const TagList& tags)
	{
		red::alg::PushBackUnique(m_tags, tags.m_tags.Begin(), tags.m_tags.End());
	}

	void TagList::AddTag(const CName& tag)
	{
		red::alg::PushBackUnique(m_tags, tag);
	}

	void TagList::AddTags(const CName& tag1, const CName& tag2)
	{
		red::alg::PushBackUnique(m_tags, tag1);
		red::alg::PushBackUnique(m_tags, tag2);
	}

	void TagList::AddTags(const CName& tag1, const CName& tag2, const CName& tag3)
	{
		red::alg::PushBackUnique(m_tags, tag1);
		red::alg::PushBackUnique(m_tags, tag2);
		red::alg::PushBackUnique(m_tags, tag3);
	}

	void TagList::Reserve(Uint32 size)
	{
		m_tags.Reserve(size);
	}

	void TagList::RemoveTags(const TagList& tags)
	{
		if (!m_tags.Empty() && !tags.Empty())
		{
			// Remove all occurrences
			for (Uint32 i = 0; i < tags.m_tags.Size(); i++)
			{
				m_tags.Remove(tags.m_tags[i]);
			}
		}
	}

	void TagList::RemoveTag(const CName& tag)
	{
		m_tags.Remove(tag);
	}

	void TagList::RemoveTags(const CName& tag1, const CName& tag2)
	{
		m_tags.Remove(tag1);
		m_tags.Remove(tag2);
	}

	void TagList::RemoveTags(const CName& tag1, const CName& tag2, const CName& tag3)
	{
		m_tags.Remove(tag1);
		m_tags.Remove(tag2);
		m_tags.Remove(tag3);
	}

	TagList& TagList::operator=(const TagList& other)
	{
		if (this != &other)
		{
			m_tags = other.m_tags;
		}

		return *this;
	}

	Bool TagList::operator==(const TagList& other) const
	{
		return other.Size() == this->Size() && MatchAll(*this, other);
	}

	Bool TagList::operator!=(const TagList& other) const
	{
		return other.Size() != this->Size() || !MatchAll(*this, other);
	}

	void TagList::Serialize(IFile& ar)
	{
		ar << m_tags;
	}

	// todo:
	Bool TagList::FromString(const String& tagString)
	{
		// Special case - no tags 
		if (tagString.EqualsNC("<No tags>"))
		{
			Clear();
			return true;
		}

		// Gather tag list
		TagNamesList tagList{ red::PoolEngine() };
		if (!tagString.Empty())
		{
			String tagString2 = tagString;

			// Extract shit
			if (tagString2.BeginsWith("<") && tagString2.EndsWith(">"))
			{
				tagString2 = tagString2.MidString(1, tagString2.Length() - 2);
			}

			// Cut the crap
			tagString2.Trim();

			// Slice into tags
			DynArray< String > tagList2{ red::PoolEngine() };
			tagString2.Slice(tagList2, ";");

			// Trim and convert to CNames
			const Uint32 tagList2Count = tagList2.Size();
			for (Uint32 i = 0; i < tagList2Count; i++)
			{
				String& tagName = tagList2[i];
				tagName.Trim();

				CName tagListCName = RED_NAME(tagName);
				if (tagListCName)
				{
					tagList.PushBack(tagListCName);
				}
			}
		}

		// Create tag list
		SetTags(tagList);
		return true;
	}

	String TagList::ToString(bool withSpaces /*= true*/) const
	{
		// Special case - no tags in list
		if (Empty())
		{
			return "<No tags>";
		}

		// Assemble single readable string
		String outputString = withSpaces ? "< " : "<";

		for (Uint32 i = 0; i < m_tags.Size(); i++)
		{
			// If it's not first item append separator
			if (i > 0)
			{
				outputString += "; ";
			}

			// Append tag name
			outputString += m_tags[i].AsChar();
		}
		outputString += withSpaces ? " >" : ">";

		// Return formated string
		return outputString;
	}


	Bool TagList::MatchAny(const TagList& tagList, const TagList& referenceList)
	{
		return MatchAny( red::ArraySpan<const CName>( tagList.m_tags ), referenceList.m_tags );
	}

	Bool TagList::MatchAny( red::ArraySpan<const CName> toCheck, red::ArraySpan<const CName> referenceList )
	{
		// Nothing to match with, assume matched
		if( toCheck.Empty() )
		{
			return true;
		}

		// Empty target list, not matched - only empty tagList above can match
		if( referenceList.Empty() )
		{
			return false;
		}

		// Try to match any tag from reference list in the given tag list
		for( Uint32 i = 0; i < toCheck.Size(); i++ )
		{
			const CName tag = toCheck[i];
			auto found = std::find( referenceList.begin(), referenceList.end(), tag );
			if( found != referenceList.end() )
			{
				return true;
			}
		}

		// Not matched
		return false;
	}

	Bool TagList::MatchAll( const TagList& tagList, const TagList& referenceList )
	{
		return MatchAll( red::ArraySpan<const CName>( tagList.m_tags ), referenceList.m_tags );
	}

	Bool TagList::MatchAll( red::ArraySpan<const CName> toCheck, red::ArraySpan<const CName> referenceList )
	{
		if( toCheck.Empty() )
			return true;

		// Empty target list, not matched - only empty tagList above can match
		if( referenceList.Empty() )
			return false;

		// Try to match all tag from reference list in the given tag list
		for( Uint32 i = 0; i < toCheck.Size(); i++ )
		{
			const CName tag = toCheck[i];
			auto found = std::find( referenceList.begin(), referenceList.end(), tag );
			
			if( found == referenceList.end() )
				return false;
		}

		// All tags were found, matched
		return true;

	}

	Bool TagList::Match( const CName& reference, const TagList& tagList )
	{
		// Empty target list, not matched
		if (tagList.Empty())
		{
			return false;
		}

		// Try to match any tag from reference
		return tagList.HasTag(reference);
	}

} // red
