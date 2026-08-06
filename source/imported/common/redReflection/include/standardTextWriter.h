/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "textWriter.h"

#include "../../redContainers/include/string/stringBuilder.h"

namespace text
{
	/// helper class that can write RTTI values to a JSON-like text file
	/// Be warned this does will not output a JSON compliant string
	class RED_REFLECTION_API StandardWriter : public text::ITextWriter, public red::NonCopyable
	{
	public:
		StandardWriter( red::StringBuilder<String>& builder );
		virtual ~StandardWriter();

		virtual Uint32 GetVersion() const override final;

		virtual void BeginObject( const rtti::IType* type, const void* data ) override final;
		virtual void EndObject() override final;

		virtual void BeginArray() override final;
		virtual void EndArray() override final;

		virtual void BeginArrayElement() override final;
		virtual void EndArrayElement() override final;

		virtual void BeginProperty( const CName& name ) override final;
		virtual void EndProperty() override final;

		virtual void WriteValue( const void* data, Uint32 size ) override final;
		virtual void WriteValue( const String& str ) override final;
		virtual void WriteValue( const ISerializable* object ) override final;

	private:
		red::StringBuilder<String>& m_builder;
		red::DynArray<Uint32> m_itemCounts{ red::PoolEngine() };

		red::HashMap<const void*, Uint32> m_mappedObjects{ red::PoolEngine() }; // only ones by reference
		Uint32 m_objectIDAllocator;
	};

	/// save ANY data to compatible text
	extern RED_REFLECTION_API const Bool SaveToText( const void* data, const rtti::IType* type, String& outText );

} // namespace text
