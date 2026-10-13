/**
* Copyright (c) 2016-2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "materialViewer.h"

#include "../../../common/resourceMaterial/include/materialCompilationManager.h"

#include "../../../common/core/include/message.h"

#include "../../../common/renderBackend/include/materialLibrary.h"
#include "../../backendEngine/include/genericPreviewPanelSharedData.h"
#include "../../backendEngine/include/objectCollection.h"
#include "../../../common/shaderCache/include/shaderCacheManager.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( MaterialViewerResourceEditor, tools );
RTTI_PARENT_TYPE( tools::IResourceEditor );
RTTI_END_TYPE();

namespace tools
{
	MaterialViewerResourceEditor::MaterialViewerResourceEditor() = default;
	MaterialViewerResourceEditor::~MaterialViewerResourceEditor() = default;

	Bool MaterialViewerResourceEditor::Initialize( const THandle< CResource >& resource )
	{
		MaterialControllerFactory factory;
		m_materialController = factory.CreateMaterialController( resource, GetBackend(), &GetAccessHistory(), GetSharedSelection() );
		if ( !m_materialController )
		{
			return false;
		}

		m_sharedData = red::CreateSharedPtr< GenericPreviewPanelSharedData >( *this );

		if ( resource->IsA< CMaterialTemplate >() )
		{
			THandle< CMaterialTemplate > materialHandle = Cast< CMaterialTemplate >( resource );

			for ( const auto& technique : materialHandle->GetTechniques() )
			{
				for ( const auto& pass : technique.m_passes )
				{
					for ( Uint32 target = MSH_First; target < MSH_MAX; ++target )
					{
						red::DateTime dt;
						red::AbsolutePath sourcePath;
						if ( GetMaterialLibrary()->GetShaderSourceAbsolutePath( materialHandle->GetName(), pass.m_stagePassNameRegular, (EMaterialShaderTarget)target, sourcePath ) )
						{
							dt = GFileManager->GetFileTime( sourcePath );
						}
						else
						{
							CName targetName;
							if ( const rtti::EnumType* targetEnum = GetRttiSystem().FindEnum( ::GetTypeName<EMaterialShaderTarget>() ) )
							{
								targetEnum->FindName( target, targetName );
							}
							RED_LOG_ERROR( "Could not resolve source filename for Shader: %hs, Stage pass: %hs, target: %hs. Check your .fxmeta", materialHandle->GetName().AsChar(), pass.m_stagePassNameRegular.AsChar(), targetName.AsChar() );
						}
						materialHandle->SetTimeStamp( pass.m_stagePassNameRegular, (EMaterialShaderTarget)target, dt );
					}
				}
			}
		}

		m_serializableNotifier.SetCallback( [this]( const SerializableID& id, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue )
			{ OnNotifyPropertyChange( id, path ); } );

		return true;
	}

	void MaterialViewerResourceEditor::OnPreClose()
	{
		IResourceEditor::OnPreClose();
	}

	const Bool MaterialViewerResourceEditor::SupportsResourceClass( const rtti::ClassType* resourceClass ) const
	{
		return resourceClass->IsA( ClassID< CMaterialTemplate >() ) ||
			resourceClass->IsA( ClassID< CMaterialInstance >() ) ||
			resourceClass->IsA( ClassID< CMaterialLayerLibrary >() );
	}

	const THandle< CResource > MaterialViewerResourceEditor::GetResource() const
	{
		return m_materialController->GetMaterial();
	}

	void MaterialViewerResourceEditor::SetupInterop( MaterialViewerResourceEditor* editor, ::interop::utils::Object& op )
	{
		if ( editor->m_materialController )
		{
			op.RegisterObject( "materialController", editor->m_materialController->CreateInterop() );
		}

		// reimport
		op.RegisterFunctor< Bool >( "reimport", [ editor ]( ::interop::IContext& ctx )
		{
			MaterialControllerPtr materialController = editor->m_materialController;
			Bool result = true;
			if ( auto materialInstance = Cast< CMaterialInstance >( materialController->GetMaterial() ) )
			{
				// In case of material instance, update its parameters based on its material template.
				if ( materialInstance->UpdateParametersListBasedOnTemplate() )
				{
					materialInstance->MarkModified();
				}
			}
			else if ( auto materialTemplate = Cast< CMaterialTemplate >( materialController->GetMaterial() ) )
			{
				if ( !GShaderCache->HasMaterial( materialTemplate->GetHash() ) )
				{
					ctx.ThrowException("Reimport of material is not possible before first recompilation. Please recompile the material!");
					return false;
				}
				result = materialController->UpdateMaterialMeta( materialTemplate );
			}
			editor->RefreshPropertyGrid();
			return result;
		}
		);

		op.RegisterFunctor<void>( "recompile", [editor]( interop::IContext& ctx )
		{
#ifndef NO_RUNTIME_MATERIAL_COMPILATION
			if ( THandle< IMaterial > material = Cast< IMaterial >( editor->m_materialController->GetMaterial() ) )
			{
				const Bool recompilationResult = material->Recompile( false );
				if ( !recompilationResult )
				{
					ctx.ThrowException( "Recompilation of material failed. Possible reasons: errors in shader code (check log or engine\\shadersDump directory." );
				}
				else
				{
					GetMaterialCompilationManager().Run( false );
				}
			}
#endif // NO_RUNTIME_MATERIAL_COMPILATION
		} );

		op.RegisterFunctor<void>( "recompileSingle", [editor]( interop::IContext& ctx )
		{
#ifndef NO_RUNTIME_MATERIAL_COMPILATION
			if (THandle< IMaterial > material = Cast< IMaterial >( editor->m_materialController->GetMaterial() ))
			{
				const Bool recompilationResult = material->Recompile( false, true );
				if (!recompilationResult)
				{
					ctx.ThrowException( "Recompilation of material failed. Possible reasons: errors in shader code (check log or engine\\shadersDump directory." );
				}
				else
				{
					GetMaterialCompilationManager().Run( false );
				}
			}
#endif // NO_RUNTIME_MATERIAL_COMPILATION
		} );
	}

	void MaterialViewerResourceEditor::OnPreviewPanelRequest( const String& type, RenderingWindowPtr renderingWindowPtr, PreviewPanelPtr& outPanel )
	{
		auto panel = red::CreateSharedPtr< MaterialPreviewPanel >( renderingWindowPtr, m_sharedData );
		m_materialController->SetPreview( panel );
		outPanel = panel;
	}

	void MaterialViewerResourceEditor::RefreshPropertyGrid()
	{
		GetResourceView().Refresh();
	}

	Bool MaterialViewerResourceEditor::OnResourcePreSave( const THandle< CResource >& resource )
	{
		if ( resource->IsA< IMaterial >() )
		{
			// material instances MUST have "baseMaterial" set to be saved
			THandle< IMaterial > material = SafeCast< IMaterial >( resource );
			if ( !material->GetMaterialDefinition() )
			{
				tools::Message logMessage( MessageSeverity::Error, std::move( RED_LOCATION( resource->GetPath() ) ) );
				logMessage << "Material: ";
				logMessage << resource->GetPath().ToDebugString();
				logMessage << " is missing base material. ";
				IMessageReporter::ReportSafe( logMessage );
				return false;
			}
			red::StringBuilder< String > errorMessageBuilder;
			Bool validationResult = material->ValidateParameters( errorMessageBuilder );
			if ( !errorMessageBuilder.Empty() )
			{
				tools::Message logMessage( validationResult ? MessageSeverity::Warning : MessageSeverity::Error, std::move( RED_LOCATION( resource->GetPath() ) ) );
				logMessage << RED_NAME_CONSTEXPR( "Material: " );
				logMessage << resource->GetPath().ToDebugString();
				logMessage << " has following parameter errors: ";
				logMessage << errorMessageBuilder.AsChar();
				IMessageReporter::ReportSafe( logMessage );
			}
			if ( !validationResult )
			{
				return false;
			}
		}
		return IResourceEditor::OnResourcePreSave( resource );
	}

	Bool MaterialViewerResourceEditor::Save( red::DynArray< res::ResourcePath >& successfullySaved, red::DynArray< res::ResourcePath >& failedToSave )
	{
		Bool result = m_materialController->Save();
		result &= IResourceEditor::Save( successfullySaved, failedToSave );

		return result;
	}

	void MaterialViewerResourceEditor::OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path )
	{
		if ( m_materialController->CheckNotification( id ) )
		{
			m_materialController->OnNotifyPropertyChange( id, path );

			if ( path.ToString() == "baseMaterial" || path.ToString() == "name" )
			{
				RefreshPropertyGrid();
			}
		}
	}

} // namespace tools