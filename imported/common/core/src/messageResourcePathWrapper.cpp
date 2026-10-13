/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "messageResourcePathWrapper.h"

RTTI_DEFINE_SIMPLE_TYPE_ALIAS( MessageResourcePath, tools::MessageResourcePath );

namespace tools
{
	MessageResourcePath::MessageResourcePath() = default;
	MessageResourcePath::MessageResourcePath( const res::ResourcePath& path )
		: m_resourcePath( path )
	{
	}

	MessageResourcePath& MessageResourcePath::operator=( res::ResourcePath&& resourcePath )
	{
		m_resourcePath = std::move( resourcePath );

		return *this;
	}

	void MessageResourcePath::Serialize( IFile& file )
	{
		if ( file.IsReader() )
		{
			String value;
			file << value;
			m_resourcePath = res::ResourcePath::Build( value );
		}
		else
		{
			String value = m_resourcePath.ToString();
			file << value;
		}
	}

	const Bool ToString( String& outTxt, const MessageResourcePath& val )
	{
		outTxt = val->ToString();
		return true;
	}

	Bool FromString( const String& str, MessageResourcePath& out )
	{
		out = res::ResourcePath::Build( str );

		return out->IsValid();
	}
}
