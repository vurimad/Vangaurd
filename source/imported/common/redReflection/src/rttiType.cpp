/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "rttiType.h"
#include "rttiClass.h"
#include "resource.h"
#include "textWriter.h"
#include "textReader.h"
#include "../../../common/redContainers/include/dynArrayAccessor.h"
#include "rttiAccessPath.h"
#include "rttiValueHolder.h"
#include "rttiSingleValueHolder.h"

using red::DynArray;
using red::DynArrayAccessor;

IRTTIContext::~IRTTIContext()
{
}

namespace helper
{
	class DefaultRTTIContext : public IRTTIContext
	{
	public:
		virtual const Bool ReportError( const rtti::IType* typeInfo, STATIC_CHECK_PRINTF_MSC const AnsiChar* txt, ... ) override final
		{
			AnsiChar buf[1024];
			va_list args;

			va_start( args, txt );
			red::VSNPrintF( buf, RED_ARRAY_COUNT(buf), txt, args );
			va_end( args );

			RED_LOG_ERROR( "Core: RTTI: %hs", buf );
			return false;
		}
	};

	class DummyRTTIContext : public IRTTIContext
	{
		virtual const Bool ReportError( const class rtti::IType* typeInfo, STATIC_CHECK_PRINTF_MSC const char* txt, ... ) override final
		{
			return false;
		}
	};
}

IRTTIContext& IRTTIContext::GetDefault()
{
	static helper::DefaultRTTIContext theContext;
	return theContext;
}

IRTTIContext& IRTTIContext::GetDummy()
{
	static helper::DummyRTTIContext theContext;
	return theContext;
}

namespace rtti
{
	IType::IType()
		: m_defaultReplicatedType( nullptr )
	{
	}

	IType::~IType()
	{
	}

	void IType::Move( void* dest, void* src ) const
	{
		Copy( dest, src );
	}
	
	String IType::GetERTTITypeString() const
	{

#define ENUM_STRING(x) case ERTTITypeType::x: return #x; 

		switch (GetType())
		{
			ENUM_STRING(RT_Name)
			ENUM_STRING(RT_Fundamental)
			ENUM_STRING(RT_Class)
			ENUM_STRING(RT_Array)
			ENUM_STRING(RT_Simple)
			ENUM_STRING(RT_Enum)
			ENUM_STRING(RT_StaticArray)
			ENUM_STRING(RT_NativeArray)
			ENUM_STRING(RT_Pointer)
			ENUM_STRING(RT_Handle)
			ENUM_STRING(RT_WeakHandle)
			ENUM_STRING(RT_ResourceReference)
			ENUM_STRING(RT_ResourceAsyncReference)
			ENUM_STRING(RT_BitField)
			ENUM_STRING(RT_LegacySingleChannelCurve)
			ENUM_STRING(RT_ScriptReference)
			default: break;
		}
#undef ENUM_STRING

		return "Unhandled ERTTITypeType";
	}

	CName IType::GetRefName() const
	{
		return rtti::FormatScriptedReferenceTypeName( GetName() );
	}
	
	const Bool IType::SerializeToText( text::ITextWriter& writer, const void* data ) const
	{
		// serialize to text directly
		String val;
		if (!ToString(data, val))
			return false;

		// store as single value in the target
		writer.WriteValue(val);
		return true;
	}

	const Bool IType::SerializeFromText( text::ITextReader& reader, void* data ) const
	{
		// load value
		String value;
		if ( !reader.ReadValue( value ) )
			return false;

		// parse as text
		return FromString(data, value);
	}

	const Bool IType::ReadValue( IRTTIContext& ctx, const void* data, const rtti::AccessPath& path, rtti::ValuePtr& outValue ) const
	{
		if ( !path.IsEmpty() )
			return ctx.ReportError( this, "Simple type does not has fine structure" );

		String text;
		if ( !ToString( data, text ) )
			return ctx.ReportError( this, "Unable to convert value to text" );

		outValue = rtti::ValueHolder::CreateSingle( text.AsChar() );
		return true;
	}

	const Bool IType::WriteValue( IRTTIContext& ctx, void* data, const rtti::AccessPath& path, const rtti::ValueHolder& newValue, bool clone ) const
	{
		if ( !path.IsEmpty() )
			return ctx.ReportError( this, "Simple type does not has fine structure" );

		if ( newValue.IsEmpty() )
		{
			Destruct( data );
			return true;
		}
		else if ( newValue.IsSingle() )
		{
			const auto& valueString = newValue.GetSingle()->ToString();
			if ( FromString( data, valueString ) )
				return true;

			return ctx.ReportError( this, "Unable to restore value from text" );
		}
		else
		{
			return ctx.ReportError( this, "Simple types do not support fine structured values" );
		}
	}

	Bool IType::IsPropertyReadOnly( IRTTIContext& ctx, const rtti::AccessPath& path, Bool& outReadOnly ) const
	{
		return ctx.ReportError( this, "Type does not contain any properties" );
	}

	void IType::RebuildParentHierarchy( void * object, ISerializable * parent ) const
	{}

	const red::memory::Pool & IType::GetInnerTypeMemoryPool() const
	{
		return red::PoolRTTI::GetInstance();
	}
}
