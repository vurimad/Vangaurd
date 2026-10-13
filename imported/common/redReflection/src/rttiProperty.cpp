/**
* Copyright (c) 2007-2019 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "rttiProperty.h"
#include "scriptable.h"


//////////////////////////////////////////////////////////////////////////
// usings
using red::AlignOffset;
using red::OffsetPtr;
using red::DynArray;

namespace rtti
{
	// ctremblay about the GetFilteredPropertyName here. If property is from script, it's might still have the "m_". Should we compile script with m_ removed ?.
	// However this is code is editor only, might not be a big issue.
	// However #2 we want to remove CName strings in final.

	Property::Property( const rtti::IType* type, const rtti::ClassType* owner, Uint32 offset,
						const CName name, Uint64 flags, const String& tooltip,
						const CName category, const String& editorType,
						const Float minValue /*= 0.0f*/, const Float maxValue /*= 0.0f*/,
						Uint32 editorFlags
	)
		: m_parent( const_cast< const rtti::ClassType* >(owner) ) // TEMPSHIT
		, m_type( const_cast< const rtti::IType* >(type) ) // TEMPSHIT
		, m_name( GetFilteredPropertyName( name ) )
		, m_category( category )
		, m_offset( offset )
		, m_flags( flags )
#ifndef NO_EDITOR
		, m_minValue( minValue )
		, m_maxValue( maxValue )
		, m_xMinValue( 0.f )
		, m_xMaxValue( 0.f )
		, m_minSize( 0 )
		, m_maxSize( 0 )
		, m_customEditor( RED_NAME( editorType ) )
		, m_customEditorMode( CustomEditorMode::NormalType )
		, m_tooltip( tooltip )
		, m_editorFlags( editorFlags )
#endif
	{
		RED_UNUSED( editorType );
	}

	Property::~Property()
	{}

	void Property::Set( void *object, const void *buffer ) const
	{
		void* ptr = GetOffsetPtr( object );
		m_type->Copy( ptr, buffer );
	}

	void Property::Get( const void *object, void *buffer ) const
	{
		const void* ptr = GetOffsetPtr( object );
		m_type->Copy( buffer, ptr );
	}

	Uint32 Property::CalcDataLayout( const DynArray< const Property* >& properties, Uint32 initialOffset, const Uint32 initialAlignment /*= 4*/ )
	{
		// First we align the initial offset with the class offset
		Uint32 offset = static_cast< Uint32 >( AlignOffset( initialOffset, initialAlignment ) );

		// Pack the properties using the initial alignment
		for ( Uint32 i=0; i<properties.Size(); i++ )
		{
			Property* editableProp = const_cast< Property*> ( properties[i] );

			const Uint32 propSize = editableProp->GetType()->GetSize();
			const Uint32 propAlignment = editableProp->GetType()->GetAlignment();

			// Align property placement to the requested alignment
			// Note that the offset is ABSOLUTE (so base class is accounted for)
			offset = static_cast< Uint32 >( AlignOffset( offset, propAlignment ) );

			// Place property
			editableProp->m_offset = offset;
			offset += propSize;
		}

		// Return offset at the end of the last property in the structure, don't align it (it will be aligned by the next structure if there is one)
		return offset;
	}

	void Property::ChangePropertyFlag( const rtti::ClassType* objectClass, const CName& propertyName, Uint64 clearFlags, Uint64 setFlags )
	{
		RED_FATAL_ASSERT( objectClass, "Invalid object" );

		// Change property flags
		rtti::Property* prop = const_cast<rtti::Property*>( objectClass->FindProperty( propertyName ) );
		if ( prop )
		{
			prop->m_flags &= ~clearFlags;
			prop->m_flags |= setFlags;
		}
	}

	void Property::SetSizeConstraints( Uint32 minSize, Uint32 maxSize )
	{
#ifndef NO_EDITOR
		m_minSize = minSize;
		m_maxSize = maxSize;
#endif
	}

	void Property::SetXAxisConstraints( Float min, Float max )
	{
#ifndef NO_EDITOR
		m_xMinValue = min;
		m_xMaxValue = max;
#endif
	}

	void Property::SetCustomEditorMode( CustomEditorMode editorMode )
	{
#ifndef NO_EDITOR
		m_customEditorMode = editorMode;
#endif
	}

	void Property::SetKeyPath( const String& keyPath )
	{
#ifndef NO_EDITOR
		m_keyPath = keyPath;
#endif
	}

	void Property::ResolveOverride( const Property& baseProperty, Uint32 overrideMask )
	{
		// We need to resolve this property override into a complete property replacement,
		// taking missing (non overridden) attributes/flags from base property.
		// The mask "overrideMask" determines which properties were actually overridden (bit set for overridden, cleared for not changed).
		m_type = baseProperty.m_type;
		m_category = baseProperty.m_category;
		m_parent = baseProperty.m_parent;            // We copy parent, we want overridden property to look almost exactly like the original.
		m_offset = baseProperty.m_offset;

		// Prepare override mask for the flags. Since the state flags PF_* and override flags OA_* use different values we need to remap.
		Uint64 flagsMask = 0;            // When bit is set means this flag is overridden, when bit is clear we take the flag from base property.

		if ( ( overrideMask & PropertyOverrideBuilder::OA_Editable ) != 0 )
			flagsMask |= PF_Editable;
		if ( ( overrideMask & PropertyOverrideBuilder::OA_ReadOnly ) != 0 )
			flagsMask |= PF_ReadOnly;
		if ( ( overrideMask & PropertyOverrideBuilder::OA_Browsable ) != 0 )
			flagsMask |= PF_Browsable;
		if( ( overrideMask & PropertyOverrideBuilder::OA_InstanceEditable ) != 0 )
			flagsMask |= PF_InstanceEditable;

		m_flags = (m_flags & flagsMask) | (baseProperty.m_flags & ~flagsMask);    // Merge override flags with base property flags.
	}

	void * Property::GetScriptPropertyData( void* scriptable ) const
	{
		IScriptable* realObject = static_cast< IScriptable* >( scriptable );
		void * propertyData = realObject->GetScriptPropertyData();
		RED_FATAL_ASSERT( propertyData, "Not a scriptable property" );
		return propertyData;
	}
	
	const void * Property::GetScriptPropertyData( const void* scriptable ) const
	{
		const IScriptable* realObject = static_cast< const IScriptable* >( scriptable );
		const void * propertyData = realObject->GetScriptPropertyData();
		RED_FATAL_ASSERT( propertyData, "Not a scriptable property" );
		return propertyData;
	}

} // rtti