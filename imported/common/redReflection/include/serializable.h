/**
* Copyright (c) 2007-2020 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "handle.h"
#include "weakHandle.h"
#include "editorObjectId.h"
#include "reflectionPool.h"
#include "serializableId.h"
#include "rttiClass.h"
#include "../../redMemory/include/sharedPtr.h"


class IFile;
class IRTTIContext;
class CResource;
struct PostLoadContext;
struct PreSaveContext;

namespace text
{
	class ITextWriter;
	class ITextReader;
}

namespace tools
{
	class IEditableView;
	typedef red::SharedPtr< IEditableView > EditableViewPtr;
}

namespace rtti
{
	class ValueHolder;
	class AccessPath;
	class Property;
	class Variant;
}

///--------------------------------------------------------------------------

class RED_REFLECTION_API ISerializable
{
	RTTI_DECLARE_POLYMORPHIC_TYPE( ISerializable );
	RED_USE_POLYMORPHIC_MEMORY_POOL( red::PoolSerializable );

public:

	ISerializable();
	ISerializable( const ISerializable& other );
	virtual ~ISerializable();

	ISerializable& operator=( const ISerializable& other );

	// Called before object is saved
	virtual void OnPreSave( const PreSaveContext& context );

	// Called after object is loaded
	virtual void OnPostLoad( const PostLoadContext& context );

	// Called before object's property is changed via the IEditableView
	// The "path" represents the detailed access path to the element being changed
	// The "value" is the value that that somebody wants to set. We can override it if we want (and know how).
	// NOTE: DO NOT CHANGE THE VALUE HOLDER, create a new one and .Reset() the pointer. 
	// Edition can be denied by returning false from this function.
	virtual Bool OnPropertyPreChange( const rtti::AccessPath& path, red::SharedPtr< const rtti::ValueHolder >& inOutValue );

	// Called after object's property is changed via the IEditableView
	// The "path" represents the detailed access path to the element being changed
	// The "value" is the value that was set
	virtual void OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& newValue );

	// Object serialization interface
	virtual void OnSerialize( IFile& file );

	// Object text serialization interface
	virtual Bool OnSerializeToText( text::ITextWriter& writer ) const;
	
	// Object text de-serialization interface
	virtual Bool OnSerializeFromText( text::ITextReader& reader );

	// Property was read from file that is no longer in the object, returns true if data was handled
	virtual Bool OnPropertyMissing( CName propertyName, const rtti::Variant& readValue );

	// Property was read from file that has different type than current property, returns true if data was handled
	virtual Bool OnPropertyTypeMismatch( CName propertyName, const rtti::Property* existingProperty, const rtti::Variant& readValue );

	// Property value was not within the range currently declared
	virtual Bool OnPropertyRangeMismatch( CName propertyName, const Float min, const Float max, const Float readValue );

	// Called to check if we can save given property
	virtual Bool OnPropertyCanSave( IFile& file, CName propertyName, const rtti::Property* propertyObject ) const;

	// Create editor view for this object
	virtual red::SharedPtr< tools::IEditableView > CreateView(const red::SharedPtr< tools::IEditableView >& parentView = nullptr, const Bool isReadOnly = false, const CName viewClass = CName::NONE(), THandle< ISerializable > userContext = nullptr) const;

	// Make sure that the internal state of this object is valid, used only to connect ends between backend and frontend
	virtual Bool Editor_OnInitialize();

	// Notify editor that property has changed
	// If there are any collections there that have a view created from this ISerializable in there then they will get notified
	// The purpose is to allow the UI to redraw when a value of the property is changed (regardless of the source of that change)
	void NotifyPropertyChanged( const rtti::AccessPath& propertyPath, const rtti::ValuePtr& oldValue = rtti::ValuePtr(), const rtti::ValuePtr& newValue = rtti::ValuePtr() ); 

	// Dynamic editable properties - for materials, blocks with dynamic content, etc.
	virtual void GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const;

	// Read dynamic property
	virtual Bool ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr< rtti::ValueHolder >& outValue ) const;

	// Write dynamic property
	virtual Bool WriteCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr< rtti::ValueHolder >& newValue );

	// Get base object data
	virtual const void* GetBaseObjectData() const;

	// Get base object class
	virtual const rtti::ClassType* GetBaseObjectClass() const;

	// Editor integration - hacky but saves a lot of useless interfacing, there are times when a simpler solution is better
	virtual tools::EditorObjectID GetEditorObjectID() const;

	// Get object friendly name (used by ToString and many debug functions)
	virtual String GetFriendlyName() const;

	// Return resource Path associated with this object. Can return an empty path. 
	virtual const res::ResourcePath & GetPath() const;

	// Get parent object
	ISerializable* GetParent() const;

	// Set object parent, use with care
	void SetParent( const ISerializable* parent );

	// RTTI class check
	Bool IsA( const rtti::ClassType* rttiClass ) const { return GetClass()->IsA( rttiClass ); }

	// RTTI exact class check
	Bool IsExactlyA( const rtti::ClassType* rttiClass ) const { return GetClass() == rttiClass; }

	// RTTI class check
	template< class T >
	RED_INLINE Bool IsA() const;

	// RTTI class check
	template< class T >
	RED_INLINE Bool IsExactlyA() const;

	// Get serializable id (runtime or global)
	RED_INLINE const SerializableID& GetID() const { return m_objectId; }

	// Get handle holding this object (WARNING: object has to be already "shared" otherwise it causes fatal assert)
	THandle< ISerializable > HandleFromThis() const;

	// Get weak handle holding this object (WARNING: object has to be already "shared" otherwise it causes fatal assert)
	WeakHandle< ISerializable > WeakHandleFromThis() const;

	// Get weak handle holding this object (WARNING: it do not check if object was already "shared" and can return nullptr in such case. In ctor and dtor returns nullptr)
	WeakHandle< ISerializable > Unsafe_WeakHandleFromThis() const;

	template< typename T >
	RED_INLINE THandle< T > HandleFromThis() const;

	template< typename T >
	RED_INLINE WeakHandle< T > WeakHandleFromThis() const;

	virtual THandle< CResource > HACK_GetPrefabNodeInstanceDataResource() const { return nullptr; }

private:

	// Get handle holding this object, if called for "non-shared" object empty handle is returned
	THandle< ISerializable > HandleFromThisInternal() const;

	// Initialize internal weak handle
	void InternalSetWeakHandle( const THandle< ISerializable > & handle );

	// Called when THandle strong refcount goes to 0. Override and return false if THandle should not delete object.
	virtual Bool OnDestructionRequest();

	bool CheckForCircularDependency();

	WeakHandle< ISerializable > m_weakHandle;
	WeakHandle< ISerializable > m_parent;
	SerializableID m_objectId;

	friend class HandleSharedStorage;
};

typedef THandle< ISerializable >	SerializableHandle;
typedef WeakHandle< ISerializable > SerializableWeakHandle;

//-----------------------------------------------------------------------------

template< class T >
RED_INLINE Bool ISerializable::IsA() const
{
	return GetClass()->IsA( ClassID< T >() );
}

template< class T >
RED_INLINE Bool ISerializable::IsExactlyA() const
{
	return GetClass() == ClassID< T >();
}

template< typename T >
RED_INLINE THandle< T > ISerializable::HandleFromThis() const
{
	return SafeCast< T >( HandleFromThis() );
}

template< typename T >
RED_INLINE WeakHandle< T > ISerializable::WeakHandleFromThis() const
{
	return SafeCast< T >( WeakHandleFromThis() );
}

template< typename T >
RED_INLINE THandle< T > HandleFromPtr( const T* ptr )
{
	return ptr ? red::StaticCast< T >( ptr->HandleFromThis() ) : THandle< T >();
}

template< typename T >
RED_INLINE WeakHandle< T > WeakHandleFromPtr( const T* ptr )
{
	return ptr ? red::StaticCast< T >( ptr->WeakHandleFromThis() ) : WeakHandle< T >();
}
