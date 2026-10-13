/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "rttiClass.h"
#include "rttiValueHolder.h"
#include "rttiAccessPath.h"
#include "rttiPathParser.h"
#include "textWriter.h"
#include "textReader.h"
#include "variant.h"
#include "resourceAsyncReference.h"
#include "resource.h"
#include "mathEulerAngles.h"
#include "mathQuaternion.h"
#include "mathVector4.h"
#include "rttiArrayTypesImpl.h"
#include "rttiPointerTypesImpl.h"
#include "sharedDataBuffer.h"
#include "rttiFundamentalTypes.h"
#include "containersRTTI.h"
#include "stringRTTI.h"
#include "eventConnectorCollector.h"
#include "event.h"
#include "resourceReferenceScriptToken.h"
#include "serializableDebug.h"

#include "../../redFileSystem/include/fileSkipableBlock.h"
#include "../../redContainers/include/dynArrayAccessor.h"
#include "dataBuffer.h"

using red::DynArray;
using red::StaticArray;

namespace Config
{
	TConfigVar< Bool > cvShowAllClassProperties( "Backend", "ShowAllClassProperties", false );
	TConfigVar< Bool > cvEditAllClassProperties( "Backend", "EditAllClassProperties", false );
}


namespace rtti
{
	ClassEditablePropertyInfo::ClassEditablePropertyInfo()
		: m_type( nullptr )
		, m_customTypeMode( CustomEditorMode::NormalType )
		, m_isReadOnly( false )
		, m_isInlined( false )
		, m_minValue( 0.0f )
		, m_maxValue( 0.0f )
		, m_xMinValue( 0.0f )
		, m_xMaxValue( 0.0f )
		, m_isResizable( false )
		, m_canAdd( true )
		, m_isMask( false )
		, m_minSize( 0 )
		, m_maxSize( 0 )
		, m_canClear( true )
		, m_canReset( true )
		, m_canSelect( true )
		, m_isInstanceEditable( false )
		, m_hideSlider( false )
		, m_isBrowsable( true )
	{}

	ClassEditablePropertyInfo::~ClassEditablePropertyInfo() = default;
	ClassEditablePropertyInfo& ClassEditablePropertyInfo::operator=( const ClassEditablePropertyInfo& ) = default;
	ClassEditablePropertyInfo::ClassEditablePropertyInfo( const ClassEditablePropertyInfo & ) = default;

	const atomic::TAtomic16 c_invalidEventClassId = ~0;

	ClassType::ClassType( const CName name, Uint32 size, Uint32 flags )
		:  m_baseClass( nullptr ) 
		, m_name( name )
		, m_localProperties( red::PoolRTTI() )
		, m_propertyOverrides( red::PoolRTTI() )
		, m_localFunctions( red::PoolRTTI() )
		, m_localStaticFunctions( red::PoolRTTI() )
		, m_size( size )
		, m_alignment( 4 )
		, m_scriptSize( 0 )
		, m_flags( flags )
		, m_arePropertiesCached( false )
		, m_cachedSecondaryPropertiesToDestory( red::PoolRTTI() )
		, m_arePropertyOverridesResolved( false )
		, m_isFullyInitialized( 0 )
		, m_userData( nullptr )
		, m_defaultObject( nullptr )
		, m_cachedPropertyDictionary( red::PoolRTTI() )
		, m_cachedProperties( red::PoolRTTI() )
		, m_cachedPersistentProperties( red::PoolRTTI() )
		, m_propertiesToDestroy( red::PoolRTTI() )
		, m_cachedFunctionByName( red::PoolRTTI()  )
		, m_cachedFunctionByHash( red::PoolRTTI() )
		, m_defaultValues( red::PoolScript() )
		, m_eventConnector( red::PoolRTTI() )
		, m_eventClassId( c_invalidEventClassId )
		, m_userClassIndex( ~0 )
#ifdef USE_PROFILER
		, m_instrumentationObject( name.ToDebugString() )
#endif	
	{
		m_refName = FormatScriptedReferenceTypeName( name );
	}

	ClassType::~ClassType()
	{
	}

	const CName	ClassType::GetName() const 
	{ 
		return m_name; 
	}

	Uint32 ClassType::GetAlignment() const 
	{ 
		return m_alignment; 
	}

	Uint32 ClassType::GetSize() const 
	{ 
		return m_size; 
	}

	ERTTITypeType ClassType::GetType() const 
	{ 
		return RT_Class; 
	}

	CName ClassType::GetRefName() const
	{
		return m_refName;
	}

	Bool ClassType::ToString( const void* object, String& valueString ) const 
	{ 
		valueString += GetName().AsChar();
		valueString += "[";

		red::DynArray<const rtti::Property*> properties{ red::PoolRTTI() };
		GetProperties(properties);

		for (Uint32 i = 0; i < properties.Size(); ++i )
		{
			const rtti::Property* prop = properties[i];
			const CName propName = prop->GetName();

			const rtti::IType* propType = prop->GetType();
			const void* propData = prop->GetOffsetPtr(object);
			String propValueString;
			propType->ToString(propData, propValueString);

			String prefix = i == 0 ? "" : ",";
			red::StringBuilder<String> str;
			str.Appendf("%s %s:%s", prefix.AsChar(), propName.AsChar(), propValueString.AsChar());

			valueString += str.ToString();
		}
		valueString += " ]";
		return true; 
	}

	Bool ClassType::FromString( void* object, const String& valueString ) const 
	{ 
		return false; 
	}

	void ClassType::ReuseScriptStub( Uint32 flags )
	{
		RED_ASSERT( !m_localFunctions.Size() );
		RED_ASSERT( !m_localStaticFunctions.Size() );
		RED_ASSERT( !m_localProperties.Size() );

		m_flags = flags;
		m_scriptSize = 0;
		m_size = 0;

		m_baseClass = nullptr;
	}

	void* ClassType::CreateObject( Uint32 sizeCheck, bool wipeMemory ) const
	{
		GetRttiSystem().RegisterPendingTypes();

		RED_FATAL_ASSERT( m_isFullyInitialized, "Class '%hs' is NOT fully initialized yet we try to create objects from it", GetName().AsChar() );

		if ( IsAbstract() )
		{
			RED_LOG_ERROR( "Trying to instantiate abstract class '%hs', no object will be created", GetName().AsChar() );
			return nullptr;
		}

		RED_UNUSED( sizeCheck );
		RED_ASSERT( !sizeCheck || GetSize() >= sizeCheck, "Class '%hs' has failed size check while creating an object. Probable corrupt data", m_name.AsChar() );

        void * buffer = nullptr;

        if( const ClassType* nativeClass = GetFirstNativeBaseClass() )
        {
            const Uint32 alignment = GetAlignment();
            Uint32 bufferSize = red::memory::RoundUp( GetSize(), alignment );
            buffer = RED_ALLOCATE_ALIGNED( nativeClass->GetInnerTypeMemoryPool(), bufferSize, alignment );
        }
        	
        if( !buffer )
        {
            buffer = AllocateClassBuffer();
        }

		if( wipeMemory )
		{
			const Uint32 alignment = GetAlignment();
			Uint32 bufferSize = red::memory::RoundUp( GetSize(), alignment );
			red::Memzero( buffer, bufferSize );
		}

		OnConstruct( buffer );
		InitializeScriptedProperties( buffer );
		InitializeScriptDefaultValues( buffer );

#ifdef RED_ENABLE_SERIALIZABLE_DEBUG
		if ( IsA< ISerializable >() )
		{
			ISerializable* obj = reinterpret_cast< ISerializable* >( buffer );
			serializableDebug::Register( obj, this );
		}
#endif

		return buffer;
	}

	void ClassType::DestroyObject( void* mem ) const
	{
		// Empty memory
		if ( !mem )
			return;

		Destruct( mem );

        if( const ClassType* nativeClass = GetFirstNativeBaseClass() )
        {
            RED_FREE( nativeClass->GetInnerTypeMemoryPool(), mem );
        }
        else
        {
            RED_FREE( GetInnerTypeMemoryPool(), mem );
        }
	}

	void ClassType::Construct( void *mem ) const
	{
		OnConstruct( mem );
	}

	void ClassType::Destruct( void *mem ) const
	{	
		OnDestruct( mem );
	}

	void ClassType::DestroyProperties( void * buffer ) const
	{
		if( NeedsCleaning() )
		{
			for ( const Property* prop : m_propertiesToDestroy )
			{
				const rtti::IType * type = prop->GetType();
				void * propBuffer = prop->GetOffsetPtr( buffer );
				type->Destruct( propBuffer );
			}
		}
	}

	void ClassType::InternalSetSize( Uint32 size )
	{
		m_size = size;
	}

	Bool ClassType::DeepCompare( const void* data1, const void* data2, Uint32 flags ) const
	{
		RED_ASSERT( data1 );
		RED_ASSERT( data2 );

		// Get properties
		const TPropertyList& properties = GetCachedProperties();

		// Compare properties
		for ( Uint32 i=0; i<properties.Size(); i++ )
		{
			const rtti::Property* prop = properties[i];
			const void* propData1 = prop->GetOffsetPtr( data1 );
			const void* propData2 = prop->GetOffsetPtr( data2 );
			if ( !prop->GetType()->Compare( propData1, propData2, flags ) )
			{
				return false;
			}
		}

		// Equal
		return true;
	}

	void ClassType::SetAlignment( const Uint32 alignment )
	{
		m_alignment = Max( alignment, 4u );
	}

	void ClassType::SetDescriptiveName( const red::String& humanReadableName )
	{
#ifndef RED_CONFIGURATION_FINAL
		m_descriptiveName = humanReadableName;
#endif
	}

	const char* ClassType::GetDescriptiveName() const
	{
#ifndef RED_CONFIGURATION_FINAL
		return m_descriptiveName.Empty() ? m_name.AsChar() : m_descriptiveName.AsChar();
#else
		return m_name.AsChar();
#endif
	}

	void ClassType::SetCustomEditorName( const CName customEditorName )
	{
#ifndef RED_CONFIGURATION_FINAL 
		m_customEditorName = customEditorName;
#endif	//RED_CONFIGURATION_FINAL
	}

	CName ClassType::GetCustomEditorName() const
	{
#ifndef RED_CONFIGURATION_FINAL 
		return m_customEditorName;
#else
		return CName::NONE();
#endif	//RED_CONFIGURATION_FINAL
	}


	void ClassType::AddParentClass( const ClassType* baseClass )
	{
		RED_ASSERT( !HasBaseClass() );
		RED_ASSERT( m_scriptSize == 0 );
		RED_ASSERT( m_size == 0 );

		// Set base class
		m_baseClass = baseClass;
	}

	void ClassType::AddProperty( rtti::Property* property )
	{
		RED_ASSERT( property, "Trying to add NULL property to class '%hs'", GetName().AsChar() );
		RED_ASSERT( !FindProperty( property->GetName() ), "The property with name '%hs' already exists in class '%hs'", property->GetName().AsChar(), GetName().AsChar() );

		m_localProperties.PushBack( property );
		m_arePropertiesCached = false;
		m_areSecondaryPropertiesToDestroyCached = false;
	}

	void ClassType::AddPropertyOverride( rtti::Property* property, Uint32 overrideMask )
	{
		RED_ASSERT( property, "Trying to add NULL property override to class '%hs'", GetName().AsChar() );
		RED_ASSERT( !FindPropertyOverride( property->GetName() ), "The property override with name '%hs' already exists in class '%hs'", property->GetName().AsChar(), GetName().AsChar() );

		m_propertyOverrides.PushBack( PropOverride{ property, overrideMask } );
		m_arePropertiesCached = false;
		m_arePropertyOverridesResolved = false;
		m_areSecondaryPropertiesToDestroyCached = false;
	}

	void ClassType::AddFunction( const rtti::Function* function )
	{
		RED_ASSERT( function, "Trying to add NULL function to class '%hs'", GetName().AsChar() );
		RED_ASSERT( function->GetClass() == this, "Function '%hs' belongs to class '%hs', not class '%hs'", function->GetName().AsChar(), function->GetClass()->GetName().AsChar(), GetName().AsChar() );

		m_localFunctions.PushBack( function );
	}

	void ClassType::AddStaticFunction( const rtti::Function* function )
	{
		RED_ASSERT( function, "Trying to add NULL function to class '%hs'", GetName().AsChar() );
		RED_ASSERT( function->GetClass() == this, "Function '%hs' belongs to class '%hs', not class '%hs'", function->GetName().AsChar(), function->GetClass()->GetName().AsChar(), GetName().AsChar() );

		m_localStaticFunctions.PushBack( function );
	}

	void ClassType::AddNativeFunction( const CName funcName, TNativeFunc funcPtr, Uint32 flags )
	{
		const rtti::Function* func = RED_NEW( rtti::NativeMemberFunction )( this, funcName, funcName, funcPtr, flags );
		AddFunction( func );
	}

	void ClassType::AddNativeStaticFunction( const CName funcName, TNativeGlobalFunc funcPtr, Uint32 flags )
	{
		const rtti::Function* func = RED_NEW( rtti::NativeMemberFunction )( this, funcName, funcName, funcPtr, flags );
		AddStaticFunction( func );
	}

	void ClassType::GetProperties( TPropertyList &properties ) const
	{
		if ( HasBaseClass() )
		{
			Uint32 baseClassPropsStart = properties.Size();

			GetBaseClass()->GetProperties( properties );

			if ( HasPropertyOverrides() )
			{
				ResolvePropertyOverrides();

				Uint32 numGatheredProperties = properties.Size();

				for ( const PropOverride& propOverride : GetPropertyOverrides() )
				{
					const rtti::Property* propertyOverride = propOverride.m_propertyOverride;

					for ( Uint32 i = baseClassPropsStart; i < numGatheredProperties; ++i )
					{
						if ( properties[i]->GetName() == propertyOverride->GetName() )
						{
							properties[i] = propertyOverride;
						}
					}
				}
			}
		}

		// add local and parent properties
		properties.PushBack( m_localProperties );
	}

	void ClassType::GetPersistentProperties( TPropertyList &properties ) const
	{
		if ( HasBaseClass() )
		{
			const Uint32 baseClassPropsStart = properties.Size();
			GetBaseClass()->GetPersistentProperties( properties );

			if ( HasPropertyOverrides() )
			{
				ResolvePropertyOverrides();

				const Uint32 numGatheredProperties = properties.Size();
				for ( const PropOverride& propOverride : GetPropertyOverrides() )
				{
					const rtti::Property* propertyOverride = propOverride.m_propertyOverride;

					for ( Uint32 i = baseClassPropsStart; i < numGatheredProperties; ++i )
					{
						if ( properties[i]->GetName() == propertyOverride->GetName() )
						{
							RED_FATAL_ASSERT( propertyOverride->IsPersistent(), "Property %s overrides persistent property, but is not persistent.", propertyOverride->GetName().AsChar() );
							properties[i] = propertyOverride;
						}
					}
				}
			}
		}

		for ( const rtti::Property* prop : m_localProperties )
		{
			if ( prop->IsPersistent() )
				properties.PushBack( prop );
		}
	}

	void ClassType::CreateDefaultObject() const
	{
		m_defaultObject = CreateObject( 0, true );
	}

	void ClassType::SetFlag( Uint32 flag, Bool isOn )
	{
		if ( isOn )
		{
			m_flags |= flag;
		}
		else
		{
			m_flags &= ( ~flag );
		}
	}

	void ClassType::ClearScriptData()
	{
		m_isFullyInitialized = 0;

		// Clear cached data
		{
			red::ScopedLock< red::RWSpinLock > scopedLock( m_cachedPropertiesLock );
			m_arePropertiesCached = false;
			m_cachedProperties.Clear();
			m_cachedPropertyDictionary.Clear();
			m_propertiesToDestroy.Clear();
		}
	
		m_areSecondaryPropertiesToDestroyCached = false;
		m_cachedSecondaryPropertiesToDestory.Clear();

		// Delete properties that were created from scripts
		for ( Uint32 i = m_localProperties.Size(); i-- > 0; )
		{
			const rtti::Property* prop = m_localProperties[i];
			if ( prop->IsScripted() )
			{
				m_localProperties.RemoveAt( i );
				RED_DELETE( prop );
			}
		}

		// If we are not a native class then all properties should be deleted
		if ( !IsNative() )
		{
			RED_ASSERT( m_localProperties.Empty(), "All local properties should be deleted from a non-native class" );
		}

		// Delete functions that were created from scripts
		for ( Uint32 i = m_localFunctions.Size(); i-- > 0; )
		{
			const rtti::Function* func = m_localFunctions[i];
			if ( !func->IsNative() )
			{
				m_localFunctions.RemoveAt( i );
				RED_DELETE( func );
			}
		}

		// Delete static functions that were created from scripts
		for ( Uint32 i = m_localStaticFunctions.Size(); i-- > 0; )
		{
			const rtti::Function* func = m_localStaticFunctions[ i ];
			if ( !func->IsNative() )
			{
				m_localStaticFunctions.RemoveAt( i );
				RED_DELETE( func );
			}
		}
	}

	const rtti::Property* ClassType::FindProperty( const CName name ) const
	{
		auto iter = m_cachedPropertyDictionary.Find( name );
		if( iter != m_cachedPropertyDictionary.End() )
		{
			return iter.Value();
		}

		for ( Uint32 i=0; i<m_localProperties.Size(); ++i )
		{
			if ( m_localProperties[i]->GetName() == name )
			{
				return m_localProperties[i];
			}
		}

		if ( HasBaseClass() )
		{
			const rtti::Property* property = GetBaseClass()->FindProperty( name );
			if ( property )
			{
				if ( HasPropertyOverrides() )              // This class might have override of the base class property.
				{
					ResolvePropertyOverrides();

					for ( Uint32 j = 0; j < m_propertyOverrides.Size(); ++j )
					{
						if ( m_propertyOverrides[j].m_propertyOverride->GetName() == name )
						{
							return m_propertyOverrides[j].m_propertyOverride;
						}
					}
				}

				return property;
			}
		}

		return nullptr;
	}

	const rtti::Property* ClassType::FindPropertyOverride( const CName name ) const
	{
		for ( const PropOverride& propOverride : m_propertyOverrides )
		{
			if ( propOverride.m_propertyOverride->GetName() == name )
			{
				return propOverride.m_propertyOverride;
			}
		}

		return nullptr;
	}

	const rtti::Function* ClassType::FindFunction( const CName name ) const
	{
		auto iter = m_cachedFunctionByName.Find( name );
		if( iter != m_cachedFunctionByName.End() )
		{
			const rtti::Function* func = iter.Value();
			return func;
		}

		if( HasBaseClass() )
		{
			const rtti::Function* func = GetBaseClass()->FindFunction( name );	
		
			if( func )
			{
				return func;
			}
		}

#ifndef RED_CONFIGURATION_FINAL
		return FindFunctionNonCached( name );
#else
		return nullptr;
#endif
	}

	const rtti::Function* ClassType::FindLocalFunction( const CName name ) const
	{
		// Find local function
		for ( auto* func : m_localFunctions )
		{
			if ( func->GetName() == name )
				return func;
		}

		// Find local static function
		for ( auto* func : m_localStaticFunctions )
		{
			if ( func->GetName() == name )
				return func;
		}

		return nullptr;
	}

	const rtti::Function* ClassType::FindFunctionNonCached( const CName name ) const
	{
		const rtti::Function* function = FindLocalFunction( name );

		if ( function )
			return function;

		if ( HasBaseClass() )
		{
			function = GetBaseClass()->FindFunctionNonCached( name );
			if ( function )
				return function;
		}

		return nullptr;
	}

	const rtti::Function* ClassType::FindFunctionByHash( Uint64 hash ) const
	{
		auto iter = m_cachedFunctionByHash.Find( hash );
		if( iter != m_cachedFunctionByHash.End() )
		{
			const rtti::Function* func = iter.Value();
			return func;
		}

		if( HasBaseClass() )
		{
			return GetBaseClass()->FindFunctionByHash( hash );	
		}

		return nullptr;
	}

	const rtti::Function* ClassType::FindFunctionByFamilyName( const CName familyName ) const
	{
		const rtti::ClassType* curr = this;
		while ( curr )
		{
			for ( auto* func : curr->m_localFunctions )
			{
				if ( func->GetFamilyName() == familyName )
					return func;
			}

			for ( auto* func : curr->m_localStaticFunctions )
			{
				if ( func->GetFamilyName() == familyName )
					return func;
			}

			curr = curr->GetBaseClass();
		}
		return nullptr;
	}

	void ClassType::EnumFunctionsFromFamily( rtti::IFunctionCollector& collector ) const
	{
		// Find local function
		for ( auto* func : m_localFunctions )
		{
			if ( !collector( func ) )
			{
				return;
			}
		}

		if ( HasBaseClass() )
		{
			GetBaseClass()->EnumFunctionsFromFamily( collector );
		}
	}

	const Bool ClassType::ReadDirectValue( IRTTIContext& ctx, const void* data, rtti::ValuePtr& outValue ) const
	{
		outValue = rtti::ValueHolder::CreateStructure();

		const auto& props = GetCachedProperties();
		for ( const auto& prop : props )
		{
			const auto propName = prop->GetName();
			const auto* propType = prop->GetType();
			const void* propData = prop->GetOffsetPtr( data );

			rtti::ValuePtr propValue;
			if ( !propType->ReadValue( ctx, propData, rtti::AccessPath(), propValue ) )
			{
				ctx.ReportError( this, "Unable to retrieve value for property '%hs'", propName.AsChar() );
			}
			else
			{
				outValue->SetStructElement( propName, propValue );
			}
		}

		return true;
	}

	const Bool ClassType::WriteDirectValue( IRTTIContext& ctx, void* data, const rtti::ValueHolder& value, bool clone ) const
	{
		if ( value.IsEmpty() )
		{
			Construct( data );
			return true;
		}

		// the value stream must contain a structure at this point
		if ( !value.IsStructure() )
		{
			ctx.ReportError( this, "Expected structure value" );
			return false;
		}

		if ( value.GetNumElements() == 0 )
		{
			Construct( data );
			return true;
		}

		// resolve properties FIRST - before applying any value
		const Uint32 count = value.GetNumElements();
		StaticArray< const rtti::Property*, 256 > properties;
		for ( Uint32 i=0; i<count; ++i )
		{
			const CName propName = value.GetStructElementName( i );

			const auto* prop = FindProperty( propName );
			if ( prop == nullptr && propName != RED_NAME_CONSTEXPR_NOREG( "class" ) ) // TODO: remove this hack
			{
				ctx.ReportError( this, "Property '%hs' not found", propName.AsChar() );
				return false; // this is a fatal error - we are trying to set property that does not exist
			}

			properties.PushBack( prop );
		}

		// copy specified values to properties
		for ( Uint32 i=0; i<count; ++i )
		{
			const CName propName = value.GetStructElementName( i );
			const auto& propValue = value.GetStructElement( i );

			const auto* prop = properties[i];
			if ( prop != nullptr )
			{
				const auto* propType = prop->GetType();
				void* propData = prop->GetOffsetPtr( data );
				if ( !propType->WriteValue( ctx, propData, rtti::AccessPath(), *propValue, clone ) )
				{
					ctx.ReportError( this, "Unable to write value for property '%hs'", propName.AsChar() );
				}
			}
		}

		// restored
		return true;
	}

	const Bool ClassType::ReadValue( IRTTIContext& ctx, const void* data, const rtti::AccessPath& path, rtti::ValuePtr& outValue ) const
	{
		// we want the whole value
		if ( path.IsEmpty() )
			return ReadDirectValue( ctx, data, outValue );

		// parse the property name
		rtti::PathParser parser( path );
		CName propName;
		if ( !parser.EatName( propName ) )
		{
			ctx.ReportError( this, "Expected property name in path" );
			return false;
		}

		// find property
		const auto* prop = FindProperty( propName );
		if ( !prop )
		{
			ctx.ReportError( this, "Property '%hs' not found", propName.AsChar() );
			return false;
		}

		// recurse
		const void* propData = prop->GetOffsetPtr( data );
		return prop->GetType()->ReadValue( ctx, propData, parser.GetUneatenPath(), outValue );
	}

	const Bool ClassType::WriteValue( IRTTIContext& ctx, void* data, const rtti::AccessPath& path, const rtti::ValueHolder& newValue, bool clone ) const
	{
		// we want the whole value
		if ( path.IsEmpty() )
			return WriteDirectValue( ctx, data, newValue, clone );

		// parse the property name
		rtti::PathParser parser( path );
		CName propName;
		if ( !parser.EatName( propName ) )
		{
			if ( propName != RED_NAME_CONSTEXPR_NOREG( "class" ) )
			{
				ctx.ReportError( this, "Expected property name in path" );
			}

			return false;
		}

		// find property
		const auto* prop = FindProperty( propName );
		if ( !prop )
		{
			ctx.ReportError( this, "Property '%hs' not found", propName.AsChar() );
			return false;
		}

		// recurse
		void* propData = prop->GetOffsetPtr( data );
		return prop->GetType()->WriteValue( ctx, propData, parser.GetUneatenPath(), newValue, clone );
	}

	Bool ClassType::IsPropertyReadOnly( IRTTIContext& ctx, const rtti::AccessPath& path, Bool& outReadOnly ) const
	{
		if ( path.IsEmpty() )
			return false;

		rtti::PathParser parser( path );
		CName propName;
		if ( !parser.EatName( propName ) )
		{
			return ctx.ReportError( this, "Expected property name in path" );
		}

		const auto* prop = FindProperty( propName );
		if ( !prop )
		{
			return ctx.ReportError( this, "Property '%hs' not found", propName.AsChar() );
		}

		if ( prop->IsReadOnly() )
		{
			outReadOnly = prop->IsReadOnly();
			return true;
		}

		if ( parser )
		{
			return prop->GetType()->IsPropertyReadOnly( ctx, parser.GetUneatenPath(), outReadOnly );
		}

		outReadOnly = prop->IsReadOnly();
		return true;
	}

	Bool ClassType::Serialize( IFile& file, void* data, ISerializable* owner ) const
	{
		return DefaultSerialize( file, data, owner );
	}

	const Bool ClassType::SerializeToText( text::ITextWriter& writer, const void* data ) const
	{
		return DefaultSerializeToText( writer, data );
	}

	const Bool ClassType::SerializeFromText( text::ITextReader& reader, void* data ) const
	{
		return DefaultSerializeFromText( reader, data );
	}

	Bool ClassType::DefaultSerialize( IFile& file, void* data, ISerializable * owner ) const
	{
		if ( IsSerializable() )
		{
			const void* defaultObjectData = GetDefaultObject();
			return SerializeDiff( owner, file, data, defaultObjectData, const_cast<ClassType*>( this ) );
		}
		else
		{
			return SerializeDiff( owner, file, data, IsDefaultObjectSerialization() ? GetDefaultObject() : nullptr, const_cast<ClassType*>( this ) );
		}
	}

	const Bool ClassType::DefaultSerializeToText( text::ITextWriter& writer, const void* data ) const
	{
		const void* defaultObjectData = IsDefaultObjectSerialization() ? GetDefaultObject() : nullptr;
		const void* objectData = IsSerializable() ? data : defaultObjectData;

		writer.BeginObject(this, data);

		// Grab all properties
		const auto& allProperties = GetCachedProperties();
		for ( const auto* prop : allProperties )
		{
			// Skip not serializable properties
			if ( !prop->IsSerializable() )
				continue;

			// Property block
			{
				const auto* propType = prop->GetType();
				const void* propData = prop->GetOffsetPtr( data );

				// skip default values
				if ( defaultObjectData )
				{
					const void* defaultPropData = prop->GetOffsetPtr( defaultObjectData );
					if ( propType->Compare( propData, defaultPropData, 0 ) )
					{
						continue;
					}
				}

				// Save value
				{
					writer.BeginProperty( prop->GetName() );

					if ( !prop->GetType()->SerializeToText( writer, propData ) )
						return false;

					writer.EndProperty();
				}
			}
		}

		writer.EndObject();

		// saved
		return true;
	}

	const Bool ClassType::DefaultSerializeFromText( text::ITextReader& reader, void* data ) const
	{
		if (reader.BeginObject())
		{
			while (1)
			{
				// get property
				CName propertyName;
				if (!reader.BeginProperty(propertyName))
					break;

				// find property by this name in the class
				//RED_LOG("Core: PRopName: '%hs'", propertyName.AsChar());
				const auto* prop = FindProperty(propertyName);
				if (prop != nullptr)
				{
					const auto* propType = prop->GetType();
					void* propData = prop->GetOffsetPtr(data);
					if (!prop->GetType()->SerializeFromText(reader, propData))
						return false;

				}

				// done with property
				reader.EndProperty();
			}

			reader.EndObject();
		}

		// loaded
		return true;
	}

	Uint32 ClassType::CalcScriptClassAlignment() const
	{
		Uint32 alignment = m_alignment; // default

		if ( const ClassType* nativeBaseClass = GetFirstNativeBaseClass() )
		{
			alignment = Max< Uint32 >( nativeBaseClass->GetAlignment(), alignment );
		}

		DynArray< const rtti::Property* > allProps{ red::PoolRTTI() };
		GetProperties( allProps );

		// Make sure data in scripted classes is aligned as well
		for ( Uint32 i=0; i<allProps.Size(); ++i )
		{
			const rtti::Property* prop = allProps[i];
			Uint32 propAlignment = prop->GetType()->GetAlignment();			
			alignment = Max< Uint32 >( propAlignment, alignment );
		}

		// Return calculated alignment
		return alignment;
	}

	void ClassType::InitializeScriptedProperties( void* buffer ) const
	{
		// All scripted classes except for structs must derive from IScriptable.
		const Bool isScriptedStruct = IsScriptedStruct();

		if ( !isScriptedStruct && !IsA< IScriptable >() )
		{
			return;
		}

		if ( isScriptedStruct )
		{
			red::Memzero( buffer, GetSize() );
		}
		else
		{
			IScriptable* scriptable = reinterpret_cast< IScriptable* >( buffer );
			const Uint32 scriptDataSize = GetScriptDataSize();
			void* scriptPropertyData = RED_ALLOCATE_ALIGNED( GetInnerTypeMemoryPool(), scriptDataSize, GetAlignment() );
			red::Memzero( scriptPropertyData, scriptDataSize );		
			scriptable->BindLocalClassType( this, scriptPropertyData );
		}

		const TPropertyList& properties = GetCachedProperties();
		for ( const rtti::Property* prop : properties )
		{
			if ( prop->IsScripted() )
			{
				void* propData = prop->GetOffsetPtr( buffer );
				const IType* type = prop->GetType();
				type->Construct( propData );

				if( type->GetType() == RT_Array )
				{
					red::DynArrayAccessor & accessor = red::DynArrayAccessor::GetRef( propData );
					accessor.SetPool( red::PoolScript() );
				}
			}
		}
	}

	Bool ClassType::AddDefaultValue( const CName propertyName, const red::String& value )
	{
		// overwrite existing default value (e.g. scripts reload)
		TDefaultValues::iterator it = m_defaultValues.Find( propertyName );
		if ( it != m_defaultValues.End() )
		{
			return it.Value()->ValueFromString( value );
		}

		// create new default value
		const CName realPropertyName = rtti::GetFilteredPropertyName( propertyName );
		const Property* prop = FindProperty( realPropertyName );
		if ( prop == nullptr )
		{
			return false;
		}
		Variant* var = RED_NEW( Variant )( prop->GetType(), nullptr );
		if ( !var->ValueFromString( value ) )
		{
			RED_DELETE( var );
			return false;
		}
		return m_defaultValues.Insert( realPropertyName, var ).IsSuccessful();
	}

	void ClassType::InitializeScriptDefaultValues( void* buffer ) const
	{
		if ( const ClassType* baseClass = GetBaseClass() )
		{
			baseClass->InitializeScriptDefaultValues( buffer );
		}

		if ( m_defaultValues.Empty() )
		{
			return;
		}

		// Assign default values to scripted properties
		for ( const auto& it : m_defaultValues )
		{
			const Property* prop = FindProperty( it.Key() );
			if ( prop != nullptr && prop->IsScripted() )
			{
				const IType* propType = prop->GetType();
				void* propData = prop->GetOffsetPtr( buffer );
				propType->Copy( propData, it.Value()->GetData() );
			}
		}
	}

	void ClassType::RecalculateClassDataSize()
	{
		// The script properties
		DynArray< const rtti::Property* > localScriptProperties{ red::PoolRTTI() };
		for ( Uint32 i=0; i<m_localProperties.Size(); i++ )
		{
			const rtti::Property* prop = m_localProperties[i];
			if ( prop && prop->IsInSecondaryDataBuffer() )
			{
				localScriptProperties.PushBack( prop );
			}
		}

		// Base offset from base class
		Uint32 baseOffset = 0;
		if ( HasBaseClass() )
		{
			const ClassType* baseClass = GetBaseClass();
			baseOffset = baseClass->GetScriptDataSize();

			// Update native data size from the base class
			if ( 0 == m_size )
			{
				RED_ASSERT( !IsNative() );
				ClassType* editableBaseClass = const_cast< ClassType* >( baseClass );
				editableBaseClass->RecalculateClassDataSize();
				m_size = editableBaseClass->GetSize();
			}

			// Calculate required initial alignment
			const Uint32 classAlignment = CalcScriptClassAlignment();
			SetAlignment( Max( GetAlignment(), classAlignment ) );
			m_scriptSize = rtti::Property::CalcDataLayout( localScriptProperties, baseOffset, classAlignment );
		}

		RED_ASSERT( m_size > 0 );
	}

	Bool ClassType::ValidateLayout() const
	{
		Bool result = true;

		DynArray< const rtti::Property* > properties{ red::PoolBackend() };
		GetProperties( properties );

		for ( Uint32 i=0; i<properties.Size(); ++i )
		{
			const rtti::Property* prop = properties[i];

			const Uint32 dataStart = prop->GetDataOffset();
			const Uint32 dataEnd = dataStart + prop->GetType()->GetSize();

			// check limits
			if ( prop->IsInSecondaryDataBuffer() && dataEnd > GetScriptDataSize() )
			{
				RED_LOG_WARNING( "Core: Property '%hs' in '%hs' is out bounds (script data)", 
					prop->GetName().AsChar(),
					GetName().AsChar() );

				result = false;
			}
			else if ( !prop->IsInSecondaryDataBuffer() && dataEnd > GetSize() )
			{
				RED_LOG_WARNING( "Core: Property '%hs' in '%hs' is out bounds (native data)", 
					prop->GetName().AsChar(),
					GetName().AsChar() );

				result = false;
			}

			// check overlap with class properties
			for ( Uint32 j=0; j<properties.Size(); ++j )
			{
				const rtti::Property* otherProp = properties[j];
				if ( (otherProp != prop) && otherProp->IsInSecondaryDataBuffer() == prop->IsInSecondaryDataBuffer() )
				{
					const Uint32 otherStart = otherProp->GetDataOffset();
					const Uint32 otherEnd = otherStart + otherProp->GetType()->GetSize();

					if ( !(dataEnd <= otherStart || dataStart >= otherEnd) )
					{
						RED_LOG_WARNING( "Core: Property '%hs' in '%hs' overlaps property '%hs'", 
							prop->GetName().AsChar(),
							GetName().AsChar(),
							otherProp->GetName().AsChar());
						result = false;
					}
				}
			}
		}

		return result;
	}

	// ResourceReference -> Handle
	static Bool ConvertResRefToHandleTypes( rtti::Variant& value, const rtti::Property* prop, void* data )
	{
		if ( value.GetRTTIType()->GetType() == RT_ResourceReference && prop->GetType()->GetType() == RT_Handle ) 
		{
			res::ResourceReference* src = reinterpret_cast< res::ResourceReference* >( prop->GetOffsetPtr( data ) );

			if ( src && src->GetPath().IsValid() )
			{
				value.SetValue< SerializableHandle >( src->Get() );
			}
			else
			{
				value.SetValue< SerializableHandle >( SerializableHandle() );
			}

			return true;
		}

		// Not converted
		return false;
	}

	// Handle -> ResourceReference
	static Bool ConvertHandleToResRefTypes( rtti::Variant& value, const rtti::Property* prop, void* data )
	{
		if ( value.GetRTTIType()->GetType() == RT_Handle && prop->GetType()->GetType() == RT_ResourceReference ) 
		{
			const SerializableHandle* src = reinterpret_cast< const SerializableHandle* >( value.GetData() );
			res::ResourceReference* dst = reinterpret_cast< res::ResourceReference* >( prop->GetOffsetPtr( data ) );

			if ( src && src->Get() )
			{
				THandle< CResource > res = Cast< CResource >( *src );
				*dst = res::ResourceReference( res->GetPath() );
			}
			else
			{
				*dst = res::ResourceReference();
			}

			return true;
		}

		// Not converted
		return false;
	}

	// Handle -> ResourceReference
	static Bool ConvertHandleToAsyncResRefTypes( rtti::Variant& value, const rtti::Property* prop, void* data )
	{
		if ( value.GetRTTIType()->GetType() == RT_Handle && prop->GetType()->GetType() == RT_ResourceAsyncReference ) 
		{
			const SerializableHandle* src = reinterpret_cast< const SerializableHandle* >( value.GetData() );
			res::ResourceAsyncReference* dst = reinterpret_cast< res::ResourceAsyncReference* >( prop->GetOffsetPtr( data ) );

			if ( src && src->Get() )
			{
				THandle< CResource > res = Cast< CResource >( *src );
				if ( res )
				{
					*dst = res::ResourceAsyncReference( res->GetPath() );
				}
				else
				{
					*dst = res::ResourceAsyncReference();
				}
			}
			else
			{
				*dst = res::ResourceAsyncReference();
			}

			return true;
		}

		// Not converted
		return false;
	}

	// ResourceAsyncReference -> Handle 
	static Bool ConvertResAsyncRefToHandleTypes( rtti::Variant& value, const rtti::Property* prop, void* data )
	{
		if ( value.GetRTTIType()->GetType() == RT_ResourceAsyncReference && prop->GetType()->GetType() == RT_Handle ) 
		{
			const res::ResourceAsyncReference* src = reinterpret_cast< const res::ResourceAsyncReference* >( value.GetData() );
			SerializableHandle* dst = reinterpret_cast< SerializableHandle* >( prop->GetOffsetPtr( data ) );

			if ( src && src->GetPath().IsValid() )
			{
				// ensure resource is loaded
				*dst = SerializableHandle( src->IssueLoadingRequest()->WaitUntilLoaded() );
			}
			else
			{
				*dst = SerializableHandle();
			}

			return true;
		}

		// Not converted
		return false;
	}

	// ResourceAsyncReference -> String 
	static Bool ConvertResAsyncRefToStringType( rtti::Variant& value, const rtti::Property* prop, void* data )
	{
		if ( value.GetRTTIType()->GetType() == RT_ResourceAsyncReference && prop->GetType() == GetTypeObject< String >() )
		{
			const res::ResourceAsyncReference* src = reinterpret_cast<const res::ResourceAsyncReference*>( value.GetData() );
			String* dst = reinterpret_cast<String*>( prop->GetOffsetPtr( data ) );

			if ( src && src->GetPath().IsValid() )
			{
				*dst = src->GetPath().ToString();
			}
			else
			{
				*dst = String();
			}

			return true;
		}

		// Not converted
		return false;
	}

	// Handle -> ResourceAsyncReference
	static Bool ConvertHandleToResAsyncRefTypes( rtti::Variant& value, const rtti::Property* prop, void* data )
	{
		if ( value.GetRTTIType()->GetType() ==  RT_Handle && prop->GetType()->GetType() == RT_ResourceAsyncReference ) 
		{
			const SerializableHandle* src = reinterpret_cast< const SerializableHandle* >( value.GetData() );
			res::ResourceAsyncReference* dst = reinterpret_cast< res::ResourceAsyncReference* >( prop->GetOffsetPtr( data ) );

			if ( src && src->Get() )
			{
				THandle< CResource > res = Cast< CResource >( *src );
				*dst = res::ResourceAsyncReference( res->GetPath() );
			}
			else
			{
				*dst = res::ResourceAsyncReference();
			}

			return true;
		}

		// Not converted
		return false;
	}

	// ResourceReference -> ResourceAsyncReference
	static Bool ConvertResRefToResAsyncRef( rtti::Variant& value, const rtti::Property* prop, void* data )
	{
		if ( value.GetRTTIType()->GetType() ==  RT_ResourceReference && prop->GetType()->GetType() == RT_ResourceAsyncReference ) 
		{
			const res::ResourceReference* src = reinterpret_cast< const res::ResourceReference* >( value.GetData() );
			res::ResourceAsyncReference* dst = reinterpret_cast< res::ResourceAsyncReference* >( prop->GetOffsetPtr( data ) );

			if ( src && src->GetPath().IsValid() )
			{
				*dst = res::ResourceAsyncReference( src->GetPath() );
			}
			else
			{
				*dst = res::ResourceAsyncReference();
			}

			return true;
		}

		// Not converted
		return false;
	}

	// ResourceAsyncReference -> ResourceReference
	static Bool ConvertResAsyncRefToResRefTypes( rtti::Variant& value, const rtti::Property* prop, void* data )
	{
		if ( value.GetRTTIType()->GetType() ==  RT_ResourceAsyncReference && prop->GetType()->GetType() == RT_ResourceReference ) 
		{
			const res::ResourceAsyncReference* src = reinterpret_cast< const res::ResourceAsyncReference* >( value.GetData() );
			res::ResourceReference* dst = reinterpret_cast< res::ResourceReference* >( prop->GetOffsetPtr( data ) );

			if ( src && src->GetPath().IsValid() )
			{
				auto loadingToken = src->IssueLoadingRequest();
				
				*dst = res::ResourceReference( loadingToken );
			}
			else
			{
				*dst = res::ResourceReference();
			}

			return true;
		}

		// Not converted
		return false;
	}

	static Bool ConvertUint64ToCDateTimeTypes( rtti::Variant& value, const rtti::Property* prop, void* data )
	{
		if
			(
			value.GetRTTIType()->GetType() == RT_Fundamental &&
			prop->GetType()->GetType() == RT_Simple &&

			value.GetRTTIType()->GetName() == ::GetTypeName< Uint64 >() &&
			prop->GetType()->GetName() == ::GetTypeName< CDateTime >()
			)
		{
			CDateTime* dest = reinterpret_cast< CDateTime* >( prop->GetOffsetPtr( data ) );
			const Uint64* source = reinterpret_cast< const Uint64* >( value.GetData() );

			dest->ImportFromOldFileTimeFormat( *source );

			//RED_LOG_SPAM( "RTTI: Converted %hs::%hs (%p) from %ull to %ull", prop->GetParent()->GetName().AsChar(), prop->GetName().AsChar(), dest, *source, dest->GetRaw() );

			return true;
		}

		// Not converted
		return false;
	}

	template< typename SrcType, typename DestType >
	static Bool ConvertIntToInt( rtti::Variant& value, const rtti::Property* prop, void* data )
	{
		static_assert( std::is_integral< SrcType >::value && std::is_integral< DestType >::value, "Only integer types are supported." );

		if ( value.GetRTTIType()->GetName() == ::GetTypeName< SrcType >() && prop->GetType()->GetName() == ::GetTypeName< DestType >() )
		{
			DestType* dest = reinterpret_cast<DestType*>( prop->GetOffsetPtr( data ) );
			const SrcType* source = reinterpret_cast<const SrcType*>( value.GetData() );
			if ( *source >= std::numeric_limits<DestType>::min() && *source <= std::numeric_limits<DestType>::max() )
			{
				*dest = (DestType)*source;
				return true;
			}
		}
		return false;
	}

	static Bool ConvertIntToIntTypes( rtti::Variant& value, const rtti::Property* prop, void* data )
	{
		if ( value.GetRTTIType()->GetType() == RT_Fundamental && prop->GetType()->GetType() == RT_Fundamental )
		{
			if ( ConvertIntToInt< Uint8, Uint16 >( value, prop, data ) )
				return true;
			else if ( ConvertIntToInt< Uint8, Uint32 >( value, prop, data ) )
				return true;
			else if ( ConvertIntToInt< Uint8, Uint64 >( value, prop, data ) )
				return true;
			else if ( ConvertIntToInt< Uint16, Uint32 >( value, prop, data ) )
				return true;
			else if ( ConvertIntToInt< Uint16, Uint64 >( value, prop, data ) )
				return true;
			else if ( ConvertIntToInt< Uint32, Uint64 >( value, prop, data ) )
				return true;

			else if ( ConvertIntToInt< Uint16, Uint8 >( value, prop, data ) )
				return true;
			else if ( ConvertIntToInt< Uint32, Uint8 >( value, prop, data ) )
				return true;
			else if ( ConvertIntToInt< Uint64, Uint8 >( value, prop, data ) )
				return true;
			else if ( ConvertIntToInt< Uint32, Uint16 >( value, prop, data ) )
				return true;
			else if ( ConvertIntToInt< Uint64, Uint16 >( value, prop, data ) )
				return true;
			else if ( ConvertIntToInt< Uint64, Uint32 >( value, prop, data ) )
				return true;

			else if ( ConvertIntToInt< Int8, Int16 >( value, prop, data ) )
				return true;
			else if ( ConvertIntToInt< Int8, Int32 >( value, prop, data ) )
				return true;
			else if ( ConvertIntToInt< Int8, Int64 >( value, prop, data ) )
				return true;
			else if ( ConvertIntToInt< Int16, Int32 >( value, prop, data ) )
				return true;
			else if ( ConvertIntToInt< Int16, Int64 >( value, prop, data ) )
				return true;
			else if ( ConvertIntToInt< Int32, Int64 >( value, prop, data ) )
				return true;

			else if ( ConvertIntToInt< Int16, Int8 >( value, prop, data ) )
				return true;
			else if ( ConvertIntToInt< Int32, Int8 >( value, prop, data ) )
				return true;
			else if ( ConvertIntToInt< Int64, Int8 >( value, prop, data ) )
				return true;
			else if ( ConvertIntToInt< Int32, Int16 >( value, prop, data ) )
				return true;
			else if ( ConvertIntToInt< Int64, Int16 >( value, prop, data ) )
				return true;
			else if ( ConvertIntToInt< Int64, Int32 >( value, prop, data ) )
				return true;
		}

		// Not converted
		return false;
	}

	static Bool ConvertEnumToIntTypes( rtti::Variant& value, const rtti::Property* prop, void* data )
	{
		if ( ( value.GetRTTIType()->GetType() == RT_Enum && prop->GetType()->GetName() == ::GetTypeName< Int32 >() )
			|| ( value.GetTypeName() == ::GetTypeName< Int32 >() && prop->GetType()->GetType() == RT_Enum ) )
		{
			const Int32* src  = reinterpret_cast< const Int32* >( value.GetData() );
			Int32* dest = reinterpret_cast< Int32* >( prop->GetOffsetPtr( data ) );

			*dest = *src;

			return true;
		}

		// Not converted
		return false;
	}

	static Bool ConvertStringToCnameTypes( rtti::Variant& value, const rtti::Property* prop, void* data )
	{
		// Convert from strings to cnames automatically
		if ( value.GetTypeName() == ::GetTypeName< String >() && prop->GetType()->GetName() == ::GetTypeName< CName >() )
		{
			CName cnameValue = RED_NAME( value.ValueToString() );
			CName* cname = reinterpret_cast<CName*>( prop->GetOffsetPtr( data ) );
			if ( cname )
			{
				*cname = cnameValue;
			}

			return true;
		}

		if ( value.GetTypeName() == ::GetTypeName< CName >() && prop->GetType()->GetName() == ::GetTypeName< String >() )
		{
			String stringValue( value.ValueToString() );
			String* propertyString = reinterpret_cast<String*>( prop->GetOffsetPtr( data ) );
			if ( propertyString )
			{
				*propertyString = stringValue;
			}

			return true;
		}

		// Not converted
		return false;
	}

	// dUd: todo if needed
	static Bool ConvertStringToStringAnsiTypes( rtti::Variant& value, const rtti::Property* prop, void* data )
	{
		if ( value.GetTypeName() == ::GetTypeName< String >() && prop->GetType()->GetName() == ::GetTypeName< String >() )
		{
			return false;
		}

		if ( value.GetTypeName() == ::GetTypeName< String >() && prop->GetType()->GetName() == ::GetTypeName< String >() )
		{
			return false;
		}

		// Not converted
		return false;
	}

	static Bool ConvertDataBufferToDeferredDataBuffer(rtti::Variant& value, const rtti::Property* prop, void* data)
	{
		if (value.GetTypeName() == ::GetTypeName< DataBuffer >() && prop->GetType()->GetName() == ::GetTypeName<serialization::DeferredDataBuffer>())
		{	
			const auto* src = reinterpret_cast< const DataBuffer* >( value.GetData() );
			auto* dest = reinterpret_cast< serialization::DeferredDataBuffer* >( prop->GetOffsetPtr( data ) );
			RED_FATAL_ASSERT( src );
			RED_FATAL_ASSERT( dest );

			// #todo: should remove after texture resave
			dest->Convert_SetContentDirect( *src );
			return true;
		}
		return false;
	}

	static Bool ConvertByteArrayToDataBuffer(rtti::Variant& value, const rtti::Property* prop, void* data)
	{
		if (value.GetTypeName() == ::GetTypeName< red::DynArray<Uint8>>() && prop->GetType()->GetName() == ::GetTypeName<DataBuffer>())
		{
			const auto* src = reinterpret_cast<const red::DynArray<Uint8>*>(value.GetData());
			auto* dest = reinterpret_cast<DataBuffer*>(prop->GetOffsetPtr(data));
			RED_FATAL_ASSERT(src);
			RED_FATAL_ASSERT(dest);

			*dest = DataBuffer::Copy(src->Data(), src->DataSize());

			return true;
		}
		return false;
	}

	static Bool ConvertEulerAnglesToQuaternion(rtti::Variant& value, const rtti::Property* prop, void* data)
	{
		if (value.GetTypeName() == ::GetTypeName< EulerAngles >() && prop->GetType()->GetName() == ::GetTypeName<Quaternion>())
		{
			const auto* src = reinterpret_cast<const EulerAngles*>(value.GetData());
			auto* dest = reinterpret_cast<Quaternion*>(prop->GetOffsetPtr(data));

			*dest = src->ToQuat();
			return true;
		}

		if (value.GetTypeName() == ::GetTypeName<Quaternion>() && prop->GetType()->GetName() == ::GetTypeName<EulerAngles>())
		{
			const auto* src = reinterpret_cast<const Quaternion*>(value.GetData());
			auto* dest = reinterpret_cast<EulerAngles*>(prop->GetOffsetPtr(data));

			*dest = src->ToEulerAngles();
			return true;
		}

		// Not converted
		return false;
	}

	static Bool ConvertVector3ToVector4(rtti::Variant& value, const rtti::Property* prop, void* data)
	{
		if (value.GetTypeName() == ::GetTypeName< Vector3 >() && prop->GetType()->GetName() == ::GetTypeName<Vector4>())
		{
			const auto* src = reinterpret_cast<const Vector3*>(value.GetData());
			auto* dest = reinterpret_cast<Vector4*>(prop->GetOffsetPtr(data));

			*dest = *src;
			return true;
		}
		else if (value.GetTypeName() == ::GetTypeName<Vector4>() && prop->GetType()->GetName() == ::GetTypeName<Vector3>())
		{
			const auto* src = reinterpret_cast<const Vector4*>(value.GetData());
			auto* dest = reinterpret_cast<Vector3*>(prop->GetOffsetPtr(data));

			dest->Set( src->X, src->Y, src->Z );
			return true;
		}

		return false;
	}

	static Bool ConvertArrayStringsToCNamesTypes( rtti::Variant& value, const rtti::Property* prop, void* data )
	{
		if ( value.GetTypeName() == ::GetTypeName< DynArray< String > >()
			&& prop->GetType()->GetName() == ::GetTypeName< DynArray< CName > > () )
		{
			DynArray< String > strings{ red::PoolRTTI() };
			value.Get( strings );

			DynArray< CName >* names = reinterpret_cast< DynArray< CName >* >( prop->GetOffsetPtr( data ) );
			if ( names != nullptr )
			{
				for( Uint32 i = 0; i < strings.Size(); ++i )
				{
					names->PushBack( RED_NAME( strings[ i ] ) );
				}
			}

			return true;
		}

		if ( value.GetTypeName() == ::GetTypeName< DynArray< CName > >()
			&& prop->GetType()->GetName() == ::GetTypeName< DynArray< String > > () )
		{
			DynArray< CName > names{ red::PoolRTTI() };
			value.Get( names );

			DynArray< String >* strings = reinterpret_cast< DynArray< String >* >( prop->GetOffsetPtr( data ) );
			if ( strings != nullptr )
			{
				for( Uint32 i = 0; i < names.Size(); ++i )
				{
					strings->PushBack( names[ i ].AsChar() );
				}
			}

			return true;
		}

		// Not converted
		return false;
	};

	static Bool ConvertArrayUint32ToUint64Types( rtti::Variant& value, const rtti::Property* prop, void* data )
	{
		if ( value.GetTypeName() == ::GetTypeName< DynArray< Uint32 > >()
			&& prop->GetType()->GetName() == ::GetTypeName< DynArray< Uint64 > > () )
		{

			DynArray< Uint32 > uints32{ red::PoolRTTI() };
			value.Get( uints32 );

			DynArray< Uint64 >* uints64 = reinterpret_cast< DynArray< Uint64 >* >( prop->GetOffsetPtr( data ) );
			if ( uints64 != nullptr )
			{
				for( Uint32 i = 0; i < uints32.Size(); ++i )
				{
					uints64->PushBack( static_cast< Uint64 > ( uints32[ i ] ) );
				}
			}

			return true;
		}

		// Not converted
		return false;
	}

	static Bool ConvertStringToResRefScriptToken( rtti::Variant& value, const rtti::Property* prop, void* data )
	{
		if ( value.GetTypeName() == ::GetTypeName< String >() && prop->GetType()->GetName() == ::GetTypeName< red::ResourceReferenceScriptToken >() )
		{
			const String* src = reinterpret_cast< const String* >( value.GetData() );
			red::ResourceReferenceScriptToken* dest = reinterpret_cast< red::ResourceReferenceScriptToken* >( prop->GetOffsetPtr( data ) );

			*dest = red::ResourceReferenceScriptToken( *src );
			return true;
		}

		return false;
	}

	Bool AreTypesSerializationCompatible( const rtti::IType* originalType, const rtti::IType* currentType )
	{
		// same type
		if ( originalType == currentType ) return true;

		// if any type is null then types are not compatible
		if ( !originalType || !currentType ) return false;

		// arrays are compatible if their inner type is compatible regardless of anything else
		const Bool isOriginalTypeArray = (originalType->GetType() == RT_Array) || (originalType->GetType() == RT_NativeArray) || (originalType->GetType() == RT_StaticArray);
		const Bool isCurrentTypeArray = (currentType->GetType() == RT_Array) || (currentType->GetType() == RT_NativeArray) || (currentType->GetType() == RT_StaticArray);
		if ( isOriginalTypeArray && isCurrentTypeArray )
		{
			const rtti::IBaseArrayType* originalArrayType = static_cast< const rtti::IBaseArrayType* >( originalType );
			const rtti::IBaseArrayType* currentArrayType = static_cast< const rtti::IBaseArrayType* >( currentType );

			// make sure that inner types are compatible
			return AreTypesSerializationCompatible( originalArrayType->ArrayGetInnerType(), currentArrayType->ArrayGetInnerType() );
		}

		// pointers are handles are serialization compatible
		const Bool isOriginalTypePointerLike = ( originalType->GetType() == RT_Pointer || originalType->GetType() == RT_Handle || originalType->GetType() == RT_WeakHandle );
		const Bool isCurrentTypePointerLike = ( currentType->GetType() == RT_Pointer || currentType->GetType() == RT_Handle || currentType->GetType() == RT_WeakHandle );
		if ( isOriginalTypePointerLike && isCurrentTypePointerLike )
		{
			const rtti::ClassType* originalPointedType = ( static_cast<const rtti::PointerType*>( originalType ) )->GetPointedType();
			const rtti::ClassType* currentPointedType = ( static_cast<const rtti::PointerType*>( currentType ) )->GetPointedType();

			if ( originalPointedType == currentPointedType || originalPointedType->IsA( currentPointedType ) || currentPointedType->IsA( originalPointedType ) )
			{
				return true;
			}
			return false;
		}
		
		// simple types conversions
		{
			// Uint32 -> Int32 conversion
			if ( originalType->GetName() == ::GetTypeName< Uint32 >() && currentType->GetName() == ::GetTypeName< Int32 >() )
			{
				return true;
			}

			// Int32 -> Uint32 conversion
			if ( originalType->GetName() == ::GetTypeName< Int32 >() && currentType->GetName() == ::GetTypeName< Uint32 >() )
			{
				return true;
			}
		}

		// RUID and RUIDRef
		if ( (originalType->GetName() == ::GetTypeName< CRUID >() && currentType->GetName() == ::GetTypeName< CRUIDRef >() ) ||
			 (originalType->GetName() == ::GetTypeName< CRUIDRef >() && currentType->GetName() == ::GetTypeName< CRUID >() ) )
		{
			return true;
		}

		// data buffer and shared data buffers are compatible
		if ( (originalType == GetTypeObject< DataBuffer >() && currentType == GetTypeObject< SharedDataBuffer >()) ||
			(originalType == GetTypeObject< SharedDataBuffer >() && currentType == GetTypeObject< DataBuffer >()) )
		{
			return true;
		}

		// String -> TweakDBID
		if( ( originalType == GetTypeObject< String >() && currentType == GetTypeObject< game::data::TweakDBID >() ) ||
			( originalType == GetTypeObject< game::data::TweakDBID >() && currentType == GetTypeObject< String >() ) )
		{
			return true;
		}

		// not directly compatible
		return false;
	}

	Bool ClassType::WritePropertyList( ISerializable* owner, IFile& file, void* data, const void* defaultData, const ClassType* defaultDataClass ) const
	{
		// Grab all struct properties
		const auto& properties = GetCachedProperties();

		// Serialize them
		for ( Uint32 i=0; i<properties.Size(); i++ )
		{
			const rtti::Property* prop = properties[i];
			if ( !prop )
				continue;

			// Skip unserializable properties
			if ( !prop->IsSerializable() )
			{
				continue;
			}

			// When cooking skip properties that are not for cooked builds
			if ( file.IsCooker() && !prop->IsSerializableInCookedBuilds() )
			{
				continue;
			}

			// Skip writing this property if not cooking
			if ( !file.IsCooker() && prop->IsSerializableInCookedBuildsOnly() )
			{
				continue;
			}

			// Do not save scripted properties that are not editable (internal scripted properties)
			{
				const Bool isScriptedStruct = IsScriptedStruct();
				const Bool isScriptedProp = prop->IsScripted() || isScriptedStruct;
				if ( isScriptedProp && !prop->IsEditable() )
				{
					continue;
				}
			}

			// Skip property if it has the same value as in the default object
			if ( defaultData )
			{
				// Property should be defined in the default class, if it's not defined that we cannot diff it
				if ( (defaultDataClass == this) || defaultDataClass->FindProperty( prop->GetName() ) == prop )
				{
					const void* srcData1 = prop->GetOffsetPtr( data );
					const void* srcData2 = prop->GetOffsetPtr( defaultData );
					if ( srcData1 && srcData2 )
					{
						if ( prop->GetType()->Compare( srcData1, srcData2, 0 ) )
						{
							continue;
						}
					}
				}
			}

			// Ask object if we should serialize this property
			if ( owner )
			{
				if ( !owner->OnPropertyCanSave( file, prop->GetName(), prop ) )
				{
					continue;
				}
			}

			// Save property name
			// TODO: in cooked data consider using a property index instead
			CName propName = prop->GetName();
			file << propName;

			// Save property type reference
			// TODO: this can be skipped in cooked data (we should be 100% sure about the property type)
			const rtti::IType* propType = prop->GetType();
			file << propType;

			// Save property value, contain it inside a skipable block so in case of any mismatch during loading we can skip it+
			{
				// TODO: this should not be used in cooked data
				fs::SkipableBlock block( file );

				// Save property value
				void* propData = prop->GetOffsetPtr( data );
				if ( !prop->GetType()->Serialize( file, propData ) )
				{
					// Serialization failed
					if ( !file.IsCooker() )
					{
						RED_LOG_ERROR( "Core: Serialization of property '%hs' has failed", prop->GetName().AsChar() );
					}

					return false;
				}
			}
		}

		// Use empty property name as marker
		CName emptyName;
		file << emptyName;

		// Done
		return true;
	}

	Bool ClassType::ReadPropertyList( ISerializable* owner, IFile& file, void* data ) const
	{
		// Safe serialization
		for ( ;; )
		{
			CName propName;
			file << propName;

			// End Of List marker
			if ( !propName )
			{
				break;
			}

			// Load property type (reads a CName and automatically finds the type - cached results!)
			const rtti::IType* originalType = nullptr;
			file << originalType;

			{
				// We save each property inside a skippable block in order to be able to skip the data fully in case the type is gone
				// TODO: this should be skipped in final/cooked data - it is unnecessary stuff
				fs::SkipableBlock block( file );

				// Original type is not know - there's not way to read the property value
				if ( nullptr == originalType )
				{
#ifndef RED_PLATFORM_CONSOLE
					RED_LOG_WARNING( "Core: Property '%hs' in '%hs' uses unknown type. Property value will be lost.",
						propName.AsChar(), GetName().AsChar() );
#endif

					block.Skip();
					continue;
				}

				// Get the property of given name
				const rtti::Property* prop = FindProperty( propName );

				// Check if property exists.
				if ( !prop || !prop->IsSerializable() )
				{

					// ctremblay: Legacy WeakHandle< CResource > cannot be save in a variant... 
 					if (originalType->GetType() == RT_WeakHandle)
					{
						if (static_cast<const WeakHandleType *>(originalType)->GetPointedType()->IsA< CResource >())
						{
							block.Skip();
							continue;
						}
					}
					
					// Read the propertie's value to a univeral variant data structure for handling
					// This requires the RTTI type to be avaiable for the property
					rtti::Variant value( originalType, nullptr );
					if ( value.IsValid() )
					{
						// Load data
						if ( value.SerializeData( file ) )
						{
							// Let the object handle the property
							if ( owner && owner->OnPropertyMissing( propName, value ) )
							{
								block.Skip();
								continue;
							}

							// TODO: try per class handling
						}
					}

					// Property not found
					block.Skip();
					continue;
				}

				// Find original type the value was saved with, this is done to support seamless type conversion
				// This may return NULL in case when the original sub-type (for example a class) no longer exists so be careful.
				// Also, for performance reason if the type name already matches the type lookup is skipped.
				if ( !AreTypesSerializationCompatible( originalType, prop->GetType() ) )
				{

					// If we are in here it means that the original type the value was saved with does not have compatible serialization with new type.
					// In general if we are able to load the original value we can try to convert it. If this fails we unfortunately need to skip the 
					// serialized value never knowing what it was.

					// Original type was not found, we cannot read the value
					if ( nullptr == originalType )
					{
#ifndef RED_PLATFORM_CONSOLE
						RED_LOG_WARNING( "Core: Property '%hs' in '%hs' has different type '%hs' than the one that was serialized with ('%hs'). Original type does not exist any more. Property value will be lost.",
							propName.AsChar(), GetName().AsChar(), prop->GetType()->GetName().AsChar(), originalType->GetName().AsChar() );
#endif

						block.Skip();
						continue;
					}

					// ctremblay: HACK - Weakptr to resref.
					if( prop->GetType()->GetType() == RT_ResourceReference && originalType->GetType() == RT_WeakHandle )
					{
						const rtti::ClassType* originalPointedType = ( static_cast<const rtti::PointerType*>( originalType ) )->GetPointedType();
						if( originalPointedType->IsA< CResource >() )
						{
							res::ResourceReference& handle = * ( res::ResourceReference* )( prop->GetOffsetPtr( data ) );
							handle.HACK_SerializeForConversion( file );
						
							continue;
						}
					}

					// Load the old value
					rtti::Variant value( originalType, nullptr );
					if ( value.IsValid() && value.SerializeData( file ) )
					{
						// Let the object handle the conversion shit
						if ( owner && owner->OnPropertyTypeMismatch( propName, prop, value ) )
						{
							// Either way, skip the to the end of original data
							block.Skip();
							continue;
						}

						// Convert from resource references to handles automatically
						if ( ConvertResRefToHandleTypes( value, prop, data ) )
						{
							// Either way, skip the to the end of original data
							block.Skip();
							continue;
						}

						// Convert from handles to resource references automatically
						if ( ConvertHandleToResRefTypes( value, prop, data ) )
						{
							// Either way, skip the to the end of original data
							block.Skip();
							continue;
						}

						// Convert from handles to resource references automatically
						if ( ConvertHandleToAsyncResRefTypes( value, prop, data ) )
						{
							// Either way, skip the to the end of original data
							block.Skip();
							continue;
						}

						// Convert from resource async references to handles automatically
						if ( ConvertResAsyncRefToHandleTypes( value, prop, data ) )
						{
							// Either way, skip the to the end of original data
							block.Skip();
							continue;
						}

						// Convert from resource async references to strings automatically
						if ( ConvertResAsyncRefToStringType( value, prop, data ) )
						{
							// Either way, skip the to the end of original data
							block.Skip();
							continue;
						}

						// Convert from resource references to resource async references automatically
						if ( ConvertResRefToResAsyncRef( value, prop, data ) )
						{
							// Either way, skip the to the end of original data
							block.Skip();
							continue;
						}

						// Convert from resource async references to resource references automatically
						if ( ConvertResAsyncRefToResRefTypes( value, prop, data ) )
						{
							// Either way, skip the to the end of original data
							block.Skip();
							continue;
						}

						// Convert from strings to cnames automatically
						if ( ConvertStringToCnameTypes( value, prop, data ) )
						{
							block.Skip();
							continue;
						}

						// Convert arrays of strings to cnames automatically
						if ( ConvertArrayStringsToCNamesTypes( value, prop, data ) )
						{
							block.Skip();
							continue;
						}

						// Converts enums to ints automatically
						if ( ConvertEnumToIntTypes( value, prop, data ) )
						{
							block.Skip();
							continue;
						}

						// Converts string Uni to ascii automatically
						if ( ConvertStringToStringAnsiTypes( value, prop, data ) )
						{
							block.Skip();
							continue;
						}

						// Converts old style time format to new style time format automatically
						if( ConvertUint64ToCDateTimeTypes( value, prop, data ) )
						{
							block.Skip();
							continue;
						}

						// These conversions are not treated as compatible because of differences in sizes, a manual conversion is needed.
						if ( ConvertIntToIntTypes( value, prop, data ) )
						{
							block.Skip();
							continue;
						}

						// Convert between databuffer and deferreddatabuffer
						if ( ConvertDataBufferToDeferredDataBuffer( value, prop, data ) )
						{
							block.Skip();
							continue;
						}

						if ( ConvertByteArrayToDataBuffer( value, prop, data ) )
						{
							block.Skip();
							continue;
						}

						// Convert between euler angles and quaternions
						if (ConvertEulerAnglesToQuaternion(value, prop, data))
						{
							block.Skip();
							continue;
						}

						if(ConvertVector3ToVector4(value, prop, data))
						{
							block.Skip();
							continue;
						}

						// Convert strings to resource reference tokens
						if ( ConvertStringToResRefScriptToken( value, prop, data ) )
						{
							block.Skip();
							continue;
						}

						// Convert array of Uint32 to array of Uint64
						if ( ConvertArrayUint32ToUint64Types( value, prop, data ) )
						{
							block.Skip();
							continue;
						}

						// We'll try to handle it ourselves later.
						// TODO: add per-class handling
					}

					// Property type mismatched
#ifndef RED_PLATFORM_CONSOLE
					{
						RED_LOG_WARNING( "Core: Property '%hs' in '%hs' has different type '%hs' than the one that was serialized ('%hs'). No value conversion found, property value will be lost.",
							propName.AsChar(), GetName().AsChar(), prop->GetType()->GetName().AsChar(), originalType->GetName().AsChar() );
					}
#endif

					// Skip the whole block
					block.Skip();
					continue;
				}

				// Get the target offset
				void* propData = prop->GetOffsetPtr( data );

				// Load value
				if ( !prop->GetType()->Serialize( file, propData, owner ) )
				{
					// Unable to deserialize the value
					block.Skip();
					continue;
				}
#ifndef NO_EDITOR
				// Allow the owner to handle the range mismatch if it occurs
				if ( owner && prop->IsRanged() && prop->GetType()->GetName() == RED_NAME_CONSTEXPR_NOREG( "Float" ) )
				{
					const Float* floatPropData = (Float*)propData;
					if ( *floatPropData > prop->GetRangeMax() || *floatPropData < prop->GetRangeMin() )
					{
						owner->OnPropertyRangeMismatch( propName, prop->GetRangeMin(), prop->GetRangeMax(), *floatPropData );
					}
				}
#endif

				// Check data consistency
				if ( file.GetOffset() != block.GetEndOffset() )
				{
#ifndef RED_PLATFORM_CONSOLE
					RED_LOG_WARNING( "Core: Data inconsistency when reading property '%hs' in '%hs', offset %i, exptected %i", propName.AsChar(), GetName().AsChar(), file.GetOffset(), block.GetEndOffset() );
#endif

					block.Skip();
					continue;
				}
			}
		}

		// Done
		return true;
	}

	void ClassType::HandleSerializationFlags( IFile& file, Uint8& flags ) const
	{
		// store flags
		file << flags;
	}

	Bool ClassType::SerializeDiff( ISerializable* owner, IFile& file, void* data, const void* defaultData, const ClassType* defaultDataClass ) const
	{
		// Save/Load the serialization flags
		Uint8 serializationFlags = 0;
		HandleSerializationFlags( file, serializationFlags );

		// Save/Load the object data
		if ( file.IsReader() )
		{
			return ReadPropertyList( owner, file, data );
		}
		else if ( file.IsWriter() )
		{
			return WritePropertyList( owner, file, data, defaultData, defaultDataClass );
		}

		// Done
		return true;
	}

	Bool ClassType::NeedsCleaning() const
	{
		return true; // always clean is needed, as we do not know whether class has custom destructor (which for example frees memory)
	}

	void ClassType::ResolvePropertyOverrides( const Bool force ) const
	{
		if ( (!m_arePropertyOverridesResolved | force) && HasPropertyOverrides() )
		{
			RED_FATAL_ASSERT( HasBaseClass(), "Class '%hs' has property overrides defined but it does not have any base class!", GetName().AsChar() );

			for ( const PropOverride& propOverride : m_propertyOverrides )
			{
				rtti::Property* propertyOverride = propOverride.m_propertyOverride;
				const rtti::Property* baseProperty = m_baseClass->FindProperty( propertyOverride->GetName() );

				RED_FATAL_ASSERT( baseProperty != nullptr, "Class '%hs' has property override '%hs' defined, but property with such "
				                  "a name was not found in any of the base classes", GetName().AsChar(), propertyOverride->GetName().AsChar() );

				propertyOverride->ResolveOverride( *baseProperty, propOverride.m_overrideMask );
			}
		}

		m_arePropertyOverridesResolved = true;
	}

	void ClassType::RecalculateCachedProperties( const Bool force ) const
	{
		if ( !m_arePropertiesCached || force )
		{
			red::ScopedLock< red::RWSpinLock > scopedLock( m_cachedPropertiesLock );
			if( !m_arePropertiesCached || force )
			{
				m_cachedProperties.Clear();
				m_cachedPropertyDictionary.Clear();
				m_propertiesToDestroy.Clear();

				GetProperties( m_cachedProperties );
				
				for ( TPropertyList::const_iterator it=m_cachedProperties.Begin(); it!=m_cachedProperties.End(); ++it )
				{
					const rtti::Property* prop = *it;
					if ( prop->GetType() && prop->GetType()->NeedsCleaning() )
					{
						m_propertiesToDestroy.PushBack( prop );
					}
					m_cachedPropertyDictionary[ prop->GetName() ] = prop;
				}

				m_cachedProperties.Shrink();
				m_propertiesToDestroy.Shrink();
				m_arePropertiesCached = true;	
			}
		}
	}

	void ClassType::RecalculateCachedPersistentProperties(const Bool force /*= false */) const
	{
		if ( !m_arePersistentPropertiesCached || force )
		{
			red::ScopedLock< red::RWSpinLock > scopedLock( m_cachedPropertiesLock );
			if( !m_arePersistentPropertiesCached || force )
			{
				m_cachedPersistentProperties.Clear();
				GetPersistentProperties( m_cachedPersistentProperties );

				m_cachedPersistentProperties.Shrink();
				m_arePersistentPropertiesCached = true;	
			}
		}
	}

	void ClassType::RecalculateCachedScriptPropertiesToDestroy()
	{
		m_cachedSecondaryPropertiesToDestory.Clear();

		const auto& allProps = GetCachedProperties();
		for ( const rtti::Property* prop : allProps )
		{
			if ( prop->IsInSecondaryDataBuffer() && prop->GetType()->NeedsCleaning() )
			{
				PropInfo propInfo;
				propInfo.m_offset = prop->GetDataOffset();
				propInfo.m_type = prop->GetType();
				m_cachedSecondaryPropertiesToDestory.PushBack( propInfo );
			}
		}

		m_cachedSecondaryPropertiesToDestory.Shrink();
		m_areSecondaryPropertiesToDestroyCached = true;
	}

	void ClassType::RecalculateCachedFunction()
	{
		m_cachedFunctionByName.Clear();
		m_cachedFunctionByHash.Clear();

		m_cachedFunctionByHash.Reserve( m_localFunctions.Size() );
		m_cachedFunctionByName.Reserve( m_localFunctions.Size() );

		for(Uint32 index = 0, end = m_localFunctions.Size(); index != end; ++index)
		{
			const Function * func = m_localFunctions[index];
			m_cachedFunctionByName.Insert( func->GetName(), func );
			m_cachedFunctionByHash.Insert( func->GetFunctionHash(), func );
		}
	}

	void ClassType::RecalculateAllCachedData()
	{
		RecalculateCachedProperties( true );
		RecalculateCachedPersistentProperties( true );
		RecalculateCachedScriptPropertiesToDestroy();
		RecalculateCachedFunction();
	}

	const ClassType::TPropertyList& ClassType::GetCachedProperties() const
	{
		// ctremblay if another thread add a property or request to rebuild cached properties, we are doomed.
		RecalculateCachedProperties();
		return m_cachedProperties;
	}

	const rtti::ClassType::TPropertyList& ClassType::GetCachedPersistentProperties() const
	{
		RecalculateCachedPersistentProperties();
		return m_cachedPersistentProperties;
	}

	Bool ClassType::IsSerializable() const
	{
		return IsA< ISerializable >();
	}

	void * ClassType::GetDefaultObject() const
	{
		if( !m_defaultObject )
		{
			CreateDefaultObject();
		}

		return m_defaultObject;
	}

	void ClassType::RebuildParentHierarchy( void* object, ISerializable * parent ) const
	{
		if( IsSerializable() )
		{
			ISerializable * serializable = static_cast< ISerializable* >( object );
			serializable->SetParent( parent );
			parent = serializable;
		}

		for( auto & property : GetCachedProperties() )
		{
			const IType * propertyType = property->GetType();
			void * propertyOffset = property->GetOffsetPtr( object );
			propertyType->RebuildParentHierarchy( propertyOffset, parent );
		}
		
	}

#ifndef NO_EDITOR_PROPERTY_SUPPORT

	void ClassType::GetEditableProperties( EditableProperties& outEditableProperties ) const
	{
		const Uint32 flags = PF_Editable;

		GetEditableProperties( flags, outEditableProperties );
	}

	void ClassType::GetInstanceEditableProperties( EditableProperties& outEditableProperties ) const
	{
		const Uint32 flags = PF_InstanceEditable;

		GetEditableProperties(flags, outEditableProperties);
	}

	bool IsPropertyReadonly( Uint32 flags, const Property * property )
	{
		const bool noReadOnly = Config::cvEditAllClassProperties.Get();

		if( noReadOnly )
		{
			return false;
		}

		if( property->IsReadOnly() )
		{
			return true;
		}

		return !property->IsEditable();
	}

	void ClassType::GetEditableProperties( Uint32 flags, EditableProperties& outEditableProperties ) const
	{
#ifndef NO_EDITOR
		// static - class props
		const auto& allProps = GetCachedProperties();
		for (const auto* prop : allProps)
		{
			const Uint64 propertyFlags = prop->GetFlags();

			// filter out mismatched properties unless the debug access mode was enabled
			if ( !Config::cvShowAllClassProperties.Get() )
			{
				// filter out non-editable properties
				if ( !( propertyFlags & PF_Editable ) )
				{
					continue;
				}

				// filter out private properties from base classes
				if ( prop->IsPrivate() && prop->GetParent() != this )
				{
					continue;
				}
			}

			// setup 
			rtti::ClassEditablePropertyInfo info;
			info.m_category = prop->GetCategory();
			info.m_name = GetFilteredPropertyName( prop->GetName() );
			if ( info.m_name )
			{
				info.m_isReadOnly = IsPropertyReadonly( flags, prop );
				info.m_type = prop->GetType();
				info.m_customTypeName = prop->GetCustomEditorType();
				info.m_customTypeMode = prop->GetCustomEditorMode();
				info.m_isInlined = prop->IsInlined();
				info.m_minValue = prop->GetRangeMin();
				info.m_maxValue = prop->GetRangeMax();
				info.m_xMinValue = prop->GetXRangeMin();
				info.m_xMaxValue = prop->GetXRangeMax();
				info.m_isResizable = prop->IsResizable();
				info.m_canAdd = prop->CanAdd();
				info.m_isMask = prop->IsMask();
				info.m_minSize = prop->GetSizeMin();
				info.m_maxSize = prop->GetSizeMax();
				info.m_canClear = prop->CanClear();
				info.m_canReset = prop->CanReset();
				info.m_canSelect = prop->CanSelect();
				info.m_isInstanceEditable = prop->IsInstanceEditable();
				info.m_tooltip = prop->GetTooltip();
				info.m_hideSlider = prop->ShouldHideSlider();
				info.m_isBrowsable = prop->IsBrowsable();
				info.m_keyPath = prop->GetKeyPath();
				outEditableProperties.Add( info );
			}
		}
#endif
	}

#endif //! NO_EDITOR_PROPERTY_SUPPORT

	void ClassType::MarkAsInitialized()
	{
		RED_FATAL_ASSERT( m_isFullyInitialized == 0, "Class %hs already initailized", GetName().AsChar() );
		m_isFullyInitialized = 1;
	}


#ifdef USE_PROFILER
	red::InstrumentationObject & ClassType::GetInstrumentationObject() const
	{
		return m_instrumentationObject;
	}
#endif


	/// Meta template recursion depth
#define CLASS_RTTI_RECURSION_DEPTH		16

	/// Meta Templated IsA function ( way faster )
	template< int N >
	RED_FORCE_INLINE Bool IsA_Meta( const ClassType* thisClass, const ClassType* testClass )
	{
		// Direct class match
		if ( testClass == thisClass )
		{
			return true;
		}

		// Test root class
		const ClassType* baseClass = thisClass->GetBaseClass();
		if ( baseClass && IsA_Meta<N-1>( baseClass, testClass ) )
		{
			return true;
		}

		// Not a base class
		return false;
	}

	/// Default level-0 implementation
	template<>
	RED_FORCE_INLINE Bool IsA_Meta<0>( const ClassType*, const ClassType* )
	{
		// Note: normally, this should recurse to normal IsA implementation
		// but since the class tree is not infinite we can safely drop the recursion.
		RED_HALT( "Class tree to deep. Increase the meta template recursion depth." );
		return false;
	}

	Bool ClassType::IsA( const ClassType* testedClass ) const
	{
		return IsA_Meta< CLASS_RTTI_RECURSION_DEPTH >( this, testedClass );
	}

	const ClassType* ClassType::FindCommonBase( const ClassType* typeA, const ClassType* typeB )
	{
		if ( typeA == typeB )
		{
			return typeA;
		}

		red::StaticArray< const ClassType*, CLASS_RTTI_RECURSION_DEPTH > parentsA, parentsB;

		while ( typeA )
		{
			parentsA.PushBack( typeA );
			typeA = typeA->GetBaseClass();
		}
		std::reverse( parentsA.Begin(), parentsA.End() );

		while ( typeB )
		{
			parentsB.PushBack( typeB );
			typeB = typeB->GetBaseClass();
		}
		std::reverse( parentsB.Begin(), parentsB.End() );

		const ClassType* bestMatch = nullptr;

		for ( Uint32 i = 0; i < parentsA.Size() && i < parentsB.Size(); ++i )
		{
			if ( parentsA[ i ] == parentsB[ i ] )
			{
				bestMatch = parentsA[ i ];
			}
		}

		return bestMatch;
	}

	/// Meta template UpCasting
	template< int N >
	RED_FORCE_INLINE void* UpCast_Meta( const ClassType* destClass, const ClassType* thisClass, void* thisObj )
	{
		// Target class
		if ( destClass == thisClass )
		{
			return thisObj;
		}

		// Go to base classes for "this class", if any of it returns valid pointer cast it to "this class"
		const ClassType* baseClass = thisClass->GetBaseClass();
		if ( baseClass )
		{
			void* basePtrNotYetCasted = UpCast_Meta<N-1>( destClass, baseClass, thisObj );
			return basePtrNotYetCasted;
		}

		// No cast
		return nullptr;
	}

	/// Default level-0 implementation
	template<>
	RED_FORCE_INLINE void* UpCast_Meta<0>( const ClassType*, const ClassType*, void* )
	{
		// Note: normally, this should recurse to normal UpCast implementation
		// but since the class tree is not infinite we can safely drop the recursion.
		RED_HALT( "Class tree to deep. Increase the meta template recursion depth." );
		return nullptr;
	}

	void* ClassType::CastTo( const ClassType* destClass, void* obj ) const
	{	
		// Call the magic
		return UpCast_Meta< CLASS_RTTI_RECURSION_DEPTH >( destClass, this, obj );
	}

	/// Meta template DownCasting
	template< int N >
	RED_FORCE_INLINE void* DownCast_Meta( const ClassType* destClass, const ClassType* thisClass, void* thisObj )
	{
		// Target class reached !
		if ( destClass == thisClass )
		{
			return thisObj;
		}

		// Go to base classes for "dest class"
		const ClassType* baseClass = destClass->GetBaseClass();
		if ( baseClass )
		{
			void* basePtrNotYetCasted = DownCast_Meta<N-1>( baseClass, thisClass, thisObj );
			return basePtrNotYetCasted;
		}

		// No cast
		return nullptr;
	}

	/// Default level-0 implementation
	template<>
	RED_FORCE_INLINE void* DownCast_Meta<0>( const ClassType*, const ClassType*, void* )
	{
		// Note: normally, this should recurse to normal UpCast implementation
		// but since the class tree is not infinite we can safely drop the recursion.
		RED_HALT(  "Class tree to deep. Increase the meta template recursion depth." );
		return nullptr;
	}

	void* ClassType::CastFrom( const ClassType* srcClass, void* obj ) const
	{	
		// Do the magic
		return DownCast_Meta< CLASS_RTTI_RECURSION_DEPTH >( this, srcClass, obj );
	}

	const red::memory::Pool & ClassType::GetInnerTypeMemoryPool() const
	{
		return red::PoolSerializable::GetInstance();
	}

    const ClassType* ClassType::GetFirstNativeBaseClass() const
    {
        if( IsScriptedType() )
        {
            // Find first native base class (if any)
            const ClassType* baseClass = GetBaseClass();
            while( baseClass != nullptr && baseClass->IsScriptedType() )
            {
                baseClass = baseClass->GetBaseClass();
            }
            
            return baseClass;
        }

        return this;
    }


	void ClassType::Internal_RegisterEventConnector( CName functionName, const ClassType* eventType, const red::EventConnector& connector )
	{
		auto eventId = eventType->GetEventClassId();
		m_eventConnector.PushBack( { connector, functionName, eventId, false } );
		m_supportedEventMask.Set( eventId, true );	
	}

	bool ClassType::CanServiceEvent( const ClassType* eventType ) const
	{
		const Uint16 eventId = eventType->m_eventClassId;
		if( eventId != (Uint16)~0 )
		{
			return m_supportedEventMask.Get( eventId );
		}
		
		return false;
	}

	bool ClassType::CanServiceEvents() const
	{
		return m_supportedEventMask.IsAnySet();
	}

	void AcquireNextEventClassId( atomic::TAtomic16& resultId )
	{
		while(resultId == (atomic::TAtomic16)~0)
		{
			static atomic::TAtomic16 s_eventId = 0;
			atomic::TAtomic16 eventId = atomic::Increment16( &s_eventId );
			atomic::CompareExchange16( &resultId, eventId, (atomic::TAtomic16)~0 );
		}
	}

	Uint16 ClassType::GetEventClassId() const
	{
		if( m_eventClassId == c_invalidEventClassId )
		{
			AcquireNextEventClassId( m_eventClassId );
		}

		return m_eventClassId;
	}

	void ClassType::Internal_CollectEventConnector( red::EventConnectorCollector & collector ) const
	{
		if( m_supportedEventMask.IsAnySet() )
		{
			for(const auto &connector : m_eventConnector)
			{
				if( !connector.scriptedConnector )
				{
					collector.CollectConnector( connector.eventId, connector.connector );
				}
				else
				{
					collector.CollectScriptedConnector( connector.eventId, connector.connector, connector.functionName );
				}
			}
		}

		if( m_baseClass )
		{
			m_baseClass->Internal_CollectEventConnector( collector );
		}
	}

	const rtti::ClassType* ExtractFunctionParamEventType( const rtti::Function* func )
	{
		if( !func->IsEvent() )
			return nullptr;

		// the event handler function must have ONE parameter - the event data
		if( func->GetNumParameters() != 1 )
			return nullptr;

		// get the type of the parameter, it MUST be a handle to the Event kind of thing
		const auto paramType = func->GetParameter( 0 )->GetType();
		if( paramType->GetType() != RT_Handle )
			return nullptr;

		// we must have a proper event data 
		const auto* eventClassType = static_cast< const rtti::HandleType* >( paramType )->GetPointedType();
		return eventClassType->IsA( ClassID< red::Event >() ) ? eventClassType : nullptr;
	}

	void ClassType::Internal_RegisterScriptedEventConnectors()
	{
		for( const Function* func : m_localFunctions )
		{
			const rtti::ClassType* eventClassType = ExtractFunctionParamEventType( func );
			if( eventClassType )
			{
				const CName functionName = func->GetName();

				auto connector = [functionName]( ISerializable& object, const THandle< red::Event >& event )
				{	
					IScriptable * listener = static_cast< IScriptable *>( &object );
					const rtti::ClassType * listenerClassType = listener->GetClass();	
					const rtti::Function * func = listenerClassType->FindFunction( functionName );
					if( func )
					{
						rtti::FunctionContext_ExternalParams context( listener, (void*)&event, nullptr, nullptr, nullptr );
						func->Call( context );
					}
					else
					{ /* ctremblay: Function is gone. Did script got reloaded ? */ }
				};

				auto eventId = eventClassType->GetEventClassId();
				m_eventConnector.PushBack( { connector, functionName, eventId, true } );
				m_supportedEventMask.Set( eventId, true );
			}
		}
	}

	///

	EditableProperties::EditableProperties()
		: m_properties( red::PoolRTTI() )
	{
	}

	EditableProperties::~EditableProperties() = default;

	void EditableProperties::Add( const ClassEditablePropertyInfo& propertyInfo )
	{
		InsertAt( m_properties.Size(), propertyInfo );
	}

	void EditableProperties::Add( const red::DynArray< rtti::ClassEditablePropertyInfo >& props )
	{
		for ( const rtti::ClassEditablePropertyInfo& prop : props )
		{
			Add( prop );
		}
	}

	void EditableProperties::InsertAt( const Uint32 index, const ClassEditablePropertyInfo& propertyInfo )
	{
		RED_ASSERT( std::find_if( m_properties.Begin(), m_properties.End(), [&propertyInfo]( const ClassEditablePropertyInfo& element ) { return element.m_name == propertyInfo.m_name && element.m_category != propertyInfo.m_category; } ) == m_properties.End(), "Property name must be unique" );
		m_properties.InsertAt( index, propertyInfo );
	}

	Bool EditableProperties::RemoveAll( CName propertyName )
	{
		for ( Uint32 index : m_properties.ReverseIndices() )
		{
			if ( m_properties[ index ].m_name == propertyName )
				m_properties.RemoveAt( index );
		}

		return true;
	}

	Bool EditableProperties::RemoveIf( red::FixedSizeFunction< Bool( const ClassEditablePropertyInfo& ) > pred )
	{
		auto it = std::remove_if( m_properties.Begin(), m_properties.End(), pred );
		return it != m_properties.End() ? m_properties.Remove( it, m_properties.End() ).IsSuccessful() : false;
	}

	Bool EditableProperties::RemoveAt( const Uint32 index )
	{
		return m_properties.RemoveAt( index ).IsSuccessful();
	}

	void EditableProperties::Clear()
	{
		m_properties.Clear();
	}

	const rtti::ClassEditablePropertyInfo* EditableProperties::Find( CName name )
	{
		auto it = std::find_if( m_properties.Begin(), m_properties.End(), [name]( const rtti::ClassEditablePropertyInfo& element )
		{
			return element.m_name == name;
		} );

		return it != m_properties.End() ? &(*it) : nullptr;
	}

	Uint32 EditableProperties::IndexOf( CName name ) const
	{
		for( const Uint32 propertyIndex : m_properties.Indices() )
		{
			if( m_properties[ propertyIndex ].m_name == name )
			{
				return propertyIndex;
			}
		}

		return red::INVALID_INDEX;
	}

	void EditableProperties::OverrideProperty( const rtti::ClassEditablePropertyInfo& info )
	{
		auto it = std::find_if( m_properties.Begin(), m_properties.End(), [&info]( const rtti::ClassEditablePropertyInfo& element )
		{
			return element.m_name == info.m_name;
		} );

		if ( it != m_properties.End() )
		{
			*it = info;
		}
	}

	void EditableProperties::Modify( red::FixedSizeFunction< void( ClassEditablePropertyInfo& ) > pred )
	{
		for( auto& prop : m_properties )
		{
			pred( prop );
		}
	}

} // rtti
