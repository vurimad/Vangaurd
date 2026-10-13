/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace tools
{
	// This is a simple wrapper class that adds RTTI to res::ResourcePath
	// in order to allow Messages to be sent over interop
	class MessageResourcePath
	{
		RTTI_DECLARE_TYPE( MessageResourcePath );

	public:
		MessageResourcePath();
		MessageResourcePath( const res::ResourcePath& path );

		const res::ResourcePath& operator*() const { return m_resourcePath; }
		const res::ResourcePath* operator->() const { return &m_resourcePath; }

		MessageResourcePath& operator=( res::ResourcePath&& resourcePath );

		Bool operator==( const MessageResourcePath& other ) const { return m_resourcePath == other.m_resourcePath; }

		//! Serialize to/from file
		void Serialize( IFile& file );

	private:
		res::ResourcePath m_resourcePath;
	};
}

RTTI_CUSTOM_TYPE_NAME( tools::MessageResourcePath, "MessageResourcePath" );
