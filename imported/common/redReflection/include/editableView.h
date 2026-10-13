/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redMemory/include/sharedPtr.h"
#include "../../redMemory/include/sharedFromThis.h"

#include "serializableId.h"
#include "reflectionPool.h"
#include "handle.h"

class ISerializable;
class IRTTIContext;

/// The view system utilizes some of the things from the RTTI system, especially the generalized value access
namespace rtti
{
	class AccessPath;
	class ValueHolder; // for safe value storage and ToString/FromString conversions	
	struct ClassEditablePropertyInfo;
}

namespace tools
{
	class IEditAction;
	class IEditableView;
	typedef red::SharedPtr< IEditableView > EditableViewPtr;

	class RED_REFLECTION_API IResolverUserContext : public ISerializable
	{
		RTTI_DECLARE_TYPE( IResolverUserContext );

		class IPrePropertyChange
		{
			RED_USE_POLYMORPHIC_MEMORY_POOL( red::PoolBackend );
		public:
			virtual ~IPrePropertyChange() = default;
			virtual Bool Do() = 0;
			virtual Bool Undo() = 0;
		};

	public:
		// to block pasting, override and return false
		// to change pasted value and prepare your data for it, override, do what you have to and return true
		virtual Bool OnPrePaste( const SerializableID& ownerId, const rtti::AccessPath& path, const rtti::ValueHolder& inValue, rtti::ValuePtr& outValue ) const;

		// to include additional un-doable logic when changing properties
		// (i.e. when property changes, reset some other property)
		virtual red::UniquePtr< IPrePropertyChange > CreatePrePropertyChangeAction( const rtti::ClassType* viewClass, SerializableID ownerId, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& newValue ) const;
	};

	/// Editable view of abstract object 
	/// The view may be constrained internally (less properties will be reported via EnumerateProperties)
	class RED_REFLECTION_API IEditableView : public red::EnableSharedFromThis<IEditableView>
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		explicit IEditableView( const rtti::ClassType* classType );

		virtual ~IEditableView();

		/// Get user context, access to custom data set by editor
		virtual SerializableHandle GetUserContext() const = 0;

		/// Get underlying ID
		virtual SerializableID GetSerializableID() const = 0;

		/// Get parent view, if any
		/// This is valid only for inlined edited object
		virtual EditableViewPtr GetParentView() const = 0;

		/// Get the view class for which this view was created
		/// In general this can be used to specify some editor-side visualization
		/// In RTTI based views this matches RTTI class name
		virtual const rtti::ClassType* GetViewClass() const;

		/// Is this a read only view? Will be true for object that are not checked out
		virtual Bool IsReadOnly() const = 0;

		/// Is this view's property read-only
		virtual Bool IsReadOnly( IRTTIContext& ctx, const rtti::AccessPath& path, Bool& outReadOnly ) const = 0;

		/// Is this an instance editable view? Different set of properties will be listed.
		virtual Bool IsInstanceEditable() const = 0;
		
		/// Get value of property
		/// May fail, for example if the underlying object is gone
		virtual Bool GetValue( IRTTIContext& ctx, const rtti::AccessPath& path, red::SharedPtr< rtti::ValueHolder >& outValue ) const = 0;

		/// Set new value of property
		/// May fail, for example if the underlying object is gone
		virtual Bool SetValue( IRTTIContext& ctx, const rtti::AccessPath& path, const red::SharedPtr< rtti::ValueHolder >& oldValue, const red::SharedPtr< rtti::ValueHolder >& newValue, bool clone = false ) const = 0;

		/// Reset value to default
		virtual Bool ResetValue( IRTTIContext& ctx, const rtti::AccessPath& path, const red::SharedPtr< rtti::ValueHolder >& oldValue ) const;

		/// Check if the given value is the default value
		/// May fail if default values are not supported by this object
		virtual Bool IsDefault( IRTTIContext& ctx, const rtti::AccessPath& path, Uint8& outValue ) const = 0;

		/// Get root properties defined by the view - this can be dynamic (depend on the object)
		/// This is not required to be cached as it's only queried when the editableContainer is constructed
		virtual void GetRootProperties( rtti::EditableProperties& outRootProperties ) const = 0;

		virtual void OnChildViewChanged() const {}

	private:
		const rtti::ClassType* m_classType;
	};

} // tools