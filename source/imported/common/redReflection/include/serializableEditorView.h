/**
* Copyright (c) 2007-2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "editableView.h"
#include "handle.h"

namespace tools
{
	/// A default RTTI base view of a serialized object
	/// This class provides legacy (old school) interface of accessing EDITABLE (PF_Editable) properties of object
	class RED_REFLECTION_API ISerializableEditorView : public IEditableView
	{
	public:
		ISerializableEditorView( const EditableViewPtr& parentView, const Bool isReadOnly, const rtti::ClassType* viewClass, const THandle<ISerializable>& userContext = nullptr );

		// IEditableView interface
		virtual SerializableHandle GetUserContext() const override final;
		virtual SerializableID GetSerializableID() const override final;
		virtual EditableViewPtr GetParentView() const override final;
		virtual Bool IsReadOnly() const override final;
		virtual Bool IsReadOnly( IRTTIContext& ctx, const rtti::AccessPath& path, Bool& outReadOnly ) const override;
		virtual Bool IsInstanceEditable() const override final;
		virtual Bool GetValue( IRTTIContext& ctx, const rtti::AccessPath& path, red::SharedPtr< rtti::ValueHolder >& outValue ) const override final;
		virtual Bool SetValue( IRTTIContext& ctx, const rtti::AccessPath& path, const red::SharedPtr< rtti::ValueHolder >& oldValue, const red::SharedPtr< rtti::ValueHolder >& newValue, bool clone = false ) const override;
		virtual Bool ResetValue( IRTTIContext& ctx, const rtti::AccessPath& path, const red::SharedPtr< rtti::ValueHolder >& oldValue ) const override;
		virtual Bool IsDefault( IRTTIContext& ctx, const rtti::AccessPath& path, Uint8& out ) const override;
		virtual void GetRootProperties( rtti::EditableProperties& outRootProperties ) const override final;
		virtual void OnChildViewChanged() const override;

	protected:
		// Get the object we are editing
		virtual THandle< ISerializable > GetObject() const = 0;
		virtual void OnValueSet( const rtti::AccessPath& path, const red::SharedPtr< rtti::ValueHolder >& oldValue, const rtti::ValuePtr& newValue ) const;
		virtual THandle< ISerializable > GetDefaultObject() const;

	private:
		
		// parent view
		EditableViewPtr m_parentView;

		// is this a read only view ?
		Bool m_isReadOnly;

		// user context, custom data set by editors
		THandle<ISerializable> m_userContext;
		mutable THandle<ISerializable> m_defaultObject;

		// custom value handling (still can be nested)
		const Bool GetCustomValue( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr< rtti::ValueHolder >& outValue ) const;
		const Bool SetCustomValue( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr< rtti::ValueHolder >& newValue ) const;

		// debug value handling (single props)
		const Bool GetDebugValue( IRTTIContext& ctx, const CName propertyName, red::SharedPtr< rtti::ValueHolder >& outValue ) const;

		static CName st_PropNameViewClass;
		static CName st_PropNameObjectClass;
		static CName st_PropNameObjectID;
	};

} // tools