/* Copyright (c) 2017-2018 CD Projekt Red. All Rights Reserved.*/

#pragma once

#include "../../backendEngine/include/editableCollectionCustomPropertyEditor.h"

namespace physics
{
	class PhysicsMeshBoneMapping;
}

namespace tools
{
	class EditableCollection;

	namespace prv
	{
		class EditableCollectionRootItem;

		// Physics bone name selector - drop down list filled with bone names of meshes in physics editor scene
		class EditControlPhysicsBoneNameSelector final : public tools::IEditableCustomProperty
		{
			RTTI_DECLARE_TYPE( EditControlPhysicsBoneNameSelector );

		public:
			virtual const Bool HandlesCustomType( const CName customTypeName, const EditableCollectionRootItem* root ) const override;
			virtual CName GetControlType() const override;
			virtual Bool HasOptions() const override;
			virtual const red::DynArray< String >& GetOptions() const override;

			virtual Bool SetValue( IRTTIContext& ctx, const rtti::AccessPath& path, const red::SharedPtr<rtti::ValueHolder>& newValue ) const override;
			virtual Bool GetValue( IRTTIContext& ctx, const rtti::AccessPath& path, red::SharedPtr<rtti::ValueHolder>& outValue ) const override;
			void Initialize( const CName customPropertyType, EditableCollection* collection, prv::EditableCollectionRootItem* root, const rtti::AccessPath& pathSoFar, const rtti::IType* type, const Bool isInlined, const Bool isReadOnly ) override final;

		private:
			void RefreshOptions() const;
			mutable red::DynArray< String > m_options;
			mutable red::HashMap< String, physics::PhysicsMeshBoneMapping > m_values;

		};
	}
}