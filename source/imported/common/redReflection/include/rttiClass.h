/**
* Copyright (c) 2007-2019 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "rttiType.h"
#include "eventTypes.h"
//#include "../../redSystem/include/readWriteSpinLock.h"

/// Class info flags
enum EClassFlags
{
	CF_Abstract							= RED_FLAG( 0 ),		//!< Class is abstract, no instance of it can be created
	CF_Native							= RED_FLAG( 1 ),		//!< Class is defined in C++
	CF_ScriptedClass					= RED_FLAG( 2 ),		//!< Class has definition in script
	CF_ScriptedStruct					= RED_FLAG( 3 ),		//!< Class is actually a struct defined in scripts
	CF_NoDefaultObjectSerialization		= RED_FLAG( 4 ),		//!< Don't compare properties to default object on serialize
	CF_AlwaysTransient					= RED_FLAG( 5 ),		//!< NEVER save or load objects of this class to ANY storage
	CF_ImportOnly						= RED_FLAG( 6 ),		//!< This class cannot cannot have "non-imported" scripted functions and properties
	CF_Private							= RED_FLAG( 7 ),		//!< Class is private
	CF_Protected						= RED_FLAG( 8 ),		//!< Class is protected
	CF_TestOnly							= RED_FLAG( 9 ),		//!< This class is test only and will be removed from "shipping" version of the game
	CF_Savable							= RED_FLAG( 10 ),		//!< Objects of this class can be stored in a savegame
};

namespace text
{
	class ITextWriter;
	class ITextReader;
}

namespace rep
{
	class Class;
}

namespace red
{
	class Event;
	class EventConnectorCollector;
}

/// Forward declaration
class CName;
class ISerializable;
class IScriptable;

template< typename T >
class THandle;

namespace rtti
{
	class ITypeSystem;
	class Property;
	class Function;
	class ScriptedClassType;
	class AbstractClassType;
	class Variant;
	class IFunctionCollector;
	enum class CustomEditorMode : Uint8;

	/// Editable class properties - they are a little bit abstracted because there may be may sources
	struct RED_REFLECTION_API ClassEditablePropertyInfo
	{
		const rtti::IType*		m_type;
		CName					m_name;
		CName					m_category;
		CName					m_customTypeName;
		Float					m_minValue;
		Float					m_maxValue;
		Float					m_xMinValue;
		Float					m_xMaxValue;
		Uint32					m_minSize;
		Uint32					m_maxSize;
		Bool					m_canClear;
		Bool					m_canReset;
		Bool					m_canSelect;
		Bool					m_isInstanceEditable;
		Bool					m_isReadOnly;
		Bool					m_isInlined;
		Bool					m_isResizable;
		Bool					m_canAdd;
		Bool					m_isMask;
		Bool					m_hideSlider;
		Bool					m_isBrowsable;
		CustomEditorMode		m_customTypeMode;
		String					m_tooltip;		
		String					m_keyPath;

		ClassEditablePropertyInfo();
		~ClassEditablePropertyInfo();
		ClassEditablePropertyInfo  & operator=( const ClassEditablePropertyInfo& );
		ClassEditablePropertyInfo( const ClassEditablePropertyInfo & );
	};

	// Wrapper for an array of ClassEditablePropertyInfos to prevent name duplicates
	class RED_REFLECTION_API EditableProperties final
	{
	public:
		EditableProperties();
		virtual ~EditableProperties();

		RED_INLINE const red::DynArray< ClassEditablePropertyInfo >& Get() const { return m_properties; }

		void Add( const ClassEditablePropertyInfo& propertyInfo );
		void Add( const red::DynArray< rtti::ClassEditablePropertyInfo >& props );
		void InsertAt( const Uint32 index, const ClassEditablePropertyInfo& propertyInfo );

		Bool RemoveAll( CName propertyName );
		Bool RemoveIf( red::FixedSizeFunction< Bool( const ClassEditablePropertyInfo& ) > pred );
		Bool RemoveAt( const Uint32 index );

		void Modify( red::FixedSizeFunction< void( ClassEditablePropertyInfo& ) > pred );

		void Clear();

		const rtti::ClassEditablePropertyInfo* Find( CName name );
		Uint32 IndexOf( CName name ) const;

		void OverrideProperty( const rtti::ClassEditablePropertyInfo& info );

	private:
		red::DynArray< ClassEditablePropertyInfo > m_properties;
	};

	RED_REFLECTION_API Bool AreTypesSerializationCompatible( const rtti::IType* originalType, const rtti::IType* currentType );

	/// Class definition
	class RED_REFLECTION_API ClassType : public rtti::IType
	{
	public:
		struct PropInfo
		{
			const rtti::IType*		m_type;
			Uint32					m_offset;
		};

		struct PropOverride
		{
			rtti::Property* m_propertyOverride;
			Uint32 m_overrideMask;                //< Which properties are actually overridden?
		};

		typedef red::DynArray< const rtti::Property* > TPropertyList;
		typedef red::DynArray< PropOverride > TPropertyOverrideList;
		typedef red::DynArray< PropInfo > TDestroyPropList;
		typedef red::DynArray< const rtti::Function* > TFunctionList;
		typedef red::Map< CName, rtti::Variant* > TDefaultValues;


		ClassType( const CName name, Uint32 size, Uint32 flags );
		virtual ~ClassType();
	
		//////////////////////////////////////////////////////////////////////////
		// accessor section

		//! Get the n-th base class
		RED_INLINE const rtti::ClassType* GetBaseClass() const { return m_baseClass ; }

		//! Do we have base class ?
		RED_INLINE Bool HasBaseClass() const { return (m_baseClass != nullptr); }
		
		//! Get flags
		RED_INLINE Uint32 GetFlags() const { return m_flags; }

		//! Is this class abstract
		RED_INLINE Bool IsAbstract() const { return ( m_flags & CF_Abstract ) != 0; }

		//! Is this class scripted?
		RED_INLINE Bool IsScriptedClass() const { return ( m_flags & CF_ScriptedClass ) != 0; }

		//! Is this struct in scripts?
		RED_INLINE Bool IsScriptedStruct() const { return ( m_flags & CF_ScriptedStruct ) != 0; }

		//! Is this either scripted class or struct?
		RED_INLINE Bool IsScriptedType() const { return IsScriptedClass() || IsScriptedStruct(); }

		//! Is this a native class ( C++ ) ?
		RED_INLINE Bool IsNative() const { return ( m_flags & CF_Native ) != 0; }

		//! Is this class always transient ?
		RED_INLINE Bool IsAlwaysTransient() const { return ( m_flags & CF_AlwaysTransient ) != 0; }

		//! Do we have to compare properties to default object on serialize
		RED_INLINE Bool IsDefaultObjectSerialization() const { return ( m_flags & CF_NoDefaultObjectSerialization ) == 0; }	

		//! Is this "import only"? (cannot have "non-imported" scripted functions and properties)
		RED_INLINE Bool IsImportOnly() const { return ( m_flags & CF_ImportOnly ) != 0; }

		//! Is this "test only"? (test only classes will be removed from "shipping" version of the game)
		RED_INLINE Bool IsTestOnly() const { return ( m_flags & CF_TestOnly ) != 0; }

		//! Is this a private class ( script )
		RED_INLINE Bool IsPrivate() const { return ( m_flags & CF_Private ) != 0; }

		//! Is this a protected class ( script )
		RED_INLINE Bool IsProtected() const { return ( m_flags & CF_Protected ) != 0; }

		//! Is this a protected class ( script )
		RED_INLINE Bool IsSavable() const { return ( m_flags & CF_Savable ) != 0; }

		//! Get size of script data for this class
		RED_INLINE Uint32 GetScriptDataSize() const { return m_scriptSize; }

		//! Get class user data pointer
		RED_INLINE void* GetUserData() const { return m_userData; }

		//! Set class user data
		RED_INLINE void SetUserData( void* userData ) const { m_userData = userData; } // mutable.. same hack as with the SetUserClassIndex(). Not proud of it :(

		//! Get the user class index 
		RED_INLINE Uint32 GetUserClassIndex() const { return m_userClassIndex; }

		//! Set the user class index
		RED_INLINE void SetUserClassIndex(const Uint32 index) const { m_userClassIndex = index; }

		//! Change class flag
		void SetFlag( Uint32 flag, Bool isOn );

		virtual const red::memory::Pool & GetInnerTypeMemoryPool() const override;
		
		const ClassType* GetFirstNativeBaseClass() const;

		static const ClassType* FindCommonBase( const rtti::ClassType* typeA, const rtti::ClassType* typeB );

		//////////////////////////////////////////////////////////////////////////
		// Casting section

		template< class _Type > 
		Bool IsA() const;

		Bool IsA( const rtti::ClassType *otherClass ) const;

		// cast up the hierarchy - you have object 'obj', of class 'this' and want to cast it to class 'destClass'
		void* CastTo( const rtti::ClassType *destClass, void *obj ) const;

		// cast down the hierarchy - you have object 'obj', of class 'this' (you can always get it, GetClass is virtual when necessary)
		// but you point it with pointer to 'srcClass'
		void* CastFrom( const rtti::ClassType *srcClass, void *obj ) const;

		// cast up the hierarchy - you have object 'obj', of class 'this' and want to cast it to class 'destClass'
		template< class _DestType > 
		_DestType*	CastTo( void *obj ) const;

		//! Is this an ISerializable class (a lot faster than IsA< ISerializable >() )
		Bool IsSerializable() const;


		//////////////////////////////////////////////////////////////////////////
		// class builder section

		template< class _CurrentType, class _ParentType >
		void AddParentClass();

		void AddParentClass( const rtti::ClassType* baseClass );

		void AddProperty( rtti::Property *property );

		void AddPropertyOverride( rtti::Property *property, Uint32 overrideMask );

		void AddFunction( const rtti::Function* function );
		void AddStaticFunction( const rtti::Function* function );

		void AddNativeFunction( const CName funcName, TNativeFunc funcPtr, Uint32 flags = 0 );
		void AddNativeStaticFunction( const CName funcName, TNativeGlobalFunc funcPtr, Uint32 flags = 0 );

		void ClearScriptData();

		void SetAlignment( const Uint32 alignment );

		void SetDescriptiveName( const red::String& humanReadableName );

		const char* GetDescriptiveName() const;

		void SetCustomEditorName( const CName customEditorName );
		CName GetCustomEditorName() const;

		// mark class as fully initialized
		void MarkAsInitialized();

		//////////////////////////////////////////////////////////////////////////
		// property section

		const rtti::Property* FindProperty( const CName name ) const;

		const rtti::Property* FindPropertyOverride( const CName name ) const;

		const rtti::Function* FindFunction( const CName name ) const;

		const rtti::Function* FindLocalFunction( const CName name ) const;

		const rtti::Function* FindFunctionNonCached( const CName name ) const;

		const rtti::Function* FindFunctionByHash( Uint64 hash ) const; 

		// Finds first family name matching function; starting from lowest class in a hierarchy
		// Note: This function is highly inefficient due to linear search across all classes in hierarchy chain
		const rtti::Function* FindFunctionByFamilyName( const CName familyName ) const;

		void GetProperties( TPropertyList &properties ) const;

		void GetPersistentProperties( TPropertyList &properties ) const;
		
		// get editable properties form class
		void GetEditableProperties( EditableProperties& outEditableProperties ) const;

		void GetInstanceEditableProperties( EditableProperties& outEditableProperties ) const;

		template < class _F >
		void IterateProperties( _F& f ) const;

		const TPropertyList& GetCachedProperties( ) const;

		const TPropertyList& GetCachedPersistentProperties( ) const;

		const TFunctionList& GetLocalFunctions( ) const { return m_localFunctions; }

		const TPropertyList& GetLocalProperties() const { return m_localProperties; }

		RED_INLINE const TPropertyOverrideList& GetPropertyOverrides() const;

		RED_INLINE Bool HasPropertyOverrides() const;

		void RecalculateCachedProperties( const Bool force = false ) const;

		void RecalculateCachedPersistentProperties( const Bool force = false ) const;

		void RecalculateCachedScriptPropertiesToDestroy();

		void RecalculateAllCachedData();

		void EnumFunctionsFromFamily( rtti::IFunctionCollector& collector ) const;

		// Resolve all property overrides. They can override some attributes of some properties from one (or more) of the base classes.
		void ResolvePropertyOverrides( const Bool force = false ) const;

		void RebuildParentHierarchy( void* object, ISerializable * parent ) const override final;

		//////////////////////////////////////////////////////////////////////////
		// script section

		void ReuseScriptStub( Uint32 flags );

		void InitializeScriptedProperties( void* buffer ) const;
		void InitializeScriptDefaultValues( void* buffer ) const;

		virtual void RecalculateClassDataSize();

		virtual Uint32 CalcScriptClassAlignment() const;

		virtual Bool ValidateLayout() const;

		Bool AddDefaultValue( CName propertyName, const red::String& value );

		//////////////////////////////////////////////////////////////////////////
		// Interface section

		virtual const CName	GetName() const override final;
	
		virtual Uint32 GetAlignment() const override final;

		virtual Uint32	GetSize() const override final;

		virtual ERTTITypeType GetType() const override final;

		virtual CName GetRefName() const override final;

		virtual void Construct( void *mem ) const override final;
		
		virtual void Destruct( void *mem ) const override final;

		virtual Bool Serialize( IFile& file, void* data, ISerializable *owner = nullptr ) const;

		virtual const Bool SerializeToText( text::ITextWriter& writer, const void* data ) const override;

		virtual const Bool SerializeFromText( text::ITextReader& reader, void* data ) const override;

		virtual Bool NeedsCleaning() const override;

		//////////////////////////////////////////////////////////////////////////
		// Creation section

		// allocate memory buffer for object, sets the m_class field for scripted objects, can optionally call the contructor (via Construct)
		void* CreateObject( Uint32 sizeCheck, bool wipeMemory = false ) const;

		// free object memory buffer, calls the destructor (via Destruct)
		void DestroyObject( void *mem ) const;

		// create object - allocated memory (via native CreateObject) and calls constructor
		template< class _Type > 
		_Type* CreateObject()  const;

		// create handle with object - allocated memory (via native CreateObject) and calls constructor
		template< class _Type > 
		THandle< _Type > CreateHandle()  const;

		// delete object - calls destructor and frees memory (via DestroyObjectMemory)
		template< class _Type > 
		void DestroyObject( _Type *obj ) const;

		template< class _Type > _Type* GetDefaultObject() const;
		void * GetDefaultObject() const;

		//////////////////////////////////////////////////////////////////////////
		// Serialization section	
	
		Bool DeepCompare( const void* data1, const void* data2, Uint32 flags = 0 ) const;

		Bool DefaultSerialize( IFile& file, void* data, ISerializable *owner ) const;

		const Bool DefaultSerializeToText( text::ITextWriter& writer, const void* data ) const;
	
		const Bool DefaultSerializeFromText( text::ITextReader& reader, void* data ) const;

		virtual Bool ToString( const void* object, String& valueString ) const;

		virtual Bool FromString( void* object, const String& valueString ) const;

		const Bool ReadDirectValue( IRTTIContext& ctx, const void* data, rtti::ValuePtr& outValue ) const;

		const Bool WriteDirectValue( IRTTIContext& ctx, void* data, const rtti::ValueHolder& value, bool clone ) const;

		virtual const Bool ReadValue( IRTTIContext& ctx, const void* data, const rtti::AccessPath& path, rtti::ValuePtr& outValue ) const override final;

		virtual const Bool WriteValue( IRTTIContext& ctx, void* data, const rtti::AccessPath& path, const rtti::ValueHolder& newValue, bool clone ) const override final;

		virtual Bool IsPropertyReadOnly( IRTTIContext& ctx, const rtti::AccessPath& path, Bool& outReadOnly ) const override final;

		//! Serialize a differential object data - requires a valid object template (or NULL).
		//! Serialization can be done against _ANY_ template now as long as it can be (kind of) restored back.
		//! By default the serialization is and should be done against the class's default object data (which is a constructor's default values)
		//! Name of the method was changed to reflect the slight change in the functionality.
		Bool SerializeDiff( ISerializable* owner, IFile& file, void* data, const void* defaultData, const rtti::ClassType* defaultDataClass ) const;

		// Get corresponding replicated class type
		RED_INLINE const rep::Class* GetReplicatedClassType() const { return ( const rep::Class* ) GetDefaultReplicatedType(); }
		

		//////////////////////////////////////////////////////////////////////////
		// Event Section	

		bool CanServiceEvent( const ClassType* eventType ) const;
		bool CanServiceEvents() const;

		Uint16 GetEventClassId() const; // ctremblay: HACK No good metadata for ClassType. One class to rule them all currently.

		void Internal_RegisterEventConnector( CName functionName, const ClassType* eventType, const red::EventConnector& connector );
		void Internal_RegisterScriptedEventConnectors();

		void Internal_CollectEventConnector( red::EventConnectorCollector & collector ) const;

#ifdef USE_PROFILER
		red::InstrumentationObject & GetInstrumentationObject() const;
#endif

	protected:

		void InternalSetSize( Uint32 size );
		void DestroyProperties( void * buffer ) const;

	private:
		
		typedef red::HashMap< CName, const rtti::Function* > TFunctionMap;
		
		// handle serialization flags saving/loading
		void HandleSerializationFlags( IFile& file, Uint8& flags ) const;

		// serialization functions for normal stuff
		Bool WritePropertyList( ISerializable* owner, IFile& file, void* data, const void* defaultData, const rtti::ClassType* defaultDataClass ) const;
		Bool ReadPropertyList( ISerializable* owner, IFile& file, void* data ) const;

		virtual void OnConstruct( void * buffer ) const = 0;
		virtual void OnDestruct( void * buffer ) const = 0;

		void CreateDefaultObject() const;

		void GetEditableProperties( Uint32 flags, EditableProperties& outEditableProperties) const;

		// ctremblay: the only reason why I let derived object allocate buffer is to be able to track them in Memory Profiler. Else it's almost impossible to find which object is leaked.
		virtual void * AllocateClassBuffer() const = 0; 

		void RecalculateCachedFunction();

		const ClassType*				m_baseClass;							//!< Base class + casting functions
		
		CName							m_name;									//!< Name of the class
		CName							m_refName;
		TPropertyList					m_localProperties;						//!< Class local properties
		TPropertyOverrideList			m_propertyOverrides;					//!< Local overrides of base-class properties.
		TFunctionList					m_localFunctions;						//!< Native & script functions defined in this class
		TFunctionList					m_localStaticFunctions;					//!< Native & script static functions defined in this class
		Uint32							m_size;									//!< C++ Size of class
		Uint32							m_scriptSize;							//!< Script Size of class ( 0 for native classes )
		Uint32							m_flags;								//!< Class flags	
		Uint32							m_alignment;							//!< Native (c++) alignment of the class itself
	
		red::HashMap< CName, const rtti::Function* > m_cachedFunctionByName;
		red::HashMap< Uint64, const rtti::Function* > m_cachedFunctionByHash;

		mutable void*					m_userData;								//!< Generic user data pointer (used by some systems)
		mutable void* 					m_defaultObject;

		mutable red::HashMap< CName, const rtti::Property* > m_cachedPropertyDictionary;
		mutable TPropertyList			m_cachedProperties;						//!< Cached properties of class
		mutable TPropertyList			m_cachedPersistentProperties;			//!< Cached persistent properties of class
		mutable TPropertyList			m_propertiesToDestroy;					//!< Explicit list of properties we need to call destroy on
		TDestroyPropList				m_cachedSecondaryPropertiesToDestory;	//!< Cached script properties that should be destroyed with the class
		TDefaultValues					m_defaultValues;						//!< Default values of scripted properties

		static const Uint32 c_maxEventType = 2048;

		struct Connector
		{
            red::EventConnector connector;
            CName functionName;
            Uint16 eventId;
			bool scriptedConnector;
		};

		red::DynArray< Connector > m_eventConnector;
		red::BitSet< c_maxEventType > m_supportedEventMask;
		mutable atomic::TAtomic16 m_eventClassId; // ctremblay: Used by Event System. HACK.


		mutable Uint32					m_userClassIndex;						// ctremblay: Only use by legacy Event Class. Quite an Hack.
		mutable red::RWSpinLock			m_cachedPropertiesLock; // ctremblay: this is not silver bullet. Be super careful when manipulating cached properties.

		mutable Uint8					m_arePropertiesCached:1;				//!< Is property cache (only of current class) up to date
		mutable Uint8					m_arePersistentPropertiesCached:1;		//!< Is persistent property cache (only of current class) up to date
		mutable Uint8					m_areSecondaryPropertiesToDestroyCached:1;	//!< Is the list of script properties to destroy cached (only of current class) up to date
		mutable Uint8					m_arePropertyOverridesResolved : 1;		//!< Are property overrides resolved (with base classes)?
		mutable Uint8					m_isFullyInitialized:1;					//!< Is this class fully initialized (have we finished the InitClass in class builder)
					
#ifndef RED_CONFIGURATION_FINAL
		String							m_descriptiveName;						//!< Optional human-readable name of the class
		CName m_customEditorName;
#endif //RED_CONFIGURATION_FINAL

#ifdef USE_PROFILER
		mutable red::InstrumentationObject m_instrumentationObject;
#endif
};

} // rtti

#include "rttiClass.inl"
