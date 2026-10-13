// Copyright (c) 2018 CD Projekt Red. All Rights Reserved.

#pragma once
#include "../../backendEngine/include/editableCollectionCustomPropertyEditor.h"
#include "../../../common/interop/include/interopNotifier.h"
#include "../../../common/redReflection/include/serializableValueNotifier.h"

namespace tools
{
	class CustomPropertyEditor_TransformMatrix : public IEditableCustomProperty
	{
		RTTI_DECLARE_TYPE(CustomPropertyEditor_TransformMatrix);

	public:
		CustomPropertyEditor_TransformMatrix();
		virtual ~CustomPropertyEditor_TransformMatrix() override;

		virtual void Initialize( const CName customPropertyType, EditableCollection* collection, prv::EditableCollectionRootItem* root, const rtti::AccessPath& pathSoFar, const rtti::IType* type, const Bool isInlined, const Bool isReadOnly ) override;

		virtual CName GetControlType() const override;
		virtual const Bool HandlesCustomType( const CName customTypeName, const prv::EditableCollectionRootItem* root ) const override final;

		virtual Bool HasUnlistedChildren() const override;
		virtual interop::ObjectPtr GetUnlistedChild( interop::IContext& ctx, const String& name ) const override;
		virtual Bool IsCompound() const override { return true; }
		virtual interop::ObjectPtr GetChild( interop::IContext& ctx, const CName& name ) const override;

		virtual Bool GetValue(IRTTIContext& ctx, const rtti::AccessPath& path, red::SharedPtr<rtti::ValueHolder>& outValue) const override;
		virtual Bool SetValue(IRTTIContext& ctx, const rtti::AccessPath& path, const red::SharedPtr<rtti::ValueHolder>& newValue) const override;

		virtual Bool GetChildrenInfo(interop::IContext& ctx, red::DynArray<interop::ChildWithCategory>& outInfo) const override;

	private:
		void OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path );
		void InitializeInteropChildren();

		static CName s_customType;

		red::HashMap< CName, std::pair< CName, interop::ObjectPtr > > m_children;

		interop::InteropNotifier m_translationChangedNotifier;
		interop::InteropNotifier m_rotationChangedNotifier;

		SerializableValueNotifier m_serializableNotifier;
	};

}