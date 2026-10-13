/**
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "resourceReferenceScriptToken.h"
#include "../../redReflection/include/rttiClassBuilder.h"
#include "../../redReflection/include/scriptStackFrame.h"


RTTI_BEGIN_TYPE_IN_NAMESPACE( ResourceReferenceScriptToken, red );
	RTTI_IMPORT_ONLY();
	RTTI_SCRIPT_ALIAS( "ResRef" );
	RTTI_NATIVE_STATIC_FUNCTION( "FromString", funcFromString );
	RTTI_NATIVE_STATIC_FUNCTION( "FromHash", funcFromHash );
	RTTI_NATIVE_STATIC_FUNCTION( "FromName", funcFromName );
	RTTI_NATIVE_STATIC_FUNCTION( "IsValid", funcIsValid );
	RTTI_PROPERTY( m_resource ).instanceEditable();
RTTI_END_TYPE();

namespace red
{
	ResourceReferenceScriptToken::ResourceReferenceScriptToken()
		: m_resource()
	{
	}

	ResourceReferenceScriptToken::ResourceReferenceScriptToken( const TResAsyncRef<CResource>& resource )
		: m_resource( resource )
	{
	}

	ResourceReferenceScriptToken::ResourceReferenceScriptToken( const red::StringView& path )
		: m_resource( res::ResourcePath::Build( path ) )
	{
	}

	ResourceReferenceScriptToken::ResourceReferenceScriptToken( Uint64 hash )
		: m_resource( res::ResourcePath::Build( hash ) )
	{
	}

	ResourceReferenceScriptToken::ResourceReferenceScriptToken( const res::ResourcePath& path )
		: ResourceReferenceScriptToken( path.GetHash() )
	{
	}

	ResourceReferenceScriptToken::ResourceReferenceScriptToken( CName name )
		: ResourceReferenceScriptToken( name.GetHash() )
	{
	}

	res::ResourcePath ResourceReferenceScriptToken::ToResourcePath() const
	{
		return m_resource.GetPath();
	}

	Uint64 ResourceReferenceScriptToken::ToHash() const
	{
		return m_resource.GetPath().GetHash();
	}

	Bool ResourceReferenceScriptToken::IsValid() const
	{
		return m_resource.IsValid();
	}

	void ResourceReferenceScriptToken::funcFromString(IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType)
	{
		GET_PARAMETER( String, input, String() );
		FINISH_PARAMETERS;
		RETURN_STRUCT( ResourceReferenceScriptToken, ResourceReferenceScriptToken( input ) );
	}

	void ResourceReferenceScriptToken::funcFromHash( IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
	{
		GET_PARAMETER( Uint64, input, 0 );
		FINISH_PARAMETERS;
		RETURN_STRUCT( ResourceReferenceScriptToken, ResourceReferenceScriptToken( input ) );
	}

	void ResourceReferenceScriptToken::funcFromName(IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType)
	{
		GET_PARAMETER( CName, input, CName::NONE() );
		FINISH_PARAMETERS;
		RETURN_STRUCT( ResourceReferenceScriptToken, ResourceReferenceScriptToken( input ) );
	}

	void ResourceReferenceScriptToken::funcIsValid(IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType)
	{
		GET_PARAMETER( ResourceReferenceScriptToken, input, ResourceReferenceScriptToken() );
		FINISH_PARAMETERS;

		RETURN_BOOL( input.IsValid() );
	}

	Bool ToString( const red::ResourceReferenceScriptToken* data, String& valueString )
	{
		valueString = data->ToResourcePath().ToString();
		return true;
	}

	Bool FromString( red::ResourceReferenceScriptToken* data, const String& valueString )
	{
		*data = red::ResourceReferenceScriptToken( valueString );
		return true;
	}
}