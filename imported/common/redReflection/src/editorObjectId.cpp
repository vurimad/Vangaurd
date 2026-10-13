/**
* Copyright (c) 2015-2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "editorObjectId.h"
#include "types.h"

#include "../../redContainers/include/string/stringBuilder.h"
#include "../../redContainers/include/string/stringUtils.h"
#include "../../redContainers/include/fundamentalStringParser.h"
#include "rttiSimpleType.h"
#include "stringSerialization.h"
#include "rttiClassBuilder.h"

#include "../../redSystem/include/crc.h"
#include "../../redFileSystem/include/file.h"

//---------------------------------------------------------

RTTI_DEFINE_SIMPLE_TYPE_ALIAS( EditorObjectID, tools::EditorObjectID );

//---------------------------------------------------------

namespace tools
{
	const Bool ToString( String& outTxt, const EditorObjectID& val )
	{
		outTxt = val.ToString();
		return true;
	}

	const Bool FromString( const String& txt, EditorObjectID& outVal )
	{
		return EditorObjectID::FromString(txt, outVal);
	}

	//---

	EditorObjectID::EditorObjectID()
		: m_internalId(0)
	{}

	const CName EditorObjectID::GetKind() const
	{
		return m_kind;
	}

	const res::ResourcePath& EditorObjectID::GetResourcePath() const
	{
		return m_resourcePath;
	}

	const EditorObjectInternalID EditorObjectID::GetInternalID() const
	{
		return m_internalId;
	}

	const CName EditorObjectID::GetName() const
	{
		return m_name;
	}

	const Bool EditorObjectID::IsValid() const
	{
		return !m_kind.Empty();
	}

	const Uint32 EditorObjectID::CalcHash() const
	{
		return red::GetHash( m_resourcePath.GetHash() ^ m_internalId ^ m_kind.GetHash() );
	}

	const Uint64 EditorObjectID::CalcHash64() const
	{
		red::THash64 hash = RED_FNV_OFFSET_BASIS64;
			
		hash = red::CombineHashes64( hash, m_resourcePath.GetHash() );
		hash = red::CombineHashes64( hash, m_internalId );
		hash = red::CombineHashes64( hash, m_kind.GetHash() );

		return hash;
	}

	bool EditorObjectID::operator==( const EditorObjectID& other ) const
	{
		return ( m_resourcePath == other.m_resourcePath ) && ( m_internalId == other.m_internalId ) && ( m_kind == other.m_kind );
	}

	bool EditorObjectID::operator!=( const EditorObjectID& other ) const
	{
		return !( operator==( other ) );
	}

	bool EditorObjectID::operator<( const EditorObjectID& other ) const
	{
		if ( m_kind != other.m_kind )
			return ( m_kind < other.m_kind );

		if ( m_resourcePath != other.m_resourcePath )
			return ( m_resourcePath < other.m_resourcePath );

		return ( m_internalId < other.m_internalId );
	}

	EditorObjectID EditorObjectID::Build( const res::ResourcePath& resourcePath, const EditorObjectInternalID internalId, const CName kind, const CName name )
	{
		RED_FATAL_ASSERT( kind, "EditorObjectID must have a valid 'kind'" );

		EditorObjectID ret;
		ret.m_resourcePath = resourcePath;
		ret.m_internalId = internalId;
		ret.m_name = name;
		ret.m_kind = kind;
		return ret;
	}

	EditorObjectID EditorObjectID::Build( const EditorObjectInternalID internalId, const CName kind, const CName name )
	{
		RED_FATAL_ASSERT( kind, "EditorObjectID must have a valid 'kind'" );

		EditorObjectID ret;
		ret.m_resourcePath = res::ResourcePath();
		ret.m_internalId = internalId;
		ret.m_name = name;
		ret.m_kind = kind;
		return ret;
	}

	void EditorObjectID::Serialize( IFile& file )
	{
		// EditorObjectID may not be persistent between editor sessions, don't save it to a file
		RED_FATAL_ASSERT( !file.IsFileBased(), "EditorObjectID can not be serialized to a file." );

		if ( file.IsWriter() )
		{
			String val = m_resourcePath.ToString();
			file << val;
			file << m_internalId;
			file << m_name;
			file << m_kind;
		}
		else if ( file.IsReader() )
		{
			String val;
			file << val;
			m_resourcePath = res::ResourcePath::Build( val );
			file << m_internalId;
			file << m_name;
			file << m_kind;
		}
	}

	String EditorObjectID::ToString() const
	{
		if (!IsValid())
		{
			return "null";
		}
		else
		{
			red::StringBuilder<String> b;

			// print the stuff JSON style
			b.Append( "{" );

			if ( m_resourcePath.IsValid() )
			{
				b.Appendf( "res=\"%hs\",", red::Base64Encode( m_resourcePath.ToDebugString() ).AsChar() );
			}

			if ( m_internalId != 0 )
			{
				b.Appendf( "id=%llu,", m_internalId );
			}

			if ( !m_name.Empty() )
			{
				b.Appendf( "name=\"%hs\",", m_name.AsChar() );
			}

			if ( !m_kind.Empty() )
			{
				b.Appendf( "kind=%hs", m_kind.AsChar() );
			}

			b.Append( "}" );

			return b.ToString();
		}
	}

	Bool EditorObjectID::FromString(const String& txt, EditorObjectID& outId)
	{
		if (txt == "null")
		{
			outId = EditorObjectID();
			return true;
		}

		String nodeName;
		String kind;
		String encodedPath;
		Uint64 id = 0;

		const auto* str = txt.AsChar();

		if ( GParseKeyword( str, "{" ) )
		{
			for ( ;;)
			{
				if ( GParseKeyword( str, "}" ) )
				{
					break;
				}

				String name;
				if ( !GParseIdentifier( str, name ) )
				{
					return false;
				}

				if ( name != "res" && name != "id" && name != "name" && name != "kind" )
				{
					return false;
				}

				if ( !GParseKeyword( str, "=" ) )
				{
					return false;
				}

				if ( name == "res" )
				{
					if ( !GParseEscapedString( str, encodedPath ) )
					{
						return false;
					}
				}
				else if ( name == "id" )
				{
					if ( !GParseUint64( str, id ) )
					{
						return false;
					}
				}
				else if ( name == "name" )
				{
					if ( !GParseEscapedString( str, nodeName ) )
					{
						return false;
					}
				}
				else if ( name == "kind" )
				{
					if ( !GParseIdentifier( str, kind ) )
					{
						return false;
					}
				}

				GParseKeyword( str, "," );
			}
		}

		outId.m_name = RED_NAME( nodeName );
		outId.m_kind = RED_NAME( kind );
		outId.m_resourcePath = res::ResourcePath::Build( red::Base64Decode( encodedPath ) );
		outId.m_internalId = id;
		return true;
	}

} // tools
