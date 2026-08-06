/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "functionNameDecorator.h"
#include "../../redContainers/include/fundamentalStringConversion.h"

namespace
{
	constexpr Uint32 c_maxDecoratedNameLength = 256;
	constexpr Uint32 c_lastValidIndex = c_maxDecoratedNameLength - 1;
	const char c_dynArrayPrefix[] = "array<";
	const char c_referencePrefix[] = "script_ref<";
}

FunctionNameDecorator::TypeDescription::~TypeDescription() = default;

FunctionNameDecorator::FunctionDescription::~FunctionDescription() = default;

CName FunctionNameDecorator::CreateDecoratedName( const FunctionDescription& funcDesc )
{
	char buffer[ c_maxDecoratedNameLength ];

	const Uint32 funcNameLength = funcDesc.GetNameLength();
	RED_FATAL_ASSERT( funcNameLength + 1 < c_lastValidIndex );
	red::Memcpy( buffer, funcDesc.GetNameData(), funcNameLength );
	Uint32 index = funcNameLength;
	buffer[ index++ ] = ';';

	const Uint32 paramsCount = funcDesc.GetParamsCount();
	for ( Uint32 i = 0; i < paramsCount; i++ )
	{
		WriteType( *funcDesc.GetParamType( i ), buffer, index );
	}

	RED_FATAL_ASSERT( index <= c_lastValidIndex );
	buffer[ index ] = 0;
	return RED_NAME( red::StringView( buffer, index ) );
}

void FunctionNameDecorator::WriteType( const TypeDescription& typeDesc, char* buffer, Uint32& index )
{
	switch ( typeDesc.GetMetaType() )
	{
	case TypeDescription::MetaType::Named:
		WriteNamedType( typeDesc, buffer, index );
		break;
	case TypeDescription::MetaType::DynArray:
		WriteDynArrayType( typeDesc, buffer, index );
		break;
	case TypeDescription::MetaType::StaticArray:
		WriteStaticArrayType( typeDesc, buffer, index );
		break;
	case TypeDescription::MetaType::Handle:
		WriteHandleType( typeDesc, buffer, index );
		break;
	case TypeDescription::MetaType::WeakHandle:
		WriteWeakHandleType( typeDesc, buffer, index );
		break;
	case TypeDescription::MetaType::Reference:
		WriteScriptReferenceType( typeDesc, buffer, index );
		break;

	default:
		RED_FATAL_ASSERT( false, "Invalid meta type: %d.", typeDesc.GetMetaType() );
	}
}

void FunctionNameDecorator::WriteNamedType( const TypeDescription& typeDesc, char* buffer, Uint32& index )
{
	const Uint32 nameLength = typeDesc.GetNameLength();
	RED_FATAL_ASSERT( index + nameLength < c_lastValidIndex );
	red::Memcpy( buffer + index, typeDesc.GetNameData(), nameLength );
	index += nameLength;
}

void FunctionNameDecorator::WriteDynArrayType( const TypeDescription& typeDesc, char* buffer, Uint32& index )
{
	// write prefix
	static const Uint32 prefixLength = static_cast< Uint32 >( red::Strlen( c_dynArrayPrefix ) );
	RED_FATAL_ASSERT( index + prefixLength < c_lastValidIndex );
	red::Memcpy( buffer + index, c_dynArrayPrefix, prefixLength );
	index += prefixLength;

	// write internal type name
	WriteType( *typeDesc.GetInternalType(), buffer, index );

	// write postfix
	RED_FATAL_ASSERT( index + 1 < c_lastValidIndex );
	buffer[ index++ ] = '>';
}

void FunctionNameDecorator::WriteStaticArrayType( const TypeDescription& typeDesc, char* buffer, Uint32& index )
{
	// write internal type name
	WriteType( *typeDesc.GetInternalType(), buffer, index );

	// write '[Size]'
	RED_FATAL_ASSERT( index + 1 < c_lastValidIndex );
	buffer[ index++ ] = '[';
	red::String strSize;
	ToString( strSize, typeDesc.GetSize() );
	const Uint32 sizeLength = strSize.Length();
	RED_FATAL_ASSERT( index + sizeLength < c_lastValidIndex );
	red::Memcpy( buffer + index, strSize.Data(), sizeLength );
	index += sizeLength;
	RED_FATAL_ASSERT( index + 1 < c_lastValidIndex );
	buffer[ index++ ] = ']';
}

void FunctionNameDecorator::WriteHandleType( const TypeDescription& typeDesc, char* buffer, Uint32& index )
{
	// write internal type name
	WriteType( *typeDesc.GetInternalType(), buffer, index );
}

void FunctionNameDecorator::WriteWeakHandleType( const TypeDescription& typeDesc, char* buffer, Uint32& index )
{
	// write internal type name
	WriteType( *typeDesc.GetInternalType(), buffer, index );
}

void FunctionNameDecorator::WriteScriptReferenceType( const TypeDescription& typeDesc, char* buffer, Uint32& index )
{
	// write prefix
	static const Uint32 prefixLength = static_cast< Uint32 >( red::Strlen( c_referencePrefix ) );
	RED_FATAL_ASSERT( index + prefixLength < c_lastValidIndex );
	red::Memcpy( buffer + index, c_referencePrefix, prefixLength );
	index += prefixLength;

	// write internal type name
	WriteType( *typeDesc.GetInternalType(), buffer, index );

	// write postfix
	RED_FATAL_ASSERT( index + 1 < c_lastValidIndex );
	buffer[index++] = '>';
}