/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "textReader.h"
#include "rttiSystem.h"

namespace text
{
	/// uses simplified JSON format
	class RED_REFLECTION_API StandardReader : public text::ITextReader, public red::NonCopyable
	{
	public:
		StandardReader(const AnsiChar*& str);
		virtual ~StandardReader();

		virtual Uint32 GetVersion() const override final;

		virtual const Bool BeginObject() override final;
		virtual void EndObject() override final;

		virtual const Bool BeginArray() override final;
		virtual void EndArray() override final;

		virtual const Bool BeginArrayElement() override final;
		virtual void EndArrayElement() override final;

		virtual const Bool BeginProperty(CName& outPropertyName) override final;
		virtual void EndProperty() override final;

		virtual const Bool ReadValue(DataBuffer& outData) override final;
		virtual const Bool ReadValue(String& outValue) override final;
		virtual const Bool ReadValue(ISerializable*& outValue) override final;

	private:
		const AnsiChar*& m_str;

		typedef red::DynArray< THandle< ISerializable > > TCreatedObjects;
		typedef red::HashMap< Uint32, THandle< ISerializable > > TCreatedObjectsMap;

		TCreatedObjects m_createdObjects{ red::PoolEngine() };
		TCreatedObjectsMap m_createdObjectsMap{ red::PoolEngine() };

		red::DynArray<Uint32> m_elementCount{ red::PoolEngine() };
	};

	/// load ANY data from compatible text
	extern RED_REFLECTION_API const Bool LoadFromText(const String& txt, void* data, const rtti::IType* type);

	template< typename T >
	RED_INLINE const Bool LoadFromTextAndConvertSafe( T& destination, const String& source )
	{
		const rtti::IType* type = GetTypeObject< T >();

		if ( type )
		{
			return LoadFromText( source, &destination, type );
		}

		return false;
	}
} // namespace text
