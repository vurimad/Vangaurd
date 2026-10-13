/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "messageLocation.h"
#include "../redContainers/include/fundamentalStringParser.h"
#include "../redContainers/include/string/stringUtils.h"
#include "../redReflection/include/rttiSystem.h"

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( IMessageLocation, tools );
	RTTI_PARENT_TYPE( ISerializable );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( MessageLocation_Resource, tools );
	RTTI_PARENT_TYPE( IMessageLocation );
	RTTI_PROPERTY( m_path );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( MessageLocation_EditorObject, tools );
	RTTI_PARENT_TYPE( IMessageLocation );
	RTTI_PROPERTY( m_path );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( MessageLocation_Point, tools );
	RTTI_PARENT_TYPE( IMessageLocation );
	RTTI_PROPERTY( m_resourcePath );
	RTTI_PROPERTY( m_point );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( MessageLocation_BoundingBox, tools );
	RTTI_PARENT_TYPE( IMessageLocation );
	RTTI_PROPERTY( m_resourcePath );
	RTTI_PROPERTY( m_box );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( MessageLocation_Webpage, tools );
	RTTI_PARENT_TYPE( IMessageLocation );
	RTTI_PROPERTY( m_link );
	RTTI_PROPERTY( m_text );
RTTI_END_TYPE();

namespace tools
{
	IMessageLocationGotoFacilitator::~IMessageLocationGotoFacilitator() = default;

	//////////////////////////////////////////////////////////////////////////

	IMessageLocation::~IMessageLocation() = default;

	MessageLocationPtr IMessageLocation::Build( const res::ResourcePath& resourcePath )
	{
		return message::CreateLocationPtr< MessageLocation_Resource >( resourcePath );
	}

	MessageLocationPtr IMessageLocation::Build( const res::ResourcePath& resourcePath, const math::Box& boundingBox )
	{
		return message::CreateLocationPtr< MessageLocation_BoundingBox >( resourcePath, boundingBox );
	}

	MessageLocationPtr IMessageLocation::Build( const res::ResourcePath& resourcePath, const math::Vector3& point )
	{
		return message::CreateLocationPtr< MessageLocation_Point >( resourcePath, point );
	}

	MessageLocationPtr IMessageLocation::Build( const EditorObjectIDPath& editorObjectPath )
	{
		return message::CreateLocationPtr< MessageLocation_EditorObject >( editorObjectPath );
	}

	MessageLocationPtr IMessageLocation::Build( const res::ResourcePath& resourcePath, const String& link, const String& text )
	{
		return message::CreateLocationPtr< MessageLocation_Webpage >( resourcePath, link, text );
	}

	MessageLocationPtr IMessageLocation::BuildFromString( red::StringView location )
	{
		if ( location.Empty() )
		{
			return nullptr;
		}

		String type;
		String value;

		MessageLocationPtr object;

		const auto* str = location.Data();

		if ( GParseKeyword( str, "{" ) )
		{
			for ( ;; )
			{
				if ( GParseKeyword( str, "}" ) )
				{
					break;
				}

				String name;
				if ( !GParseIdentifier( str, name ) )
				{
					return nullptr;
				}

				if ( name != "type" && name != "value" )
				{
					return nullptr;
				}

				if ( !GParseKeyword( str, "=" ) )
				{
					return nullptr;
				}

				if ( name == "type" )
				{
					if ( !GParseIdentifier( str, type ) )
					{
						return nullptr;
					}

					auto &rttiTypes = GetRttiSystem();
					const rtti::ClassType* klass = rttiTypes.FindClass( RED_NAME_NOREG( type ) );
					RED_ASSERT( klass );
					object = klass->CreateHandle< IMessageLocation >();
					RED_ASSERT( object );
				}
				else if ( name == "value" )
				{
					// TODO(artur.wyszynski) Rewrite those GParse* to StringView..
					Uint32 difference = static_cast< Uint32 >( str - location.Data() );
					red::StringView view = location;
					view.TrimFront( difference );
					Uint32 size = view.Length() - 1;
					view = view.SubView( 0, size );

					if ( !object->DeserializeFromString( view ) )
					{
						return nullptr;
					}

					str += size;
				}

				GParseKeyword( str, "," );
			}
		}

		return object;
	}

	//////////////////////////////////////////////////////////////////////////

	MessageLocation_Resource::MessageLocation_Resource() = default;
	MessageLocation_Resource::MessageLocation_Resource( const res::ResourcePath& resourcePath )
		: m_path( resourcePath )
	{ }

	String MessageLocation_Resource::ToString() const
	{
		return m_path->ToDebugString();
	}

	String MessageLocation_Resource::SerializeToString() const
	{
		red::StringBuilder<String> b;

		b.Append( "{" );
		b.Appendf( "type=%hs,", GetClass()->GetName().AsChar() );
		b.Appendf( "value={res=\"%hs\"}", red::Base64Encode( m_path->ToDebugString() ).AsChar() );
		b.Append( "}" );

		return b.ToString();
	}

	Bool MessageLocation_Resource::DeserializeFromString( red::StringView string )
	{
		if ( string.Empty() )
		{
			return false;
		}

		String type;
		String encodedPath;

		const char *str = string.Data();

		if ( GParseKeyword( str, "{" ) )
		{
			for ( ;; )
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

				if ( name != "res" )
				{
					return false;
				}

				if ( !GParseKeyword( str, "=" ) )
				{
					return false;
				}

				if ( !GParseEscapedString( str, encodedPath ) )
				{
					return false;
				}

				GParseKeyword( str, "," );
			}
		}

		m_path = res::ResourcePath::Build( Base64Decode( encodedPath ) );

		return true;
	}

	res::ResourcePath MessageLocation_Resource::GetResourcePath() const
	{
		return *m_path;
	}

	void MessageLocation_Resource::Goto( IMessageLocationGotoFacilitator* )
	{
		// Do nothing, editors are opened by the frontend
		// so there's nothing extra we need to do here
	}

	//////////////////////////////////////////////////////////////////////////

	MessageLocation_EditorObject::MessageLocation_EditorObject() = default;
	MessageLocation_EditorObject::MessageLocation_EditorObject( const EditorObjectIDPath& objectPath )
		: m_path( objectPath )
	{ }

	MessageLocation_EditorObject::MessageLocation_EditorObject( const EditorObjectID& objectID )
		: m_path( objectID )
	{ }

	String MessageLocation_EditorObject::ToString() const
	{
		return m_path.GetFinalChildID().GetResourcePath().ToString();
	}

	String MessageLocation_EditorObject::SerializeToString() const
	{
		red::StringBuilder<String> b;

		b.Append( "{" );
		b.Appendf( "type=%hs,", GetClass()->GetName().AsChar() );
		b.Appendf( "value=%hs", m_path.ToString().AsChar() );
		b.Append( "}" );

		return b.ToString();
	}

	Bool MessageLocation_EditorObject::DeserializeFromString( red::StringView string )
	{
		return EditorObjectIDPath::FromString( string.ToString(), m_path );
	}

	res::ResourcePath MessageLocation_EditorObject::GetResourcePath() const
	{
		return m_path.GetFirstChildID().GetResourcePath();
	}

	void MessageLocation_EditorObject::Goto( IMessageLocationGotoFacilitator* facilitator )
	{
		facilitator->Goto( m_path );
	}

	const EditorObjectIDPath& MessageLocation_EditorObject::GetEditorObjectIDPath() const
	{
		return m_path;
	}

	//////////////////////////////////////////////////////////////////////////

	MessageLocation_Point::MessageLocation_Point() = default;
	MessageLocation_Point::MessageLocation_Point( const res::ResourcePath& resourcePath, const math::Vector3& point )
		: m_resourcePath( resourcePath )
		, m_point( point )
	{}

	String MessageLocation_Point::ToString() const
	{
		return String::Printf( "[%f, %f, %f]", m_point.X, m_point.Y, m_point.Z );
	}

	String MessageLocation_Point::SerializeToString() const
	{
		red::StringBuilder<String> b;

		b.Append( "{" );
		b.Appendf( "type=%hs,", GetClass()->GetName().AsChar() );
		b.Appendf( "value={res=\"%hs\", point={%f, %f, %f}}", red::Base64Encode( m_resourcePath->ToDebugString() ).AsChar(), m_point.X, m_point.Y, m_point.Z );
		b.Append( "}" );

		return b.ToString();
	}

	Bool MessageLocation_Point::DeserializeFromString( red::StringView string )
	{
		RED_FATAL( "Not implemented" );
		return false;
	}

	res::ResourcePath MessageLocation_Point::GetResourcePath() const
	{
		return *m_resourcePath;
	}

	void MessageLocation_Point::Goto( IMessageLocationGotoFacilitator* facilitator )
	{
		// TODO
	}

	//////////////////////////////////////////////////////////////////////////

	MessageLocation_BoundingBox::MessageLocation_BoundingBox() = default;
	MessageLocation_BoundingBox::MessageLocation_BoundingBox( const res::ResourcePath& resourcePath, const math::Box& boundingBox )
		: m_resourcePath( resourcePath )
		, m_box( boundingBox )
	{}

	String MessageLocation_BoundingBox::ToString() const
	{
		return String::Printf( "[%f, %f, %f]...[%f, %f, %f]", m_box.Min.X, m_box.Min.Y, m_box.Min.Z, m_box.Max.X, m_box.Max.Y, m_box.Max.Z );
	}

	String MessageLocation_BoundingBox::SerializeToString() const
	{
		red::StringBuilder<String> b;

		b.Append( "{" );
		b.Appendf( "type=%hs,", GetClass()->GetName().AsChar() );
		b.Appendf( "value={res=\"%hs\", box={min={%f, %f, %f}, max={%f, %f, %f}}}",
			red::Base64Encode( m_resourcePath->ToDebugString() ).AsChar(),
			m_box.Min.X, m_box.Min.Y, m_box.Min.Z, m_box.Max.X, m_box.Max.Y, m_box.Max.Z );
		b.Append( "}" );

		return b.ToString();
	}

	Bool MessageLocation_BoundingBox::DeserializeFromString( red::StringView string )
	{
		RED_FATAL( "Not implemented" );
		return false;
	}

	res::ResourcePath MessageLocation_BoundingBox::GetResourcePath() const
	{
		return *m_resourcePath;
	}

	void MessageLocation_BoundingBox::Goto( IMessageLocationGotoFacilitator* facilitator )
	{
		// TODO
	}

	//////////////////////////////////////////////////////////////////////////

	MessageLocation_Webpage::MessageLocation_Webpage() = default;
	MessageLocation_Webpage::MessageLocation_Webpage( const res::ResourcePath& resourcePath, const String& link, const String& text )
		: m_resourcePath{ resourcePath }
		, m_link{ link }
		, m_text{ text }
	{}

	String MessageLocation_Webpage::ToString() const
	{
		return m_text;
	}

	String MessageLocation_Webpage::SerializeToString() const
	{
		red::StringBuilder<String> b;

		const String linkEncoded = Base64Encode( m_link );
		const String textEncoded = Base64Encode( m_text );

		b.Append( "{" );
		b.Appendf( "type=%hs,", GetClass()->GetName().AsChar() );
		b.Appendf( "value={res=\"%hs\", link=%hs, text=%hs}",
			red::Base64Encode( m_resourcePath->ToDebugString() ).AsChar(),
			linkEncoded.AsChar(), textEncoded.AsChar() );
		b.Append( "}" );

		return b.ToString();
	}

	Bool MessageLocation_Webpage::DeserializeFromString( red::StringView string )
	{
		RED_FATAL( "Not implemented" );
		return false;
	}

	res::ResourcePath MessageLocation_Webpage::GetResourcePath() const
	{
		return *m_resourcePath;
	}

	void MessageLocation_Webpage::Goto( IMessageLocationGotoFacilitator* facilitator )
	{
		facilitator->Goto( m_link );
	}

} // tools
