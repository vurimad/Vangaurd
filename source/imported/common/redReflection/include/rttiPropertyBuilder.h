/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

namespace rep
{
	class Type;

	// Determines who the property is replicated to
	enum class ReplicationTarget : Uint8
	{
		// Replicated to all peers - default value
		All = 0,
		// Only replicated to peer who controls parent object
		ControllingPeer,	
		// Replicated to all peers who don't control parent object
		NonControllingPeers
	};

	// (Static/dynamic) array replication modes
	enum class ArrayReplicationMode
	{
		// Generic mode that doesn't know anything about array "structure logic"; heavy on CPU but produces good diff; prefer other modes where possible because of CPU cost
		Generic,
		// Simple and efficient (CPU wise) mode where elements are compared based on their indices; may not produce best diff when elements relocate
		Simple,
		// Assumes all elements have unique key which helps optimize differential compression
		// Note: use replicateAsMapArray( keyMemberName ) to activate
		AsMap,

		Default = Simple
	};

}

namespace rtti
{
	class Property;
	class ClassType;
	class IType;

	/// Mode used for custom editor for RTTI properties
	/// This allows us to make custom editor for inner types of array properties (like DynArray< T > and we want to make T custom).
	enum class CustomEditorMode : Uint8
	{
		NormalType = 0,
		InnerType  = 1            ///< Used for arrays.
	};

	struct PropertyBuilderParameter
	{
		const rtti::ClassType* parentClass;
		Uint64 propertyId;
		Uint64 offset;
		CName name;
		CName category;
	};

	class RED_REFLECTION_API PropertyBuilder : red::NonCopyable
	{
		RED_USE_MEMORY_POOL( red::PoolRTTIProperty );

	public:
		PropertyBuilder( 
			const rtti::ClassType* parentClass, 
			size_t offset, 
			CName name, 
			CName category, 
			const rtti::IType* propertyType,
			const rep::Type* replicatedPropertyType = nullptr );

		~PropertyBuilder();
		
		// sets editable flag
		PropertyBuilder& editable();

		PropertyBuilder& instanceable();

		PropertyBuilder& instanceEditable();

		PropertyBuilder& browsable( const Bool browsable );

		// sets readOnly flag
		PropertyBuilder& readOnly();

		// sets inlined and editble flags
		PropertyBuilder& inlined();

		// sets notCooked flag
		PropertyBuilder& notCooked();

		// sets cookedOnly flag
		PropertyBuilder& cookedOnly();

		// sets notSerialized flag
		PropertyBuilder& notSerialized();

		// sets resizable flag
		PropertyBuilder& resizable();

		PropertyBuilder& canAdd( const Bool enableAdd );

		PropertyBuilder& noMask();

		PropertyBuilder& persistent();

		// Sets optional flag
		PropertyBuilder& optional();

		// sets min size; only usable by dynamic size containers (e.g. dynarray or string); used by replication to optimize bandwidth
		PropertyBuilder& minSize( const Uint32 minSize );

		// sets max size; only usable by dynamic size containers (e.g. dynarray or string); used by replication to optimize bandwidth
		PropertyBuilder& maxSize( const Uint32 maxSize );

		// sets resetOnClear flag
		PropertyBuilder& clearable( Bool enableClear );

		PropertyBuilder& resetable( Bool enableReset );

		PropertyBuilder& selectable( Bool enableSelect );

		// sets name of the property, that name should be used in serialization
		PropertyBuilder& setName( CName name );

		PropertyBuilder& setName( const char* name );

		// sets range
		PropertyBuilder& range( const Float min, const Float max, Bool hideSlider = false );

		// sets range; used by replication to optimize bandwidth
		PropertyBuilder& range( const Int32 min, const Int32 max, Bool hideSlider = false );

		// sets range for curve's x axis
		PropertyBuilder& curveXRange( const Float min, const Float max );

		// sets float precision; used by replication to compress value better and determine if value needs to be replicated
		PropertyBuilder& precision( const Float precision );

		// sets custom editor name if the default editor shouldn't be used
		PropertyBuilder& customEditor( const char* editorName, CustomEditorMode editorMode = CustomEditorMode::NormalType );

		// A shortcut for customEditor( editorName, CustomEditorMode::InnerType )
		PropertyBuilder& customInnerTypeEditor( const char* editorName );

		// makes property replicated; specify optional custom replication type if you want to override default replication behavior
		PropertyBuilder& replicated( const rep::Type* customRepType = nullptr );

		// allows to specify who is this property replicated to
		PropertyBuilder& replicateTo( const rep::ReplicationTarget replicationTarget );

		// hints replication that it should replicate this (array property) as a map; keyMemberName is the name of member that shall be used to uniquely identify array elements
		PropertyBuilder& replicateAsMapArray( const CName keyMemberName );

		// hints replication that it should replicate this (array property) as a simple array; simple means it will assume elements don't relocate and will thus get diffed based on their indices
		PropertyBuilder& replicateAsSimpleArray();

		// assumes property never changes; used by replication to optimize bandwidth; defaults to false
		PropertyBuilder& neverChanges();

		// log every change on this property value
		PropertyBuilder& logChanges();

		// marks that this property is replicated as a reference object (as opposed to inlined object); only usable with pointer/handle types; defaults to false
		PropertyBuilder& replicateAsReference();

		// optional tag used to mark this property for logic; only available in multiplayer; only one tag can be specified;
		PropertyBuilder& tag( const CName propertyTag );

		// helps user better understand the context and the purpose of this property
		PropertyBuilder& tooltip( const char* text );

		PropertyBuilder& keyPath( const char* path );

		//////////////////////////////////////////////////////////////////////////
		// editor side metadata, TODO: we need to find a way to separate this stuff
		// for details look at corresponding getter methods in rttiProperty.h

		PropertyBuilder& invalidateOnChange();
		PropertyBuilder& repaintOnChange();
		PropertyBuilder& inheritValueFromParent();

		//////////////////////////////////////////////////////////////////////////

		// internal (used by RTTI macros); hints property builder that property type is weak entity handle (knowledge needed by replication at initialization time)
		void _HintIsWeakEntityHandle( const Bool isWeakEntityHandle );
		// internal (used by RTTI macros); hints property builder that property type is weak entity component handle (knowledge needed by replication at initialization time)
		void _HintIsWeakEntityComponentHandle( const Bool isWeakEntityComponentHandle );

		void AddPropertyToClass();

		static Bool IsPersistentTypeSupported( const rtti::IType* type );

	private:
		void DetermineReplicatedType();
		void AddReplicatedPropertyToClass( Property* coreProperty );

		CName					m_name;				//!< name of the property
		CName					m_category;			//!< category

		const ClassType*		m_parentClass;		//!< class that the property is in
		const rtti::IType*		m_propertyType;		//!< property type in rtti system

		size_t					m_offset;			// property address offset in the class

		Uint64					m_flags;			// property flags

		struct EditorInfo
		{
			//!< tooltip for the editor
			String m_tooltip;

			//!< custom editor name
			String m_customEditor;

			//!< mode used for custom editor
			CustomEditorMode m_customEditorMode;

			// min range value
			Float m_min;

			// max range value
			Float m_max;

			// float precision
			Float m_precision;

			// min dynamic array size
			Uint32 m_minSize;

			// max dynamic array size
			Uint32 m_maxSize;

			// min X-axis range value
			Float m_curveXMin;

			// max X-axis range value
			Float m_curveXMax;

			// editor side flags
			Uint32 m_editorFlags;

			// 
			String m_keyPath;
		};

		EditorInfo m_editorInfo;

		struct ReplicationInfo
		{
			// Replicated type
			const rep::Type* m_repType;

			// is replicated?
			Bool m_isReplicated : 1;

			// when true, property is assumed to never change (thus updates won't be replicated)
			Bool m_neverChanges : 1;

			// when true, property value changes get logged
			Bool m_logChanges : 1;

			// replicated as an inline object? only valid with pointer/handle types
			Bool m_isReplicatedInline : 1;

			// is this property a weak handle to an entity?
			Bool m_isWeakEntityHandle : 1;
			// is this property a weak handle to an entity component?
			Bool m_isWeakEntityComponentHandle : 1;

			// only used for arrays
			rep::ArrayReplicationMode m_arrayReplicationMode;
			// name of the key member to be used to uniquely identify replicated array elements; only used when replicated as map array
			CName m_replicateAsMapArrayKeyName;

			// who the property is replicated to
			rep::ReplicationTarget m_replicationTarget;

			// The logic tag of this property.
			CName m_tag;

			ReplicationInfo( const rep::Type* replicatedPropertyType );
		};

		ReplicationInfo m_replicationInfo;
	};


	// sets custom editor name if the default editor shouldn't be used
	RED_INLINE PropertyBuilder& PropertyBuilder::customEditor( const char* editorName, CustomEditorMode editorMode )
	{
#ifndef NO_EDITOR
		m_editorInfo.m_customEditor = editorName;
		m_editorInfo.m_customEditorMode = editorMode;
#endif
		return *this;
	}

	// A shortcut for customEditor( editorName, CustomEditorMode::InnerType )
	RED_INLINE PropertyBuilder& PropertyBuilder::customInnerTypeEditor( const char* editorName )
	{
#ifndef NO_EDITOR
		m_editorInfo.m_customEditor = editorName;
		m_editorInfo.m_customEditorMode = CustomEditorMode::InnerType;
#endif
		return *this;
	}

	RED_INLINE PropertyBuilder& PropertyBuilder::tooltip( const char* text )
	{
#ifndef NO_EDITOR
		m_editorInfo.m_tooltip = text;
#endif
		return *this;
	}

	RED_INLINE 	PropertyBuilder& PropertyBuilder::keyPath( const char* path )
	{
#ifndef NO_EDITOR
		m_editorInfo.m_keyPath = path;
#endif
		return *this;
	}

} // rtti
