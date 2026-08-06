/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "scriptable.h"
#include "scriptStackFrame.h"
#include "serializationNullMapper.h"
#include "../../redFileSystem/include/nullFile.h"
#include "serializableMap.h"
#include "rttiClassBuilder.h"


RTTI_BEGIN_TYPE( IScriptable );
	RTTI_PARENT_TYPE( ISerializable );
	// General part
	RTTI_NATIVE_FUNCTION( "ToString", funcToString );
	RTTI_NATIVE_FUNCTION( "GetClassName", funcGetClassName, FF_ConstFunction );
	RTTI_NATIVE_FUNCTION( "IsA", funcIsA, FF_ConstFunction );
	RTTI_NATIVE_FUNCTION( "IsExactlyA", funcIsExactlyA, FF_ConstFunction );
	RTTI_NATIVE_STATIC_FUNCTION( "DetectScriptableCycles", funcDetectScriptableCycles );
RTTI_END_TYPE();


IScriptable::IScriptable()
	: m_class( nullptr )
	, m_scriptData( nullptr )	
{
#ifdef DETECT_SCRIPTABLE_CYCLES
	debug::ScriptableCyclesDetector::Register( this );
#endif
}

IScriptable::~IScriptable()
{
#ifdef DETECT_SCRIPTABLE_CYCLES
	debug::ScriptableCyclesDetector::Unregister( this );
#endif

	// release script data buffers
	ReleaseScriptPropertiesBuffer();
}

IScriptable::IScriptable( const IScriptable& other )
	: m_class( other.m_class )
	, m_scriptData( nullptr )
{
	CopyScriptPropertiesBuffer( other );
}

IScriptable& IScriptable::operator=( const IScriptable& other )
{
	RED_FATAL_ASSERT( GetClass() == other.GetClass(), "Cannot assign IScriptable of different type." );
	CopyScriptPropertiesBuffer( other );
	return *this;
}

void IScriptable::BindLocalClassType( const rtti::ClassType* classType, void * scriptData ) const
{
	RED_FATAL_ASSERT( m_class == nullptr || m_class == classType, "Scriptable object already bound to a rtti ClassType.");
	RED_FATAL_ASSERT( m_scriptData == nullptr || m_scriptData == scriptData, "Script data alrady allocated. Possible memory leak.");
	m_class = classType;
	m_scriptData = scriptData;
}

const rtti::Function* IScriptable::FindFunction( CName functionName ) const
{
	const rtti::ClassType * classType = GetLocalClass();
	return classType->FindFunction( functionName );
}

const rtti::Function* IScriptable::FindFunctionByHash( Uint64 hash ) const
{
	const rtti::ClassType * classType = GetLocalClass();
	return classType->FindFunctionByHash( hash );
}

void IScriptable::EnumFunctionsFromFamily( IScriptable*& context, rtti::IFunctionCollector& collector ) const
{
	// initial context if required to be that of the object
	RED_ASSERT( context == this );
	return GetClass()->EnumFunctionsFromFamily( collector );
}

Bool IScriptable::FindEvent( CName functionName ) const
{
	const rtti::Function* function = NULL;
	IScriptable* context = const_cast< IScriptable* >( this );
	return rtti::FindFunction( context, functionName, function );
}

void IScriptable::funcToString( CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	FINISH_PARAMETERS;
	RETURN_STRING( GetFriendlyName().AsChar() );
}

void IScriptable::funcGetClassName( CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	FINISH_PARAMETERS;
	RETURN_NAME( GetClass()->GetName() );
}

void IScriptable::funcIsA( CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( CName, className, CName::NONE() );
	FINISH_PARAMETERS;

	const rtti::ClassType* testClass = GetRttiSystem().FindClass( className );

	RETURN_BOOL( IsA( testClass ) );
}

void IScriptable::funcIsExactlyA( CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( CName, className, CName::NONE() );
	FINISH_PARAMETERS;
	RETURN_BOOL( GetClass()->GetName() == className );
}

void IScriptable::funcDetectScriptableCycles( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	FINISH_PARAMETERS;
#ifdef DETECT_SCRIPTABLE_CYCLES
	debug::ScriptableCyclesDetector::DetectCycles();
#else
	RED_LOG( "IScriptable circular dependencies detection is not enabled." );
#endif
}

void IScriptable::OnScriptPreCaptureSnapshot()
{
}

void IScriptable::OnScriptPostCaptureSnapshot()
{
}

void IScriptable::OnScriptReloaded()
{
}

void IScriptable::ReleaseScriptPropertiesBuffer()
{
	if ( m_scriptData != nullptr )
	{
		// destroy scripted properties
		bool hasCachedProperties = false;
#if 0 // ctremblay: This doesnt work. Investigation required if we want to bring this back. 
		const auto& cachedProperties = GetLocalClass()->GetCachedScriptPropertiesToDestroy( hasCachedProperties );
		if ( hasCachedProperties )
		{
			// use much faster cached list
			for ( Uint32 i=0; i<cachedProperties.Size(); ++i )
			{
				const rtti::ClassType::PropInfo& info = cachedProperties[i];

				void* propData = red::OffsetPtr( m_scriptData, info.m_offset );
				info.m_type->Destruct( propData );
			}
		}
		else
#endif
		{
			// use default method
			const auto& allProps = GetLocalClass()->GetCachedProperties();
			for ( const rtti::Property* prop : allProps )
			{
				if ( prop->IsInSecondaryDataBuffer() )
				{
					void* propData = prop->GetOffsetPtr( this );
					prop->GetType()->Destruct( propData );
				}
			}
		}

		const rtti::ClassType * classType = GetLocalClass();
		RED_ASSERT( classType != nullptr, "Failed to get local class type" );

		// free script data buffer
		RED_FREE( classType->GetInnerTypeMemoryPool(), m_scriptData );
		m_scriptData = nullptr;
	}
}

void IScriptable::CreateScriptPropertiesBuffer()
{
	if ( m_scriptData == nullptr )
	{
		const rtti::ClassType* const classType = GetLocalClass();
		RED_ASSERT( classType != nullptr, "Failed to get local class type" );
		classType->InitializeScriptedProperties( this );
		classType->InitializeScriptDefaultValues( this );
	}
}

void IScriptable::CopyScriptPropertiesBuffer( const IScriptable& other )
{
	if ( other.m_scriptData == nullptr )
	{
		ReleaseScriptPropertiesBuffer();
	}
	else
	{
		const Uint32 scriptDataSize = m_class->GetScriptDataSize();
		const rtti::ClassType::TPropertyList& props = m_class->GetCachedProperties();

		// if script data wasn't created yet
		if ( m_scriptData == nullptr )
		{
			m_scriptData = RED_ALLOCATE_ALIGNED( m_class->GetInnerTypeMemoryPool(), scriptDataSize, m_class->GetAlignment() );
			// memzero for PODs
			red::Memzero( m_scriptData, scriptDataSize );
			for ( const rtti::Property* prop : props )
			{
				// we're interested only in properties stored in script data buffer
				if ( prop->IsInSecondaryDataBuffer() )
				{
					void* propData = prop->GetOffsetPtr( this );
					prop->GetType()->Construct( propData );
				}
			}
		}

		// we need to perform deep-copy of scripted properties
		for ( const rtti::Property* prop : props )
		{
			// we're interested only in properties stored in script data buffer
			if ( prop->IsInSecondaryDataBuffer() )
			{
				const void* srcPropData = prop->GetOffsetPtr( &other );
				void* destPropData = prop->GetOffsetPtr( this );				
				prop->GetType()->Copy( destPropData, srcPropData );
			}
		}
	}
}

void IScriptable::QueueFunctionCall( const rtti::Function* function, const rtti::Variant& parameter )
{
	RED_FATAL( "QueueFunctionCall() not implemented for '%s' class", GetClass()->GetName().AsChar() );
}

namespace Helper
{

	class CScriptableCollector : public serialization::NullMapper
	{
	private:
		red::HashMap< Uint64, Uint32 >				m_visitedObjects{ red::PoolScript() };
		red::DynArray< THandle< IScriptable > >*	m_outScriptableObjects;

	public:
		CScriptableCollector( red::DynArray< THandle< IScriptable > >& outArray )
			: m_outScriptableObjects( &outArray )
		{
			m_outScriptableObjects->Reserve( 65536 );
		}

		virtual void MapPointer( const SerializableHandle& pointer, ObjectIndex& outIndex ) override final
		{
			if ( pointer )
			{
				Uint32 visited = 0;
				const Uint64 id = pointer->GetID().Get();
				if ( !m_visitedObjects.Find( id, visited ) || !visited )
				{
					// mark as visited
					m_visitedObjects.Insert( id, 1 );
					AddSerializable( pointer );
				}
			}
		}

		void AddSerializable( const SerializableHandle& object )
		{
			// objects will be unique, we can skip the test part
			m_visitedObjects.Insert( object->GetID().Get(), 1 );

			// output scriptables
			if ( object->IsA< IScriptable >() )
			{
				m_outScriptableObjects->PushBack( red::StaticCast< IScriptable >( object ) );
			}

			// serialize the object to get more pointers
			CNullFileWriter nullFile;
			nullFile.m_mapper = this;
			nullFile.m_flags |= FF_Mapper;
			object->OnSerialize( nullFile );
		}
	};

} // helper

void IScriptable::CollectAllScriptableObjects( red::DynArray< THandle< IScriptable > >& outScriptables )
{
	Helper::CScriptableCollector collector( outScriptables );

	red::DynArray< SerializableWeakHandle > allObjects{ red::PoolScript() };
	GSerializableMap->GetAll( allObjects );

	for ( const auto& it : allObjects )
	{
		if ( auto handle = it.ToHandle() )
		{
			collector.AddSerializable( handle );
		}
	}
}

void* IScriptable::GetScriptPropertyData() 
{ 
	CreateScriptPropertiesBuffer(); 
	return m_scriptData; 
}


const void* IScriptable::GetScriptPropertyData() const 
{ 
	const_cast< IScriptable* >( this )->CreateScriptPropertiesBuffer(); 
	return m_scriptData; 
}
