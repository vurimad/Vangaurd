/**
* Copyright (c) 2015-2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "materialTemplateEditor.h"
#include "session.h"

#include "../../../common/resourceMaterial/include/materialTemplate.h"
#include "../../../common/resourceMaterial/include/materialInstance.h"
#include "resourceBackend.h"

#include "objectCollection.h"
#include "editActionHistory.h"
#include "../../../common/redReflection/include/rttiPathParser.h"
#include "../../../common/redReflection/include/gatheredResource.h"
#include "../../../common/resourceMaterial/include/materialLayerLibrary.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( MaterialResourceEditor, tools );
	RTTI_PARENT_TYPE( tools::IResourceEditor );
RTTI_END_TYPE();


namespace tools
{
	MaterialResourceEditor::MaterialResourceEditor()
	{
		m_structureView = RED_NEW( ObjectCollection )( GetAccessHistory() );

		m_serializableNotifier.SetCallback( [this]( const SerializableID& id, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue )
			{ OnNotifyPropertyChange( id, path ); } );
	}

	MaterialResourceEditor::~MaterialResourceEditor()
	{
		if ( m_material )
		{
			m_material = nullptr;
		}

		if ( m_structureView )
		{
			m_structureView->Reset();
			RED_DELETE( m_structureView );
			m_structureView = nullptr;
		}
	}

	Bool MaterialResourceEditor::Initialize( const THandle< CResource >& resource )
	{
		// get material to edit
		m_material = Cast< IMaterial >( resource );
		if ( !m_material )
			return false;

		return true;
	}

	const Bool MaterialResourceEditor::SupportsResourceClass( const rtti::ClassType* resourceClass ) const
	{
		return resourceClass->IsA( ClassID< IMaterial >() ) && 
			!( resourceClass->IsA( ClassID< CMaterialTemplate >() ) ||
			resourceClass->IsA( ClassID< CMaterialInstance >() ) ||
			resourceClass->IsA( ClassID< CMaterialLayerLibrary >() ) );
	}

	const THandle< CResource > MaterialResourceEditor::GetResource() const
	{
		return Cast< CResource >( m_material );
	}

	void MaterialResourceEditor::SetupInterop( MaterialResourceEditor* editor, ::interop::utils::Object& op )
	{
		THandle< IMaterial > matTemplate = Cast< IMaterial >( editor->GetResource() );
		if ( !matTemplate )
		{
			return;
		}

		op.RegisterFunctor<void>( "compile", 
			[matTemplate](::interop::IContext& ctx)
		{
#ifndef NO_RUNTIME_MATERIAL_COMPILATION
			if ( IMaterialDefinition* definition = matTemplate->GetMaterialDefinition() )
			{
				definition->ForceRecompilation( false );
			}
#endif // NO_RUNTIME_MATERIAL_COMPILATION
		});
	}
	
	void MaterialResourceEditor::OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path )
	{
		if ( m_material )
		{
			m_material->UpdateRenderResource();
		}
	}
} // interop