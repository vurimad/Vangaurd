/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

namespace red
{
	RED_INLINE TagList::TagList(TagList&& other)
		: m_tags(std::move(other.m_tags))
	{
	}

	RED_INLINE Bool TagList::Empty() const
	{
		return m_tags.Empty();
	}

	RED_INLINE Bool TagList::HasTag(const CName& name) const
	{
		return m_tags.Exist(name);
	}

	RED_INLINE Uint32 TagList::Size() const
	{
		return m_tags.Size();
	}

	RED_INLINE TagList& TagList::operator=(TagList&& other)
	{
		if (this != &other)
		{
			m_tags = std::move(other.m_tags);
		}

		return *this;
	}

	RED_INLINE CName TagList::operator[](Uint32 index) const
	{
		Uint32 size = Size();
		if (size > 0 && size > index)
		{
			return m_tags[index];
		}

		return CName::NONE();
	}
}
