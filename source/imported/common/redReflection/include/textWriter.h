/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

class ISerializable;

namespace rtti
{
	class IType;
}

namespace text
{

	/// generic writer interface
	class ITextWriter
	{
	public:
		virtual ~ITextWriter() {}

		virtual Uint32 GetVersion() const = 0;

		virtual void BeginObject( const rtti::IType* type, const void* data ) = 0;
		virtual void EndObject() = 0;

		virtual void BeginArray() = 0;
		virtual void EndArray() = 0;

		virtual void BeginArrayElement() = 0;
		virtual void EndArrayElement() = 0;

		virtual void BeginProperty( const CName& name ) = 0;
		virtual void EndProperty() = 0;

		virtual void WriteValue( const void* data, Uint32 size ) = 0;
		virtual void WriteValue( const String& str ) = 0;
		virtual void WriteValue( const ISerializable* object ) = 0; // explicit, needed for mapping
	};

} // text
