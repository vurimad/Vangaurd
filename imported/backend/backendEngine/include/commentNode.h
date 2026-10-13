/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once

namespace tools
{
	class CommentNode : public ISerializable
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_TYPE(CommentNode);

	public:
		const String& GetComment() const { return m_comment; }
		const String& GetTitle() const { return m_title; }
	private:
		String m_title;
		String m_comment;
	};
}