/**
* Copyright (c) 2015-2020 CD Projekt Red. All Rights Reserved.
*/

#pragma once

class DataBuffer;
class ISerializable;

namespace text
{
	/// generic reader interface
	class ITextReader
	{
	public:
		virtual ~ITextReader() {}

		virtual Uint32 GetVersion() const = 0;

		virtual const Bool BeginObject() = 0;
		virtual void EndObject() = 0;

		virtual const Bool BeginArray() = 0;
		virtual void EndArray() = 0;

		virtual const Bool BeginArrayElement() = 0;
		virtual void EndArrayElement() = 0;

		virtual const Bool BeginProperty( CName& outPropertyName ) = 0;
		virtual void EndProperty() = 0;

		virtual const Bool ReadValue( DataBuffer& outData ) = 0;
		virtual const Bool ReadValue( String& outValue ) = 0;
		virtual const Bool ReadValue( ISerializable*& outValue ) = 0;
	};

} // text
