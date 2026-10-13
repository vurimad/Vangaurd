/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#include "build.h"
#include "rttiPropertyBuilder.h"
#include "rttiProperty.h"
#include "rttiReplicationBinding.h"
#include "rttiClass.h"
#include "rttiArrayTypes.h"
#include "rttiFundamentalTypes.h"


namespace rtti
{
	PropertyBuilder::ReplicationInfo::ReplicationInfo( const rep::Type* replicatedPropertyType )
		: m_isReplicated( false )
		, m_repType( replicatedPropertyType )
		, m_neverChanges( false )
		, m_logChanges( false )
		, m_isReplicatedInline( true )
		, m_isWeakEntityHandle( false )
		, m_isWeakEntityComponentHandle( false )
		, m_replicationTarget( rep::ReplicationTarget::All )
		, m_arrayReplicationMode( rep::ArrayReplicationMode::Default )
		, m_tag()
	{}

	//////////////////////////////////////////////////////////////////////////
	// PropertyBuilder
	//////////////////////////////////////////////////////////////////////////
	PropertyBuilder::PropertyBuilder(
		const rtti::ClassType* parentClass,
		size_t offset, 
		CName name,
		CName category,
		const rtti::IType* propertyType,
		const rep::Type* replicatedPropertyType )
		: m_parentClass( parentClass )
		, m_name( name )
		, m_category( category )
		, m_offset( offset )
		, m_propertyType( propertyType )
		, m_flags( PF_Clear | PF_Reset | PF_Select | PF_CanAdd | PF_Browsable )
		, m_editorInfo{ 
			/*m_tooltip*/	String::EMPTY(),
			/*m_customEditor*/	String::EMPTY(),
			/*m_customEditorMode*/	CustomEditorMode::NormalType,
			/*m_min*/	0.0f,
			/*m_max*/	0.0f,
			/*m_precision*/	std::numeric_limits< Float >::min(),
			/*m_minSize*/	0,
			/*m_maxSize*/	0,
			/*m_curveXMin*/	0.0f,
			/*m_curveXMax*/	0.0f
		}
		, m_replicationInfo( replicatedPropertyType )
	{
		RED_FATAL_ASSERT( m_propertyType || m_replicationInfo.m_repType, "Core: Trying to create property %hs in class %hs - type unknown!",
				m_name.AsChar(), m_parentClass ? m_parentClass->GetName().AsChar() : "<unknown>" );
	}

	PropertyBuilder::~PropertyBuilder()
	{}

	void PropertyBuilder::AddPropertyToClass()
	{
		if ( !m_propertyType && m_replicationInfo.m_isReplicated )
		{
			// This is the case when property doesn't have corresponding "engine RTTI type" but it does have corresponding "replication RTTI type"

			AddReplicatedPropertyToClass( nullptr );
			return;
		}

		Property* property = RED_NEW( Property )(
			const_cast< const rtti::IType* >( m_propertyType ),
			m_parentClass,
			static_cast< Uint32 >( m_offset ),
			m_name,
			m_flags | PF_Native,
			m_editorInfo.m_tooltip,
			m_category,
			m_editorInfo.m_customEditor,
			m_editorInfo.m_min, m_editorInfo.m_max,
			m_editorInfo.m_editorFlags
		);
		property->SetSizeConstraints( m_editorInfo.m_minSize, m_editorInfo.m_maxSize );
		property->SetXAxisConstraints( m_editorInfo.m_curveXMin, m_editorInfo.m_curveXMax );
		property->SetCustomEditorMode( m_editorInfo.m_customEditorMode );
		property->SetKeyPath( m_editorInfo.m_keyPath );

		const_cast<ClassType*>( m_parentClass )->AddProperty( property );

		// If replicated, then add it to network class too

		if ( m_replicationInfo.m_isReplicated )
		{
			if ( !m_replicationInfo.m_repType )
			{
				DetermineReplicatedType();
			}

			AddReplicatedPropertyToClass( property );
		}
	}

	void PropertyBuilder::DetermineReplicatedType()
	{
		RED_ASSERT( !m_replicationInfo.m_repType );
		RED_ASSERT( m_propertyType );

		rep::SimpleTypeDesc repTypeDesc( m_propertyType );

		if ( m_editorInfo.m_min != 0.0f || m_editorInfo.m_max != 0.0f )
		{
			repTypeDesc.m_min = m_editorInfo.m_min;
			repTypeDesc.m_max = m_editorInfo.m_max;
			repTypeDesc.m_precision = m_editorInfo.m_precision;
		}

		repTypeDesc.m_isReplicatedInline = m_replicationInfo.m_isReplicatedInline;

		if ( m_replicationInfo.m_isWeakEntityHandle )
		{
			repTypeDesc.m_type = rep::EType::WeakEntityHandle;
		}
		else if ( m_replicationInfo.m_isWeakEntityComponentHandle )
		{
			repTypeDesc.m_type = rep::EType::WeakComponentHandle;
		}

		if ( m_editorInfo.m_maxSize > 0 )
		{
			repTypeDesc.m_maxLength = m_editorInfo.m_maxSize;
		}

		repTypeDesc.m_arrayReplicationMode = m_replicationInfo.m_arrayReplicationMode;
		if ( m_replicationInfo.m_arrayReplicationMode == rep::ArrayReplicationMode::AsMap )
		{
			repTypeDesc.m_keyMemberName = m_replicationInfo.m_replicateAsMapArrayKeyName;
		}

		m_replicationInfo.m_repType = rep::IRTTIService::GetInstance().DetermineMatchingType( repTypeDesc );
		RED_ASSERT( m_replicationInfo.m_repType, "Failed to determine replicated type for %s property in %s class", m_name.AsChar(), m_parentClass->GetName().AsChar() );
	}

	void PropertyBuilder::AddReplicatedPropertyToClass( Property* coreProperty )
	{
		RED_ASSERT( m_replicationInfo.m_repType );

		rep::PropertyFlags flags;
		flags.m_isNeverChanges = m_replicationInfo.m_neverChanges;
		flags.m_isChangesLogged = m_replicationInfo.m_logChanges;
		flags.m_isReplicatedToControllingPeerOnly = ( m_replicationInfo.m_replicationTarget == rep::ReplicationTarget::ControllingPeer );
		flags.m_isReplicatedToNonControllingPeers = ( m_replicationInfo.m_replicationTarget == rep::ReplicationTarget::NonControllingPeers );
		
		if ( coreProperty )
		{
			rep::IRTTIService::GetInstance().AddProperty( m_parentClass, coreProperty, m_replicationInfo.m_repType, flags, m_replicationInfo.m_tag );
		}
		else
		{
			rep::IRTTIService::GetInstance().AddProperty( m_parentClass, ( Uint32 ) m_offset, m_name, m_replicationInfo.m_repType, flags, m_replicationInfo.m_tag );
		}
	}

	void PropertyBuilder::_HintIsWeakEntityHandle( const Bool isWeakEntityHandle )
	{
		m_replicationInfo.m_isWeakEntityHandle = isWeakEntityHandle;
	}
	
	void PropertyBuilder::_HintIsWeakEntityComponentHandle( const Bool isWeakEntityComponentHandle )
	{
		m_replicationInfo.m_isWeakEntityComponentHandle = isWeakEntityComponentHandle;
	}

	Bool PropertyBuilder::IsPersistentTypeSupported( const rtti::IType* type )
	{
		// for starters, support all fundamental types, cnames and arrays of those
		// add more if really needed

		const auto typeEnum = type->GetType();
		if ( typeEnum == RT_Fundamental || typeEnum == RT_Enum )
			return true;

		if ( typeEnum == RT_Name && type->GetName() == ::GetTypeName< CName > () )
			return true;

		if (	type->GetName() == RED_NAME_CONSTEXPR_NOREG( "entEntityID" ) 
			||	type->GetName() == RED_NAME_CONSTEXPR_NOREG( "gamePersistentID" )
			||	type->GetName() == RED_NAME_CONSTEXPR_NOREG( "NodeRef" )
			||	type->GetName() == RED_NAME_CONSTEXPR_NOREG( "TweakDBID" ) )
		{
			return true;
		}

		if ( typeEnum == RT_Array || typeEnum == RT_StaticArray || typeEnum == RT_NativeArray )
		{
			const auto* arrayType = static_cast< const rtti::IBaseArrayType* > ( type );
			const auto* innerType = arrayType->ArrayGetInnerType();
			
			return IsPersistentTypeSupported( innerType );
		}

		if ( typeEnum == RT_Class || typeEnum == RT_Handle )
			return true; // properties of this class are checked when built

		return false;
	}

	rtti::PropertyBuilder& PropertyBuilder::persistent()
	{
		RED_FATAL_ASSERT(IsPersistentTypeSupported(m_propertyType), "Property type %s is not supported as persistent.", m_propertyType->GetName().AsChar());

		m_flags |= PF_Persistent;
		return *this;
	}

	rtti::PropertyBuilder& PropertyBuilder::optional()
	{
		m_flags |= PF_Optional;
		return *this;
	}

	PropertyBuilder& PropertyBuilder::editable()
	{
		RED_FATAL_ASSERT( ( m_flags & PF_ReadOnly ) == 0, "Can't be set to editable when readOnly flag is set" );
		m_flags |= PF_Editable;
		return *this;
	}

	PropertyBuilder& PropertyBuilder::instanceable()
	{
		m_flags |= PF_Instanceable;
		return *this;
	}

	PropertyBuilder& PropertyBuilder::instanceEditable()
	{
		RED_FATAL_ASSERT((m_flags & PF_ReadOnly) == 0, "Can't be set to editable when readOnly flag is set");
		m_flags |= PF_InstanceEditable;
		return *this;
	}

	PropertyBuilder& PropertyBuilder::browsable( const Bool browsable )
	{
		RED_FATAL_ASSERT( ( m_flags & PF_ReadOnly ) == 0, "Can't be set to editable when readOnly flag is set" );
		RED_FATAL_ASSERT( ( m_flags & PF_Editable ) || ( m_flags & PF_InstanceEditable ) , "Can't be set as browsable when property is not (instance)editable" );
		if ( browsable )
		{
			m_flags |= PF_Browsable;
		}
		else
		{
			m_flags &= ~PF_Browsable;
		}
		return *this;
	}

	// sets readOnly flag
	PropertyBuilder& PropertyBuilder::readOnly()
	{
		RED_FATAL_ASSERT( ( m_flags & PF_Editable ) == 0 || ( m_flags & PF_Inlined ) != 0, "Can't be set to read only when editable flag is set" );
		m_flags |= PF_ReadOnly | PF_Editable;
		return *this;
	}

	// sets inlined and editble flags
	PropertyBuilder& PropertyBuilder::inlined()
	{
		m_flags |= PF_Inlined | PF_Editable;
		return *this;
	}

	// sets notCooked flag
	PropertyBuilder& PropertyBuilder::notCooked()
	{
		m_flags |= PF_NotCooked;
		return *this;
	}

	// sets cookedOnly flag
	PropertyBuilder& PropertyBuilder::cookedOnly()
	{
		m_flags |= PF_CookedOnly;
		return *this;
	}

	// sets notSerialized flag
	PropertyBuilder& PropertyBuilder::notSerialized()
	{
		m_flags |= PF_NotSerialized;
		return *this;
	}

	// sets resizable flag
	PropertyBuilder& PropertyBuilder::resizable()
	{
		m_flags |= PF_Resizable;
		return *this;
	}

	// sets canAdd flag
	PropertyBuilder& PropertyBuilder::canAdd( const Bool enableAdd )
	{
		if ( enableAdd )
		{
			m_flags |= PF_CanAdd;
		}
		else
		{
			m_flags &= ~PF_CanAdd;
		}
		return *this;
	}

	PropertyBuilder& PropertyBuilder::noMask()
	{
		m_flags |= PF_NoMask;
		return *this;
	}

	// sets min size; only usable by dynamic size containers (e.g. dynarray or string); used by replication to optimize bandwidth
	PropertyBuilder& PropertyBuilder::minSize( const Uint32 minSize )
	{
		m_editorInfo.m_minSize = minSize;
		return *this;
	}

	// sets max size; only usable by dynamic size containers (e.g. dynarray or string); used by replication to optimize bandwidth
	PropertyBuilder& PropertyBuilder::maxSize( const Uint32 maxSize )
	{
		m_editorInfo.m_maxSize = maxSize;
		return *this;
	}

	// sets resetOnClear flag
	PropertyBuilder& PropertyBuilder::clearable( Bool enableClear )
	{
		if ( enableClear )
		{
			m_flags |= PF_Clear;
		}
		else
		{
			m_flags &= ~PF_Clear;
		}
		return *this;
	}

	PropertyBuilder& PropertyBuilder::resetable( Bool enableReset )
	{
		if ( enableReset )
		{
			m_flags |= PF_Reset;
		}
		else
		{
			m_flags &= ~PF_Reset;
		}
		return *this;
	}

	PropertyBuilder& PropertyBuilder::selectable( Bool enableSelect )
	{
		if ( enableSelect )
		{
			m_flags |= PF_Select;
		}
		else
		{
			m_flags &= ~PF_Select;
		}
		return *this;
	}

	// sets name of the property, that name should be used in serialization
	PropertyBuilder& PropertyBuilder::setName( CName name )
	{
		m_name = name;
		return *this;
	}

	PropertyBuilder& PropertyBuilder::setName( const char* name )
	{
		m_name = RED_NAME( name );
		return *this;
	}

	// sets range
	PropertyBuilder& PropertyBuilder::range( const Float min, const Float max, Bool hideSlider )
	{
		m_editorInfo.m_min = min;
		m_editorInfo.m_max = max;
		if( hideSlider )
		{
			m_editorInfo.m_editorFlags |= HideSlider;
		}
		return *this;
	}

	// sets range; used by replication to optimize bandwidth
	PropertyBuilder& PropertyBuilder::range( const Int32 min, const Int32 max, Bool hideSlider )
	{
		m_editorInfo.m_min = ( Float ) min;
		m_editorInfo.m_max = ( Float ) max;
		if( hideSlider )
		{
			m_editorInfo.m_editorFlags |= HideSlider;
		}		
		return *this;
	}

	PropertyBuilder& PropertyBuilder::curveXRange( const Float min, const Float max )
	{
		RED_FATAL_ASSERT( m_propertyType->GetType() == RT_LegacySingleChannelCurve,
			"This property modifier can be used only on curve type (property '%s' in class '%s')",
			m_name.AsChar(), m_parentClass ? m_parentClass->GetName().AsChar() : "<unknown>" );

		m_editorInfo.m_curveXMin = min;
		m_editorInfo.m_curveXMax = max;
		return *this;
	}

	// sets float precision; used by replication to compress value better and determine if value needs to be replicated
	PropertyBuilder& PropertyBuilder::precision( const Float precision )
	{
		m_editorInfo.m_precision = precision;
		return *this;
	}

	

	// makes property replicated; specify optional custom replication type if you want to override default replication behavior
	PropertyBuilder& PropertyBuilder::replicated( const rep::Type* customRepType  )
	{
		RED_ASSERT( !m_replicationInfo.m_isReplicated, "Property already marked as replicated." );
		m_replicationInfo.m_isReplicated = true;
		if ( customRepType )
		{
			m_replicationInfo.m_repType = customRepType;
		}
		return *this;
	}

	// allows to specify who is this property replicated to
	PropertyBuilder& PropertyBuilder::replicateTo( const rep::ReplicationTarget replicationTarget )
	{
		RED_ASSERT( m_replicationInfo.m_isReplicated, "Property needs to be marked as replicated() first." );
		m_replicationInfo.m_replicationTarget = replicationTarget;
		return *this;
	}

	PropertyBuilder& PropertyBuilder::replicateAsMapArray( const CName keyMemberName )
	{
		m_replicationInfo.m_arrayReplicationMode = rep::ArrayReplicationMode::AsMap;
		m_replicationInfo.m_replicateAsMapArrayKeyName = keyMemberName;

		// If there's no property type, then replicated type is already there (determined based on template logic)
		// In that case, figure out desired replicated type now

		if ( !m_propertyType )
		{
			m_replicationInfo.m_repType = rep::IRTTIService::GetInstance().GetCorrespondingMapArrayType( m_replicationInfo.m_repType, m_replicationInfo.m_replicateAsMapArrayKeyName );
			RED_ASSERT( m_replicationInfo.m_repType );
		}

		return *this;
	}

	PropertyBuilder& PropertyBuilder::replicateAsSimpleArray()
	{
		m_replicationInfo.m_arrayReplicationMode = rep::ArrayReplicationMode::Simple;

		// If there's no property type, then replicated type is already there (determined based on template logic)
		// In that case, figure out desired replicated type now

		if ( !m_propertyType )
		{
			m_replicationInfo.m_repType = rep::IRTTIService::GetInstance().GetCorrespondingSimpleArrayType( m_replicationInfo.m_repType );
			RED_ASSERT( m_replicationInfo.m_repType );
		}
		return *this;
	}

	// assumes property never changes; used by replication to optimize bandwidth; defaults to false
	PropertyBuilder& PropertyBuilder::neverChanges()
	{
		m_replicationInfo.m_neverChanges = true;
		return *this;
	}

	// log every change on this property value
	PropertyBuilder& PropertyBuilder::logChanges()
	{
		m_replicationInfo.m_logChanges = true;
		return *this;
	}
	

	// marks that this property is replicated as a reference object (as opposed to inlined object); only usable with pointer/handle types; defaults to false
	PropertyBuilder& PropertyBuilder::replicateAsReference()
	{
		RED_ASSERT( m_replicationInfo.m_isReplicated, "Property needs to be marked as replicated() first." );
		m_replicationInfo.m_isReplicatedInline = false;
		return *this;
	}

	PropertyBuilder& PropertyBuilder::tag( const CName propertyTag )
	{
		m_replicationInfo.m_tag = propertyTag;
		return *this;
	}

	PropertyBuilder& PropertyBuilder::invalidateOnChange()
	{
		RED_FATAL_ASSERT( ( m_editorInfo.m_editorFlags & InvalidateOnChange ) == 0, "Already set" );
		RED_FATAL_ASSERT( ( m_editorInfo.m_editorFlags & RepaintOnChange ) == 0, "Can't be set to InvalidateOnChange when RepaintOnChange flag is set" );
		m_editorInfo.m_editorFlags |= InvalidateOnChange;
		return *this;
	}

	PropertyBuilder& PropertyBuilder::repaintOnChange()
	{
		RED_FATAL_ASSERT( ( m_editorInfo.m_editorFlags & RepaintOnChange ) == 0, "Already set" );
		RED_FATAL_ASSERT( ( m_editorInfo.m_editorFlags & InvalidateOnChange ) == 0, "Can't be set to RepaintOnChange when InvalidateOnChange flag is set" );
		m_editorInfo.m_editorFlags |= RepaintOnChange;
		return *this;
	}

	PropertyBuilder& PropertyBuilder::inheritValueFromParent()
	{
		RED_FATAL_ASSERT( ( m_editorInfo.m_editorFlags & InheritValueFromParent ) == 0, "Already set" );
		m_editorInfo.m_editorFlags |= InheritValueFromParent;
		return *this;
	}

} // rtti
