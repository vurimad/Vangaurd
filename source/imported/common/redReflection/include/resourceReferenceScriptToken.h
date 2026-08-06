/**
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../redReflection/include/resourceAsyncReference.h"
#include "../../redReflection/include/resource.h"
#include "../../redReflection/include/rttiClassDeclarationMacros.h"
#include "../../redReflection/include/rttiTypeName.h"

class IScriptable;
class CScriptStackFrame;

namespace red
{
	class RED_REFLECTION_API ResourceReferenceScriptToken
	{
		RTTI_DECLARE_TYPE( ResourceReferenceScriptToken );

	public:
		ResourceReferenceScriptToken();
		// for TweakDB interface
		ResourceReferenceScriptToken( const TResAsyncRef< CResource >& resource );
		explicit ResourceReferenceScriptToken( const red::StringView& path );
		explicit ResourceReferenceScriptToken( Uint64 hash );
		explicit ResourceReferenceScriptToken( CName name );
		explicit ResourceReferenceScriptToken( const res::ResourcePath& path );

		res::ResourcePath ToResourcePath() const;
		Uint64 ToHash() const;
		Bool IsValid() const;

	private:
		static void funcFromString( IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
		static void funcFromHash( IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
		static void funcFromName( IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
		static void funcIsValid( IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );

		TResAsyncRef< CResource > m_resource;
	};

	RED_REFLECTION_API Bool ToString( const red::ResourceReferenceScriptToken* data, String& valueString );
	RED_REFLECTION_API Bool FromString( red::ResourceReferenceScriptToken* data, const String& valueString );
}