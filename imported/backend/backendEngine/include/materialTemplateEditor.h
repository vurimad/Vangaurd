/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once

// this shouldn't be here in the final version, but for now it allows to refresh proxy when autohide distance changes
// - need to wait for the proxy creation pipeline updates with the proper solution
#include "../../../common/redReflection/include/serializableValueNotifier.h"
#include "resourceEditor.h"

class IMaterial;

namespace interop
{
	class MaterialResourceEditor;
}

namespace tools
{
	/// Editor for mesh files
	class MaterialResourceEditor : public IResourceEditor
	{
		RTTI_DECLARE_TYPE( MaterialResourceEditor );
		DECLARE_INTEROP_DERIVED( MaterialResourceEditor, IResourceEditor );

	public:
		MaterialResourceEditor();
		virtual ~MaterialResourceEditor();

		/// IResourceEditor interface
		virtual Bool Initialize( const THandle< CResource >& resource ) override;
		virtual const Bool SupportsResourceClass( const rtti::ClassType* resourceClass ) const override;
		virtual const THandle< CResource > GetResource() const override;

	private:
		void OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path );

		// the material being edited
		THandle< IMaterial >		m_material;
		
		ObjectCollection* m_structureView;
		interop::InteropNotifier m_structureChangedNotifier;

		SerializableValueNotifier m_serializableNotifier;
	};

} // tools
