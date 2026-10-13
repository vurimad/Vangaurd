/**
* Copyright (c) 2015-2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "configBackend.h"

namespace interop
{
	class IContext;
}

namespace tools
{

	class EditableCollection;

	namespace prv
	{
		class EditableCollectionRootItem;
		class EditableCollectionArrayItem;
		class EditableCollectionStructItem;
		class EditableCollectionPropertyItem;
	}

	///-----

	// Implement this interface for custom properties/controls like bone selector or expression control
	class BACKEND_ENGINE_API IEditableCustomProperty
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_TYPE(IEditableCustomProperty);

	public:
		virtual ~IEditableCustomProperty() {}

		//---
		// FACTORY

		// Does it handle given property custom type?
		virtual const Bool HandlesCustomType(const CName customTypeName, const prv::EditableCollectionRootItem* root ) const = 0;

		// Create a custom property editor for given custom type
		// Returns initialized object or nullptr if no matches were found
		static IEditableCustomProperty* CreateCustomEditor(const CName customPropertyType, EditableCollection* collection, prv::EditableCollectionRootItem* root, const rtti::AccessPath& pathSoFar, const rtti::IType* type, const Bool isInlined, const Bool isReadOnly);

		//---
		// INTERFACE

		// Initialize, cannot fail
		virtual void Initialize(const CName customPropertyType, EditableCollection* collection, prv::EditableCollectionRootItem* root, const rtti::AccessPath& pathSoFar, const rtti::IType* type, const Bool isInlined, const Bool isReadOnly);

		// Get value of the property, implement only if you want to do a type conversion (int -> string, etc)
		// Default implementation works as expected
		virtual Bool GetValue(IRTTIContext& ctx, const rtti::AccessPath& path, red::SharedPtr<rtti::ValueHolder>& outValue) const;

		// Set value of the property, implement only if you want to do a type conversion (int -> string, etc)
		// Default implementation works as expected
		virtual Bool SetValue(IRTTIContext& ctx, const rtti::AccessPath& path, const red::SharedPtr<rtti::ValueHolder>& newValue) const;

		// Options (if there is any, accessors will get the option name as value)
		// Override both if necessary
		virtual Bool HasOptions() const;
		virtual void RefreshOptions() {}
		virtual const red::DynArray<String>& GetOptions( IRTTIContext& ctx, const rtti::AccessPath& path ) const { return GetOptions(); }
		virtual const red::DynArray<String>& GetOptions() const;		// TODO: maybe we should use CNames instead of strings?

		virtual Bool GetChildrenInfo(interop::IContext& ctx, red::DynArray< interop::ChildWithCategory >& outInfo) const;

		// For adding additional interop commands for custom control
		// Override both if used
		virtual Bool HasUnlistedChildren() const;
		virtual interop::ObjectPtr GetUnlistedChild( interop::IContext& ctx, const String& name ) const;

		// This name identifies control on the C# side																			
		virtual CName GetControlType() const = 0;
		
		// is the handled property a struct/class and should we send its children properties in usual manner?
		virtual Bool IsCompound() const { return false; }
		virtual interop::ObjectPtr GetChild( interop::IContext& ctx, const CName& name ) const;

		// Editor side tooltip
		virtual const String& GetTooltip() const { return String::EMPTY(); }

		virtual Bool CanReset() const;

		virtual Bool IsDefault( IRTTIContext& ctx, const rtti::AccessPath& path, Uint8& outValue ) const { return false; }

		//----
		// HELPERS

		// required to recover after shifting elements in an array
		void Rebind( const rtti::AccessPath& oldPath, const rtti::AccessPath& newPath );

		// Walk the parent objects and find first object matching given type
		THandle< ISerializable > FindParentObject(const rtti::ClassType* classType) const;

		// Walk the parent objects and find first object matching given functor
		struct ParentObjectVisitor
		{
			virtual Bool ProcessParentObject( THandle< ISerializable >& obj ) = 0;	// return true if we should stop processing
		};
		THandle< ISerializable > FindParentObject(ParentObjectVisitor& visitor) const;

		// Walk the parent objects and find first object matching given type
		template< typename T >
		RED_FORCE_INLINE THandle< T > FindParentObject() const { return Cast< T >(FindParentObject(ClassID<T>())); }

		//---

		/// get the custom property name we were created for
		RED_FORCE_INLINE const CName GetCustomTypeName() const { return m_customType; }

		/// get the collection we are in
		RED_FORCE_INLINE EditableCollection* GetCollection() const { return m_collection; }

		/// get root item (ADANCED USE)
		RED_FORCE_INLINE prv::EditableCollectionRootItem* GetRoot() const { return m_root; }

		/// get the assigned RTTI path of the property we are editing
		RED_FORCE_INLINE const rtti::AccessPath& GetPathSoFar() const { return m_pathSoFar; }

		/// get resolved RTTI type of the data
		RED_FORCE_INLINE const rtti::IType* GetType() const { return m_type; }

		/// are we in inlined mode ? (may change behavior)
		RED_FORCE_INLINE const Bool IsInlined() const { return m_isInlined; }

		/// are we in the read only mode ?
		RED_FORCE_INLINE const Bool IsReadOnly() const { return m_isReadOnly; }

		/// are many objects selected that has different property values?
		const Bool IsMultiValue() const;

	private:
		// cached information stash
		CName								m_customType;
		EditableCollection*					m_collection;
		prv::EditableCollectionRootItem*	m_root;
		rtti::AccessPath					m_pathSoFar;
		const rtti::IType*					m_type;
		Bool								m_isInlined;
		Bool								m_isReadOnly;
	};

} // tools