/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "variant.h"
#include "rttiSimpleType.h"
#include "serializationUtils.h"
#include "resourceReferenceScriptToken.h"

#include "../../redFileSystem/include/fileSkipableBlock.h"
#include "../../redContainers/include/string/stringBuilder.h"
#include "../../redFileSystem/include/file.h"
#include "../../redContainers/include/dynArrayAccessor.h"

RTTI_DEFINE_SIMPLE_TYPE_ALIAS( Variant, rtti::Variant );

namespace rtti
{

Bool Variant::operator==( const Variant& other ) const
{
	const rtti::IType* myType = GetTypeInternal();
	const rtti::IType* otherType = other.GetTypeInternal();
	if ( !myType && !otherType )
	{
		return true;
	}

	const void* myData = GetData();
	const void* otherData = other.GetData();
	if ( myType != nullptr && otherType != nullptr && myData != nullptr && otherData != nullptr)
	{
		if ( m_type == other.m_type )
		{			
			return myType->Compare( myData, otherData, 0 );
		}
	}
	return false;
}

Bool Variant::operator!=( const Variant& other ) const
{
	return !( operator==( other ) );
}

//////////////////////////////////////////////////////////////////////////

void Variant::Set( const rtti::IType* type, const void* data )
{
	if ( GetTypeInternal() != type )
	{
		ChangeType( type, data );
	}
	else
	{
		SetValueInternal( data );
	}
}

void Variant::Set( CName typeName, const void* data )
{
	if ( GetTypeName() != typeName )
	{
		const rtti::IType* newType = GetRttiSystem().FindType( typeName );
		RED_ASSERT( newType != nullptr, "Trying to set Variant to missing type %hs", typeName.AsChar() );
		ChangeType( newType, data );
	}
	else
	{
		SetValueInternal( data );
	}
}

void Variant::ChangeType( const rtti::IType* newType, const void* data )
{
	const rtti::IType* myType = GetTypeInternal();
	// assuming here that newType differs from the actual one
	RED_ASSERT( myType != newType, "Assuming types differ here, %hs != %hs", myType ? myType->GetName().AsChar() : "(null)", newType ? newType->GetName().AsChar() : "(null)" );

	Uint32 currentTypeSize = 0;
	if ( myType != nullptr )
	{
		currentTypeSize = myType->GetSize();
		myType->Destruct( Internal_GetData() );
	}
	if ( newType == nullptr || currentTypeSize != newType->GetSize() )
	{
		// reallocate data only if types differ in size
		FreeData();
	}
	m_type = newType;
	InitDataAndFlag();
	SetValueInternal( data );
}

void* Variant::InitDataAndFlag()
{
	if ( m_type != nullptr )
	{
		RED_FATAL_ASSERT( ( reinterpret_cast< uintptr_t >( m_type ) & ~( MASK_POINTER ) ) == 0, "Assuming m_type to be pure pointer without flags." );
		const Uint32 size = m_type->GetSize();
		const Bool keepingDataDirectly = ( size <= DIRECT_DATA_SIZE ) && m_type->GetAlignment() <= 8;
		if ( keepingDataDirectly )
		{
			m_data = nullptr;
			m_type->Construct( m_dataDirect );

			if(m_type->GetType() == RT_Array)
			{
				red::DynArrayAccessor& array = red::DynArrayAccessor::GetRef( m_dataDirect );
				array.SetPool( red::PoolEngine() );
			}

			reinterpret_cast< uintptr_t& >( m_type ) |= FLAG_KEEP_DATA_DIRECTLY;
			
			return m_dataDirect;
		}
		else
		{
			if ( m_data == nullptr )
			{
				m_data = RED_ALLOCATE_ALIGNED( red::PoolEngine, size, m_type->GetAlignment() );
			}
			m_type->Construct( m_data );

			if( m_type->GetType() == RT_Array )
			{
				red::DynArrayAccessor& array = red::DynArrayAccessor::GetRef( m_data );
				array.SetPool( red::PoolEngine() );
			}

			return m_data;
		}
	}
	return nullptr;
}

void Variant::FreeData()
{
	// Free buffer
	if ( !IsKeepingDataDirectly() )
	{
		RED_FREE( red::PoolEngine, m_data );
	}

	// clear ptr, that would also nullify directly kept data
	m_data = nullptr;
}

void Variant::Clear()
{
	const rtti::IType* myType = GetTypeInternal();
	if ( myType != nullptr )
	{
		myType->Destruct( Internal_GetData() );
		FreeData();
		m_type = nullptr;
	}
	else
	{
		RED_ASSERT( m_data == nullptr, "Varian cannot have empty type and non-empty data." );
	}
}

//////////////////////////////////////////////////////////////////////////

String Variant::ValueToString() const
{
	const rtti::IType* myType = GetTypeInternal();
	if ( myType == nullptr )
	{
		return "None";
	}	
	else
	{
		RED_ASSERT( GetData() != nullptr, "Variant wasn't properly initialized." );
		String value;
		bool success = myType->ToString( GetData(), value );
		if (!success)
		{
			if ( myType == GetTypeObject< red::ResourceReferenceScriptToken >() )
			{
				if ( red::ToString( reinterpret_cast< const red::ResourceReferenceScriptToken* >( GetData() ), value ) )
				{
					return value;
				}
			}

			return String(GetTypeName().AsChar()) + "(" + myType->GetERTTITypeString() + ")";
		}
		return value;
	}
}

Bool Variant::ValueToString( String& value ) const
{
	const rtti::IType* myType = GetTypeInternal();
	if ( myType != nullptr )
	{
		RED_ASSERT( GetData() != nullptr, "Variant wasn't properly initialized." );
		String ret;
		if ( myType->ToString( GetData(), ret ) )
		{
			value = ret;
			return true;
		}

		if ( myType == GetTypeObject< red::ResourceReferenceScriptToken >() )
		{
			return red::ToString( reinterpret_cast< const red::ResourceReferenceScriptToken* >( GetData() ), ret );
		}
	}

	// Invalid type
	return false;
}

Bool Variant::ValueFromString( const char* value )
{
	const rtti::IType* myType = GetTypeInternal();
	if ( myType != nullptr )
	{
		RED_ASSERT( GetData() != nullptr, "Variant wasn't properly initialized." );
		if ( myType->FromString( Internal_GetData(), value ) )
		{
			return true;
		}

		if ( myType == GetTypeObject< red::ResourceReferenceScriptToken >() )
		{
			return red::FromString( reinterpret_cast< red::ResourceReferenceScriptToken* >( Internal_GetData() ), value );
		}
	}

	// Invalid type
	return false;
}

Bool Variant::ValueFromString( const String& value )
{
	const rtti::IType* myType = GetTypeInternal();
	if ( myType != nullptr )
	{
		RED_ASSERT( GetData() != nullptr, "Variant wasn't properly initialized." );
		if ( myType->FromString( Internal_GetData(), value ) )
		{
			return true;
		}

		if ( myType == GetTypeObject< red::ResourceReferenceScriptToken >() )
		{
			return red::FromString( reinterpret_cast< red::ResourceReferenceScriptToken* >( Internal_GetData() ), value );
		}
	}

	// Invalid type
	return false;
}

// required by rtti::TSimpleType< Variant >
const Bool ToString( String& outTxt, const Variant& val )
{
	if ( const rtti::IType* myType = val.GetRTTIType() )
	{
		String valueString;
		if ( val.ValueToString( valueString ) )
		{
			red::StringBuilder<String> sb;
			sb.Append( myType->GetName().AsChar() );
			sb.Append( ";" );
			sb.Append( valueString );
			outTxt = sb.ToString();
			return true;
		}
	}
	return false;
}

const Bool FromString( const String& txt, Variant& outVal )
{
	Uint32 colonIndex;
	if ( txt.IndexOf( ';', colonIndex ) )
	{
		red::StringView s( txt.AsChar(), colonIndex );
		outVal.Set( RED_NAME( s.ToString() ), nullptr );
		return outVal.ValueFromString( txt.AsChar() + colonIndex + 1 );
	}
	return false;
}

//////////////////////////////////////////////////////////////////////////

void Variant::Serialize( IFile& file )
{
	if ( file.IsWriter() )
	{
		const rtti::IType* myType = GetTypeInternal();
		// Protected value block
		if ( myType != nullptr )
		{			
			RED_ASSERT( GetData() != nullptr, "Variant wasn't properly initialized." );
			// Start with type name
			CName typeName = myType->GetName();				
			file << typeName;
			// Data
			fs::SkipableBlock block( file );	// we need this block for backward data compatibility
			myType->Serialize( file, Internal_GetData() );
		}
		else
		{
			// Serialize empty type name
			CName typeName = CName::NONE();
			file << typeName;
			fs::SkipableBlock block( file );	// we need this block for backward data compatibility
		}
	}
	else if ( file.IsReader() )
	{
		// Load type name
		CName typeName;
		file << typeName;

		// Restore property value
		{
			fs::SkipableBlock block( file );

			// Create property
			Set( typeName, nullptr );

			// Property initialized, load data
			const rtti::IType* myType = GetTypeInternal();
			if ( myType != nullptr )
			{
				myType->Serialize( file, Internal_GetData() );
			}
			else
			{
				// Skip data
				block.Skip();
			}
		}
	}
}

Bool Variant::SerializeData( IFile& file )
{
	const rtti::IType* myType = GetTypeInternal();
	if ( myType != nullptr )
	{
		return myType->Serialize( file, Internal_GetData() );
	}
	return false;
}

void * Variant::Internal_GetData()
{ 
	return IsKeepingDataDirectly() ? m_dataDirect : m_data; 
}

} // rtti
