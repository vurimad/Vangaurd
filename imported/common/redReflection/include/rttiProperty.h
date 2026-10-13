/**
* Copyright (c) 2007-2019 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "rttiCommon.h"
#include "reflectionPool.h"

/// Flags that modify property behavior
enum EPropertyFlags : Uint64
{
	PF_Editable				= RED_FLAG64( 0 ),	//!< Property is visible in the editor's property browser
	PF_ReadOnly				= RED_FLAG64( 1 ),	//!< Property is read only
	PF_Inlined				= RED_FLAG64( 2 ),	//!< Inline property edition ( for object properties only )
	PF_NotSerialized		= RED_FLAG64( 3 ),	//!< Use this flag to grant RTTI access to the field, but prevent serializing it
	PF_NotCooked			= RED_FLAG64( 4 ),	//!< Property with this flag is not cooked to final build packages
	PF_Scripted				= RED_FLAG64( 5 ),	//!< Property is script property
	PF_FuncRetValue			= RED_FLAG64( 6 ),	//!< Return property of function
	PF_FuncParam			= RED_FLAG64( 7 ),	//!< Function parameter
	PF_FuncLocal			= RED_FLAG64( 8 ),	//!< Function local variable
	PF_FuncOutParam			= RED_FLAG64( 9 ),	//!< Function parameter that is passed by reference ( can by modified by function )
	PF_FuncOptionaParam		= RED_FLAG64( 10 ),	//!< Function parameter is optional, does not need to be specified
	PF_FuncSkipParam		= RED_FLAG64( 11 ),	//!< Function parameter which evaluation can be skipped, used in native functions
	PF_Config				= RED_FLAG64( 12 ),	//!< Property is saved/loaded from config
	PF_Unused				= RED_FLAG64( 13 ),
	PF_Native				= RED_FLAG64( 14 ),	//!< Property is defined in C++
	PF_SavedDEPRECATED		= RED_FLAG64( 15 ),	//!< Property that will be saved to a gamesave file (this is not used anywhere anymore, so feel free to use this slot)
	PF_Private				= RED_FLAG64( 16 ),	//!< Property is private
	PF_Protected			= RED_FLAG64( 17 ),	//!< Property is protected
	PF_Public				= RED_FLAG64( 18 ),	//!< Property is public
	PF_AutoBind				= RED_FLAG64( 19 ),	//!< Property is automatically bindable
	PF_AutoBindOptional		= RED_FLAG64( 20 ),	//!< Failed autobind will not result in runtime script errors
	PF_SecondBuffer			= RED_FLAG64( 21 ),	//!< Properties data (and offsets) are relative to the second data buffer
	PF_Resizable			= RED_FLAG64( 22 ),	//!< Property is resizable (applies to DynArrays)
	PF_NoMask				= RED_FLAG64( 23 ),	//!< Bitfield is treated like enum not like a mask
	PF_Reset				= RED_FLAG64( 24 ),	//!< Property is set to its default value on clearing (doesn't accept null as its value - applies to pointers)
	PF_Clear				= RED_FLAG64( 25 ),	//!< Property is set to its default value on clearing (doesn't accept null as its value - applies to pointers)
	PF_Instanceable			= RED_FLAG64( 26 ),   //!< Property is on instance level, not only resource level
	PF_Select				= RED_FLAG64( 27 ),	//!< Property can be selected, i.e. can open a classpicker dialog (only applies to pointers).
	PF_Persistent			= RED_FLAG64( 28 ),   //!< Property is persistent. This works only in classes that supports it.
	PF_Optional				= RED_FLAG64( 29 ),	//!< Property is optional, this is editor only flag based on which editor can display some properties as optional
	PF_CookedOnly			= RED_FLAG64( 30 ),	//!< Property is only written during the cooking process
	PF_CanAdd				= RED_FLAG64( 31 ),	//!< Can add items to the array (applies to DynArrays)
	PF_Browsable			= RED_FLAG64( 32 ),
	PF_Unsavable			= RED_FLAG64( 33 ),
	PF_AccessModifiers		= PF_Private | PF_Protected | PF_Public, 
	PF_InstanceEditable		= PF_Editable | PF_Instanceable,
};

enum EditorMetadata : Uint8
{
	InvalidateOnChange = RED_FLAG( 0 ),
	RepaintOnChange = RED_FLAG( 1 ),
	InheritValueFromParent = RED_FLAG( 2 ),
	HideSlider = RED_FLAG( 3 ), //!< Should slider be hidden for ranged proeprty
};

namespace rtti
{
	class IType;
	class ClassType;
	class EnumType;
	class BitfieldType;
	class ClassType;
	enum class CustomEditorMode : Uint8;

	/// Class property
	class RED_REFLECTION_API Property : red::NonCopyable
	{
		RED_USE_MEMORY_POOL( red::PoolRTTIProperty );

	public:
		//! Get data type
		RED_FORCE_INLINE const rtti::IType* GetType() const { return m_type; }

		//! Get class this property was defined in
		RED_FORCE_INLINE const rtti::ClassType* GetParent() const { return m_parent; }

		//! Get name of the property
		RED_FORCE_INLINE const CName GetName() const { return m_name; }

		//! Get category
		RED_FORCE_INLINE CName GetCategory() const { return m_category; }

		//! Get offset of the property to the data
		RED_FORCE_INLINE Uint32 GetDataOffset() const { return m_offset; }

		//! Get property flags
		RED_FORCE_INLINE Uint64 GetFlags() const { return m_flags; }

		//! Add property flag
		RED_FORCE_INLINE void AddFlag( EPropertyFlags flag ) { m_flags |= flag; }

		//! Is the property editable by the user
		RED_FORCE_INLINE Bool IsEditable() const { return ( m_flags & PF_Editable ) != 0; }

		//! Is the property instanceable by the user
		RED_FORCE_INLINE Bool IsInstanceable() const { return ( m_flags & PF_Instanceable ) != 0; }

		//! Is the property editable on instance level by the user
		RED_FORCE_INLINE Bool IsInstanceEditable() const { return ( m_flags & PF_InstanceEditable ) != 0; }

		//! Is the property defined in C++ code
		RED_FORCE_INLINE Bool IsNative() const { return ( m_flags & PF_Native ) != 0; }

		//! Is the property read only ( user can see the value but cannot modify it )
		RED_FORCE_INLINE Bool IsReadOnly() const { return ( m_flags & PF_ReadOnly ) != 0; }

		//! Is the property inlined ( objects can be created inside parent objects )
		RED_FORCE_INLINE Bool IsInlined() const { return ( m_flags & PF_Inlined ) != 0; }

		//! Should the property be serialized to file
		RED_FORCE_INLINE Bool IsSerializable() const { return ( m_flags & PF_NotSerialized ) == 0; }

		//! Is the property resizable ( is there a possibility to add and remove elements to this property? )
		RED_FORCE_INLINE Bool IsResizable() const { return ( m_flags & PF_Resizable ) != 0; }

		//! Is there a possibility to add elements to this property?
		RED_FORCE_INLINE Bool CanAdd() const { return ( m_flags & PF_CanAdd ) != 0; }

		//! Is this bitfield property treated like a mask or like enum
		RED_FORCE_INLINE Bool IsMask() const { return ( m_flags & PF_NoMask ) == 0; }

		//! Is this property set to its default value on clear
		RED_FORCE_INLINE Bool CanClear() const { return ( m_flags & PF_Clear ) != 0; }

		//! Is this property set to its default value on clear
		RED_FORCE_INLINE Bool CanReset() const { return ( m_flags & PF_Reset ) != 0; }

		RED_FORCE_INLINE Bool CanSelect() const { return ( m_flags & PF_Select ) != 0; }

		//! Is this property saved in cooked builds ?
		RED_FORCE_INLINE Bool IsSerializableInCookedBuilds() const { return ( m_flags & PF_NotCooked ) == 0; }

		RED_FORCE_INLINE Bool IsSerializableInCookedBuildsOnly() const { return (m_flags & PF_CookedOnly) != 0; }

		//! Is this property scripted ?
		RED_FORCE_INLINE Bool IsScripted() const { return ( m_flags & PF_Scripted ) != 0; }

		//! Is this property from function ?
		RED_FORCE_INLINE Bool IsInFunction() const { return ( m_flags & ( PF_FuncLocal | PF_FuncParam ) ) != 0; }

		//! Is this property a function local ?
		RED_FORCE_INLINE Bool IsFuncLocal() const { return ( m_flags & ( PF_FuncLocal ) ) != 0; }

		//! Is this property a function parameter ?
		RED_FORCE_INLINE Bool IsFuncParam() const { return ( m_flags & ( PF_FuncParam ) ) != 0; }

		//! Is the property saved to configuration 
		RED_FORCE_INLINE Bool IsConfig() const { return ( m_flags & PF_Config ) != 0; }

		//! Is the property private
		RED_FORCE_INLINE Bool IsPrivate() const { return ( m_flags & PF_Private ) != 0; }

		//! Is the property protected
		RED_FORCE_INLINE Bool IsProtected() const { return ( m_flags & PF_Protected ) != 0; }

		//! Is the property public
		RED_FORCE_INLINE Bool IsPublic() const { return ( m_flags & PF_Public ) != 0; }

		//! Is the property browsable
		RED_FORCE_INLINE Bool IsBrowsable() const { return ( m_flags & PF_Browsable ) != 0; }

		//! Is this property auto bindable
		RED_FORCE_INLINE Bool IsAutoBindable() const { return ( m_flags & PF_AutoBind ) != 0; }

		//! Can this property fail autobinding without warning messages ?
		RED_FORCE_INLINE Bool IsAutoBindOptional() const { return ( m_flags & PF_AutoBindOptional ) != 0; }

		//! Is this property in secondary data buffer ?
		RED_FORCE_INLINE Bool IsInSecondaryDataBuffer() const { return ( m_flags & PF_SecondBuffer ) != 0; }

		//! Is this property persistent ?
		RED_FORCE_INLINE Bool IsPersistent() const { return ( m_flags & PF_Persistent ) != 0; }

		//! should we ignore the saved value and always use the value from the worldLevel file
		RED_FORCE_INLINE Bool IsUnsavable() const { return ( m_flags & PF_Unsavable ) != 0; }

		//! Is optional (editor only feature)
		RED_FORCE_INLINE Bool IsOptional() const { return ( m_flags & PF_Optional ) != 0; }

#ifndef NO_EDITOR
		//! Is this a ranged property?
		RED_FORCE_INLINE Bool IsRanged() const { return ( m_minValue != m_maxValue ); }

		//! Has this property x axis values constraints?
		RED_FORCE_INLINE Bool HasXAxisConstraints() const { return ( m_xMinValue != m_xMaxValue ); }

		//! Has this property min size constraints?
		RED_FORCE_INLINE Bool HasMinSizeConstraints() const { return m_minSize != 0; }

		//! Has this property max size constraints?
		RED_FORCE_INLINE Bool HasMaxSizeConstraints() const { return m_maxSize != 0; }

		//! Has this property a tooltip?
		RED_FORCE_INLINE Bool HasTooltip() const { return m_tooltip.Length() == 0 ? false : true; }

		//! Does this property have custom editor for it's value ?
		RED_FORCE_INLINE Bool HasCustomEditor() const { return static_cast<Bool>( m_customEditor ); }

		//! Get mode used for custom editor (allows for custom editor for array inner type etc.)
		RED_FORCE_INLINE CustomEditorMode GetCustomEditorMode() const { return m_customEditorMode; }

		//! Get the name of property custom editor
		RED_FORCE_INLINE const CName GetCustomEditorType() const { return m_customEditor; }

		//! look at m_keyPath
		RED_FORCE_INLINE const String& GetKeyPath() const { return m_keyPath; }

		//! Get lower range bound
		RED_FORCE_INLINE Float GetRangeMin() const { return m_minValue; }

		//! Get upper range bound
		RED_FORCE_INLINE Float GetRangeMax() const { return m_maxValue; }

		//! Get lower x range bound
		RED_FORCE_INLINE Float GetXRangeMin() const { return m_xMinValue; }

		//! Get upper x range bound
		RED_FORCE_INLINE Float GetXRangeMax() const { return m_xMaxValue; }

		//! Get min size
		RED_FORCE_INLINE Uint32 GetSizeMin() const { return m_minSize; }

		//! Get max size
		RED_FORCE_INLINE Uint32 GetSizeMax() const { return m_maxSize; }

		RED_FORCE_INLINE const String& GetTooltip() const { return m_tooltip; }

		RED_FORCE_INLINE Bool ShouldHideSlider() const { return( m_editorFlags & HideSlider ) != 0; }


		//! NOTE: Has impact only on widget properties registered on the editor's side, to be separated later
		//! Marked properties will cause layout invalidation when changed
		RED_FORCE_INLINE Bool IsInvalidationRequired() const { return ( m_editorFlags & InvalidateOnChange ) != 0; }

		//! NOTE: Has impact only on widget properties registered on the editor's side, to be separated later
		//! Marked properties will cause paint call when changed
		RED_FORCE_INLINE Bool IsRepaintRequired() const { return ( m_editorFlags & RepaintOnChange ) != 0; }

		//! NOTE: Has impact only on widget properties registered on the editor's side, to be separated later
		//! Marked properties will look for a property value in parent hierarchy before falling back to default
		RED_FORCE_INLINE Bool IsInheritedFromParent() const { return ( m_editorFlags & InheritValueFromParent ) != 0; }
#endif

		Property( const rtti::IType *type,
				  const rtti::ClassType* owner,
				  Uint32 offset,
				  const CName name,
				  Uint64 flags,
				  const red::String& tooltip = String::EMPTY(),
				  const CName category = CName::NONE(),
				  const red::String& editorType = String::EMPTY(), 
				  const Float minValue = 0.0f,
				  const Float maxValue = 0.0f,
				  Uint32 editorFlags = 0 );
		
		~Property();

		//! Set property value for given object
		void Set( void *object, const void *buffer ) const;

		//! Get property value for given object
		void Get( const void *object, void *buffer ) const;

		//! Get offset to data
		void* GetOffsetPtr( void* base ) const;
		const void* GetOffsetPtr( const void* base ) const;

		void SetSizeConstraints( Uint32 minSize, Uint32 maxSize );
		void SetXAxisConstraints( Float min, Float max );
		void SetCustomEditorMode( CustomEditorMode editorMode ); // Separated from the constructor to avoid dependency on PropertyBuilder.h
		void SetKeyPath( const String& keyPath ); // look at m_keyPath

		/// Resolve override property, copy not overridden attributes from base property.
		void ResolveOverride( const Property& baseProperty, Uint32 overrideMask );

		//! Calculate data layout of properties
		static Uint32 CalcDataLayout( const red::DynArray< const Property* >& properties, const Uint32 initialOffset, const Uint32 initialAlignment = 4 );

		//! Disable cooking for property
		static void ChangePropertyFlag( const rtti::ClassType* objectClass, const CName& propertyName, Uint64 clearFlags, Uint64 setFlags );

	private:
		void * GetScriptPropertyData( void* scriptable ) const;
		const void * GetScriptPropertyData( const void* scriptable ) const;
		
		const rtti::IType*	m_type;							//!< Data type of the property
		CName				m_name;							//!< Name of the property
		CName				m_category;						//!< Category of the property
		const rtti::ClassType*	m_parent;					//!< Parent class that owns this property
		Uint32				m_offset;						//!< Offset to data in the parent class
		Uint64				m_flags;						//!< Flags

#ifndef NO_EDITOR
		Float				m_minValue;						//!< Minimum range value for ranged properties
		Float				m_maxValue;						//!< Maximum range value for ranged properties
		Float				m_xMinValue;					//!< Minimum x axis range value for curve properties
		Float				m_xMaxValue;					//!< Maximum x axis range value for curve properties
		Uint32				m_minSize;						//!< Minimum size for strings and dynamic arrays
		Uint32				m_maxSize;						//!< Maximum size for strings and dynamic arrays
		CName				m_customEditor;					//!< Name of the custom editor to use
		CustomEditorMode	m_customEditorMode;				//!< Mode of the custom editor to use (allows custom editor for array inner type etc.).
		String				m_tooltip;						//!< editor's tooltip
		String				m_keyPath;						//!< relative path to a key property, used in case an array simulates a map object

		Uint32				m_editorFlags;					//!< Editor side metadata, to be separated when we figure out how to handle the evergrowing property metadata in general
#endif
	};

	RED_INLINE void* Property::GetOffsetPtr( void* base ) const
	{
		void * propertyData = IsInSecondaryDataBuffer() ? GetScriptPropertyData( base ) : base;
		return red::OffsetPtr( propertyData, m_offset );
	}

	RED_INLINE const void* Property::GetOffsetPtr( const void* base ) const
	{
		const void * propertyData = IsInSecondaryDataBuffer() ? GetScriptPropertyData( base ) : base;
		return red::OffsetPtr( propertyData, m_offset );
	}

} // rtti
