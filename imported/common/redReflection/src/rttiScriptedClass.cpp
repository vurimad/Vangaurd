/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "rttiClass.h"
#include "rttiScriptedClass.h"
#include "rttiProperty.h"


//////////////////////////////////////////////////////////////////////////
// usings
using red::AlignOffset;


namespace rtti
{
	void ScriptedClassType::RecalculateClassDataSize()
	{
		// Scripted struct
		if ( IsScriptedStruct() )
		{
			RED_FATAL_ASSERT( !IsScriptedClass(), "Structures cannot have the ScriptedClass flag set" );

			// Build data layout
			const Uint32 classAlignment = CalcScriptClassAlignment();
			SetAlignment( Max( GetAlignment(), classAlignment ) );
			const auto& allProperties = GetCachedProperties();
			const Uint32 propsSize = rtti::Property::CalcDataLayout( allProperties, 0, classAlignment );

			// Scripted classes should be padded to keep the size of the script class data matched with the aligned
			const Uint32 paddedSize = static_cast< Uint32 >( AlignOffset( propsSize, classAlignment ) );
			if ( paddedSize > GetSize() )
			{
				InternalSetSize( paddedSize );
			}
		}
		else
		{
			RED_FATAL_ASSERT( IsScriptedClass(), "Scripted class has no scripted flag set" );

			// Treat as normal scripted class
			ClassType::RecalculateClassDataSize();
		}
	}

	ScriptedClassType::ScriptedClassType( const CName name, Uint32 flags )
		: ClassType( name, 0, flags )
	{
		RED_FATAL_ASSERT( flags & ( CF_ScriptedClass | CF_ScriptedStruct ), "Has to be either scripted class or struct" );
	}

	void ScriptedClassType::OnConstruct( void * buffer ) const
	{
		// We do this only for script structs.
		// Classes require custom code (see ClassType::CreateObject) for proper data buffer initialization.
		if ( IsScriptedStruct() )
		{
			InitializeScriptedProperties( buffer );
			InitializeScriptDefaultValues( buffer );
		}

        // Find first native base class (if any)
        if( const ClassType* baseClass = GetFirstNativeBaseClass() )
        {
            const_cast<ClassType*>(baseClass)->Construct( buffer );
        }
	}

	void ScriptedClassType::OnDestruct( void * buffer ) const
	{
		// Destroy rtti properties
		DestroyProperties( buffer );

        if( const ClassType* baseClass = GetFirstNativeBaseClass() )
        {
            const_cast<ClassType*>(baseClass)->Destruct( buffer );
        }
	}

	Bool ScriptedClassType::Compare( const void* data1, const void* data2, Uint32 flags ) const
	{
		return ClassType::DeepCompare( data1, data2, flags );
	}

	void ScriptedClassType::Copy( void* dest, const void* src ) const
	{
		// Copy all scripted properties
		const TPropertyList& props = GetCachedProperties();
		for ( const rtti::Property* prop : props )
		{
			const void* srcPropData = prop->GetOffsetPtr( src );
			void* destPropData = prop->GetOffsetPtr( dest );
			prop->GetType()->Copy( destPropData, srcPropData );
		}

		if ( const ClassType* baseClass = GetFirstNativeBaseClass() )
		{
			baseClass->Copy( dest, src );
		}
		else
		{
			// If there's no base native class it has to be scripted structure
			RED_FATAL_ASSERT( IsScriptedStruct(), "Expected scripted struct" );
		}
	}

	const red::memory::Pool & ScriptedClassType::GetInnerTypeMemoryPool() const
	{
		if( const ClassType * classType = GetFirstNativeBaseClass() )
		{
			return classType->GetInnerTypeMemoryPool();
		}

		return red::PoolScript::GetInstance();
	}

	void * ScriptedClassType::AllocateClassBuffer() const
	{
		const Uint32 alignment = GetAlignment();
		Uint32 bufferSize = red::memory::RoundUp( GetSize(), alignment );
		void * buffer = RED_ALLOCATE_ALIGNED( GetInnerTypeMemoryPool(), bufferSize, alignment );
		return buffer;
	}

} // rtti