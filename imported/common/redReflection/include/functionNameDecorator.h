/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

//////////////////////////////////////////////////////////////////////////
// Rules of names decoration:
// - each function name is decorated using its name followed by underscore and
//   list of arguments types names, e.g. "test( Int32, Float )" is "test_Int32Float"
// - each fundamental type is represented by its full rtti name,
//   (with no "red" namespace), e.g. "int" is "Int32", "red::Float" is "Float"
// - dynamic array of type "T" is "array< T >"
// - static array of type "T" and size "S" is "T[ S ]"
// - handles of type "T" are represented just by "T" - this is because of existing
// - script references of type "T" are represented by "script_ref< T >"
//   scripts compiler restrictions (class types are mapped to handle types 
//   AFTER function names decoration is performed) - it may be fixed tho,
//   by performing decoration once again when we have complete type info (todo)
// - rtti type name needs to be mapped to its scripted alias if possible

class RED_REFLECTION_API FunctionNameDecorator : red::NonCopyable
{
public:

	class RED_REFLECTION_API TypeDescription
	{
	public:

		enum MetaType
		{
			Named,
			DynArray,
			StaticArray,
			Handle,
			WeakHandle,
			Reference
		};

		virtual ~TypeDescription();
		virtual const char* GetNameData() const = 0;
		virtual Uint32 GetNameLength() const = 0;
		virtual MetaType GetMetaType() const = 0;
		virtual Uint32 GetSize() const = 0; // for static arrays only
		virtual const TypeDescription* GetInternalType() const = 0; // for arrays, handles
	};

	class RED_REFLECTION_API FunctionDescription : red::NonCopyable
	{
	public:

		virtual ~FunctionDescription();
		virtual const char* GetNameData() const = 0;
		virtual Uint32 GetNameLength() const = 0;
		virtual Uint32 GetParamsCount() const = 0;
		virtual const TypeDescription* GetParamType( Uint32 i ) const = 0;
	};

	static CName CreateDecoratedName( const FunctionDescription& funcDesc );

private:

	static void WriteType( const TypeDescription& typeDesc, char* buffer, Uint32& index );
	static void WriteNamedType( const TypeDescription& typeDesc, char* buffer, Uint32& index );
	static void WriteDynArrayType( const TypeDescription& typeDesc, char* buffer, Uint32& index );
	static void WriteStaticArrayType( const TypeDescription& typeDesc, char* buffer, Uint32& index );
	static void WriteHandleType( const TypeDescription& typeDesc, char* buffer, Uint32& index );
	static void WriteWeakHandleType( const TypeDescription& typeDesc, char* buffer, Uint32& index );
	static void WriteScriptReferenceType( const TypeDescription& typeDesc, char* buffer, Uint32& index );

};