/**
* Copyright (c) 2018-2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "entityTemplateDataView.h"
#include "editorComponentResolveHelper.h"
#include "entityIncludesLoadingHelper.h"

#include "editorGraphTypes.h"          // Needed for NodeDestription (component backend data).

#include "../../../common/worldEntities/include/entity.h"
#include "../../../common/worldEntities/include/entityBinding.h"
#include "../../../common/worldEntities/include/entityComponentsStorage.h"
#include "../../../common/worldEntities/include/entityTemplateUnpackedView.h"
#include "../../../common/worldEntities/include/entityTemplateInclude.h"
#include "../../../common/worldEntities/include/entityAssembler.h"

#include "../../../common/redReflection/include/serializationLoader.h"

#include "../../../common/redContainers/include/queue.h"
#include "../../../common/redJobs2/include/jobBuilder.h"
#include "../../../common/redMath/include/random.h"

using red::SharedPtr;
using red::DynArray;

using appearance::AppearanceResource;
using appearance::AppearanceDefinition;

namespace
{
	// Used to avoid returning THandle< TemplateResource > by value, and using const& instead.
	const THandle< ent::EntityTemplate > c_nullTemplatePtr( nullptr );
	// Used to avoid returning THandle< IComponent > by value, and using const& instead.
	const THandle< ent::IComponent > c_nullComponentPtr( nullptr );
}

namespace ent
{
	TemplateDataView::TemplateDataView()
		: m_mainTemplateBuilder( nullptr )
		, m_finalTemplateBuilder( nullptr )
		, m_isAppearanceDirty( true )
		, m_includesMap( red::PoolBackend() )
		, m_templatesComponentsMap( red::PoolBackend() )
		, m_allIncludesList( red::PoolBackend() )
		, m_appearanceParts( red::PoolBackend() )
		, m_appearancePartsComponents( red::PoolBackend() )
		, m_appearancePartAppearances( red::PoolBackend() )
		, m_namesMap( red::PoolBackend() )
		, m_skipUploadRenderData( false )
	{
	}

	TemplateDataView::~TemplateDataView()
	{
	}

	void TemplateDataView::SubmitChanges()
	{
		m_unpackedView->SubmitChanges();
	}

	const DynArray< AssemblerValidationError >& TemplateDataView::GetAssemblerValidationErrors() const
	{
		return m_assembler->GetValidationErrors();
	}

	void TemplateDataView::SetCurrentAppearanceName( CName newCurrentAppearanceName )
	{
		if ( m_currentAppearance != newCurrentAppearanceName )
		{
			m_currentAppearance = newCurrentAppearanceName;
			m_isAppearanceDirty = true;
		}
	}

	const THandle< EntityTemplate >& TemplateDataView::GetIncludedTemplate( const res::ResourcePath& templatePath ) const
	{
		const THandle< EntityTemplate >* ptr = m_includesMap.FindPtr( templatePath );
		return (ptr != nullptr) ? *ptr : c_nullTemplatePtr;
	}

	void TemplateDataView::Reset()
	{
		m_unpackedView.Reset();
		m_template.Reset();
		m_assembler.Reset();
		m_namesMap.Clear();
	}

	void TemplateDataView::Reset( const THandle< EntityTemplate >& newTemplate, job::Builder& builder, Bool skipUploadRenderData )
	{
		RED_FATAL_ASSERT( newTemplate, "Template missing" );
		m_template = newTemplate;
		m_skipUploadRenderData = skipUploadRenderData;
		// unpack data for edition

		if( m_customLoadingContext )
		{
			m_unpackedView = m_template->CreateUnpackedView( *m_customLoadingContext, builder );
		}
		else
		{
			serialization::LoadingContext context;
			context.m_resourcePath = m_template->GetPath(); // For debugging filename passthrough
			context.m_postLoadFlags.skipUploadRenderData = m_skipUploadRenderData;
			m_unpackedView = m_template->CreateUnpackedView( context, builder );
		}
		RED_FATAL_ASSERT( m_unpackedView, "Template view couldn't be created" );

		EntitySharedPtr entityPtr = m_template->Editor_CreateEntity( nullptr, builder, m_skipUploadRenderData );

		builder.DispatchJob( "TemplateDataView/UnpackEntity", [ this, entityPtr ]( const job::RunContext& ctx )
		{
			m_entity = *entityPtr;
		} );
	}

	void TemplateDataView::ResetUnpackedView()									/// @todo DP: sort this out, this is some weird stuff from entity editor
	{
		job::Builder builder{ job::Priority::Latent };
		if( m_customLoadingContext )
		{
			m_unpackedView = m_template->CreateUnpackedView( *m_customLoadingContext, builder );
		}
		else
		{
			serialization::LoadingContext context;
			context.m_resourcePath = m_template->GetPath(); // For debugging filename passthrough
			context.m_postLoadFlags.skipUploadRenderData = m_skipUploadRenderData;
			m_unpackedView = m_template->CreateUnpackedView( context, builder );
		}
		RED_FATAL_ASSERT( m_unpackedView, "Unpacked template view couldn't be created" );
		job::FlushCounter( builder.ExtractWaitCounter() );
	}

	void TemplateDataView::Rebuild( job::Builder& builder, Bool refreshComponents )
	{
		if ( refreshComponents )
		{
			m_templatesComponentsMap.Clear();
		}

		LoadTemplateIncludes( builder );
		LoadTemplateAppearanceParts( builder );

		builder.DispatchJob( "TemplateDataView/Rebuild", [ this ]( const job::RunContext& ctx )
		{
			UpdateAllIncludesList();

			job::Builder builder{ ctx };
			UpdateTemplateComponentsMap( builder );

			builder.DispatchJob( "TemplateDataView/Assembly", [ this ]( const job::RunContext& ctx )
			{
				m_assembler = red::CreateSharedPtr< Assembler >();

				m_mainTemplateBuilder = AddTemplateToAssembler( m_template, m_unpackedView->GetComponentsStorage() );

				for ( const auto& includeEntry : m_includesMap )
				{
					const THandle<EntityTemplate>& templateResource = includeEntry.Value();
					const THandle<ComponentsStorage>& templateComponents = m_templatesComponentsMap[templateResource];
					RED_FATAL_ASSERT( templateComponents != nullptr );
					AddTemplateToAssembler( templateResource, templateComponents );
				}

				for ( Uint32 i = 0; i < m_appearanceParts.Size(); ++i )									/// @todo DP: use some struct for this, AppearanceEntry ?
				{
					m_assembler->AddAppearancePart( m_appearanceParts[i], m_appearancePartsComponents[i], m_appearancePartAppearances[i] );
				}

				m_assembler->SetAppearanceProxyMesh( m_appearanceProxyMesh );

				if ( m_appearanceParameters != nullptr )
				{
					m_assembler->SetAppearanceParameters( m_appearanceParameters );
				}

				ComponentsStoragePtr resolvedComponentsStorage = CreateHandle<ComponentsStorage>();
				AssemblerResolveParameter assemblerResolveParams = { true, true };
				AssemblerBuildEntityParameter assemblerParams = { *m_entity, *resolvedComponentsStorage, nullptr };

				m_assembler->Resolve( assemblerResolveParams );
				m_assembler->BuildEntity( assemblerParams );

				resolvedComponentsStorage->ApplyIncludeInstanceData( m_template->Editor_GetIncludeInstanceDataLayout() );

				m_entity->Internal_SetComponentStorage( resolvedComponentsStorage );

				const EntityAssembleParameter param = 
				{
					m_template->GetPath(),
					m_currentAppearance
				};

				m_entity->Assemble( param );

				m_finalTemplateBuilder = m_assembler->GetFinalTemplateBuilder();

				// The assembly is done.
				RED_FATAL_ASSERT( m_finalTemplateBuilder->GetResolvedComponents().Size() == m_finalTemplateBuilder->GetResolvedComponentsEditorInfo().Size() );

				job::Builder builder{ ctx };
				UpdateNamesMap( builder );
			} );
		} );
	}

	void TemplateDataView::EnableCustomLoadingContext()
	{
		m_customLoadingContext = red::CreateUniquePtr<serialization::LoadingContext>();
		m_customLoadingContext->m_skipPostLoad = true;
		m_customLoadingContext->m_skipImports = true;
		m_customLoadingContext->m_skipBuffers = true;
		m_customLoadingContext->m_postLoadFlags.skipUploadRenderData = m_skipUploadRenderData;
	}

	void TemplateDataView::DisableCustomLoadingContext()
	{
		m_customLoadingContext = nullptr;
	}

	void TemplateDataView::LoadTemplateAppearanceParts( job::Builder& builder )
	{
		if ( !m_isAppearanceDirty )
		{
			return;
		}

		m_appearanceParts.Clear();
		m_appearancePartsComponents.Clear();

		// Create main appearances loading job chain.
		auto componentsCallback = [this]( const THandle<EntityTemplate>& partTemplate, const THandle<ComponentsStorage>& partComponentsStorage,
		                                  const THandle<appearance::AppearanceDefinition>& appearance )
		{
			RED_SCOPE_LOCK( m_appearancePartsLock );
			m_appearanceParts.PushBack( partTemplate );
			m_appearancePartsComponents.PushBack( partComponentsStorage );
			m_appearancePartAppearances.PushBack( appearance );
		};

		auto parametersCallback = [this]( const THandle<EntityParametersStorage>& paramsStorage )
		{
			RED_SCOPE_LOCK( m_appearancePartsLock );
			m_appearanceParameters = paramsStorage;
		};

		auto proxyMeshCallback = [this]( const THandle<CMesh>& proxyMesh, const THandle<appearance::AppearanceDefinition>& appearance )
		{
			m_appearanceProxyMesh = proxyMesh;
		};

		EntitySetupContext entitySetupContext = EntitySetupContext();
		entitySetupContext.appearanceName = m_currentAppearance;

		TemplateAppearanceLoadingContext appearanceLoadingContext;
		appearanceLoadingContext.componentsCallback = componentsCallback;
		appearanceLoadingContext.parametersCallback = parametersCallback;
		appearanceLoadingContext.proxyMeshCallback = proxyMeshCallback;
		appearanceLoadingContext.skipUploadRenderData = m_skipUploadRenderData;

		m_template->BuildAppearanceLoadingJobChain( builder, appearanceLoadingContext, entitySetupContext );

		builder.DispatchJob( "TemplateDataView/LoadTemplateAppearanceParts", [ this ]( const job::RunContext& )
		{
			m_isAppearanceDirty = false;
		} );
	}

	void TemplateDataView::LoadTemplateIncludes( job::Builder& builder )
	{
															/// @todo DP: reload templates only when explicitly asked for that (some flag for "forceReload" operation, when template changed externaly)
		red::HashMap< res::ResourcePath, THandle<EntityTemplate> > tempIncludesMap;
		m_includesMap.Swap( tempIncludesMap );             // To keep the template resources alive while we are loading new list.

		red::SharedPtr< tools::LoadTemplateIncludesParam, red::PoolBackend > param = red::CreateSharedPtr< tools::LoadTemplateIncludesParam, red::PoolBackend  >( tools::LoadTemplateIncludesParam { m_includesMapLock, m_includesMap, false } );

		PostLoadFlags postLoadFlags;
		postLoadFlags.skipUploadRenderData = m_skipUploadRenderData;
		tools::LoadTemplateIncludes( *param, m_template, builder, postLoadFlags, nullptr );

		builder.DispatchJob( "TemplateDataView/LoadTemplateIncludes", [ this, param ]( const job::RunContext& )
		{
			RED_FATAL_ASSERT( !param->IsCycleDetected, "Detected include dependency cycle" );
		} );
	}

	Bool TemplateDataView::TestTemplateIncludeCycles( const res::ResourcePath* extraInclude ) const
	{
		red::RWLock tempIncludesMapLock;
		red::HashMap< res::ResourcePath, THandle<EntityTemplate> > tempIncludesMap;
		tools::LoadTemplateIncludesParam param{ tempIncludesMapLock, tempIncludesMap, false };

		job::Builder builder;
		PostLoadFlags postLoadFlags;
		postLoadFlags.skipUploadRenderData = m_skipUploadRenderData;
		tools::LoadTemplateIncludes( param, m_template, builder, postLoadFlags, nullptr );
		job::FlushCounter( builder.ExtractWaitCounter() );

		return param.IsCycleDetected;
	}

	void TemplateDataView::UpdateAllIncludesList()
	{
		// We want to have repeatable consistent list of all includes. The map m_includesMap has "random" order.
		// This is why we create this list, with breadth-first search.
		// Direct includes are placed at the beginning of the list, in the same order they are placed in the template.
		//
		// Note: We silently skip templates missing in m_includesMap - probably loading those entities failed,
		//       some error was probably already shown, now just skip them.
		m_allIncludesList.Clear();

		red::Queue< IncludeEntry > includesQueue;
		red::HashMap< THandle<EntityTemplate>, Uint32 > includesMap;

		for ( const TemplateInclude& entityInclude : m_template->GetIncludes() )
		{
			THandle<EntityTemplate> templateResource;
			if ( entityInclude.GetTemplate().IsValid() && m_includesMap.Find( entityInclude.GetTemplate().GetPath(), templateResource ) )
			{
				includesQueue.Push( IncludeEntry{ entityInclude.GetName().AsChar(), templateResource } );
			}
		}

		while ( !includesQueue.Empty() )
		{
			IncludeEntry templateInclude = includesQueue.Pop();
			Uint32* templateIncludeIndex = includesMap.FindPtr( templateInclude.m_template );
			if ( templateIncludeIndex == nullptr )
			{
				includesMap.Insert( templateInclude.m_template, m_allIncludesList.Size() );
				m_allIncludesList.PushBack( templateInclude );

				if ( templateInclude.m_template )
				{
					for ( const TemplateInclude& entityInclude : templateInclude.m_template->GetIncludes() )
					{
						THandle<EntityTemplate> includedTemplateResource;
						if ( m_includesMap.Find( entityInclude.GetTemplate().GetPath(), includedTemplateResource ) )
						{
							includesQueue.Push( IncludeEntry{ templateInclude.m_includePath + "/" + entityInclude.GetName().AsChar(), includedTemplateResource } );
						}
					}
				}
			}
			else
			{
				m_allIncludesList[*templateIncludeIndex].m_includePath += "\n" + templateInclude.m_includePath;
			}
		}
	}

	Bool TemplateDataView::AddInclude( CName includeName, const res::ResourcePath& includedTemplatePath, Uint32* outIncludeIndex )
	{
		RED_FATAL_ASSERT( includedTemplatePath.IsEmpty() || includedTemplatePath.IsValid() );
		RED_FATAL_ASSERT( !includeName.Empty() );
																						/// @todo DP: test if the path is valid (there is a resource)?
		if ( TestTemplateIncludeCycles( &includedTemplatePath ) )
		{
			return false;
		}

		if ( HasInclude( includeName ) )
			return false;              // Name is already used.

		EntityTemplate::TIncludes& includes = m_template->GetIncludes();
		if ( outIncludeIndex != nullptr )
			*outIncludeIndex = includes.Size();
		includes.PushBack( TemplateInclude( includeName, includedTemplatePath ) );

		if( !includedTemplatePath.IsEmpty() )
		{
			const job::Counter& counter = m_template->ScheduleEntityCompilationJobs();
			job::FlushCounter( counter );
		}
		
		return true;
	}

	Bool TemplateDataView::AddInclude( Uint32 includePositionOnList, const TemplateInclude& newInclude )
	{
		RED_FATAL_ASSERT( !newInclude.GetName().Empty() );
		RED_FATAL_ASSERT( newInclude.GetTemplate().GetPath().IsEmpty() || newInclude.GetTemplate().GetPath().IsValid() );

		if ( TestTemplateIncludeCycles( &newInclude.GetTemplate().GetPath() ) )
		{
			return false;
		}

		if ( HasInclude( newInclude.GetName() ) )
			return false;              // Name is already used.

		if( m_template->GetIncludes().InsertAt( includePositionOnList, newInclude ).IsSuccessful() )
		{
			const job::Counter& counter = m_template->ScheduleEntityCompilationJobs();
			job::FlushCounter( counter );

			return true;
		}

		return false;
	}

	Bool TemplateDataView::RemoveInclude( CName includeName )
	{
		EntityTemplate::TIncludes& includes = m_template->GetIncludes();
		for ( Uint32 includeIndex = 0; includeIndex < includes.Size(); ++includeIndex )
		{
			const TemplateInclude& include = includes[includeIndex];
			if ( include.GetName() == includeName )
			{
				return RemoveInclude( includeIndex );
			}
		}
		return false;
	}

	Bool TemplateDataView::RemoveInclude( Uint32 includeIndex )
	{
		EntityTemplate::TIncludes& includes = m_template->GetIncludes();
		if ( includeIndex < includes.Size() )
		{
			CName includeName = includes[includeIndex].GetName();
			if ( includes.RemoveAt( includeIndex ).IsSuccessful() )
			{
				CorrectResolveSettingsAfterIncludeRemoval( includeName );
				CorrectBindingOverridesAfterIncludeRemoval( includeName );
				CorrectBackendDataOverridesAfterIncludeRemoval( includeName );
				
				const job::Counter& counter = m_template->ScheduleEntityCompilationJobs();
				job::FlushCounter( counter );

				return true;
			}
		}
		return false;
	}

	void TemplateDataView::RemoveIncludes()
	{
		EntityTemplate::TIncludes& includes = m_template->GetIncludes();
		red::DynArray< CName > includeNames;
		includeNames.Reserve( includes.Size() );

		for ( const TemplateInclude& include : includes )
		{
			CName includeName = include.GetName();
			includeNames.PushBack( includeName );
		}

		includes.Clear();
		for ( CName includeName : includeNames )
		{
			CorrectResolveSettingsAfterIncludeRemoval( includeName );
			CorrectBindingOverridesAfterIncludeRemoval( includeName );
			CorrectBackendDataOverridesAfterIncludeRemoval( includeName );
		}

		const job::Counter& counter = m_template->ScheduleEntityCompilationJobs();
		job::FlushCounter( counter );
	}

	Bool TemplateDataView::RenameInclude( Uint32 includeIndex, CName newName )
	{
		if ( HasInclude( newName ) )
			return false;              // Name is already used.

		CName oldName = m_template->GetIncludes()[includeIndex].GetName();
		m_template->GetIncludes()[includeIndex].SetName( newName );

		// Update resolve settings after include name change. They might reference this renamed include.
		for ( TemplateComponentResolveSettings& resolveSettings : m_template->GetComponentResolveSettings() )
		{
			if ( resolveSettings.m_nameParam == oldName )
			{
				resolveSettings.m_nameParam = newName;
			}
		}

		return true;
	}

	Bool TemplateDataView::SetIncludePath( Uint32 includeIndex, const res::ResourcePath& newIncludePath )
	{
		if ( newIncludePath.IsValid() )
		{
			TemplateInclude& templateInclude = m_template->GetIncludes()[includeIndex];
			TResAsyncRef< EntityTemplate > previousTemplate = templateInclude.GetTemplate();
			templateInclude.SetTemplate( newIncludePath );

			if ( TestTemplateIncludeCycles() )
			{
				templateInclude.SetTemplate( previousTemplate );    // Restore previous value.
				return false;
			}
																	/// @todo DP: remove all resolve settings, binding overrides, backend data overrides
			return true;
		}
		return false;
	}

	Bool TemplateDataView::HasInclude( CName includeName ) const
	{
		for ( const TemplateInclude& include : m_template->GetIncludes() )
		{
			if ( include.GetName() == includeName )
			{
				return true;
			}
		}
		return false;
	}

	void TemplateDataView::UpdateTemplateComponentsMap( job::Builder& builder )
	{
		m_templatesComponentsMap.Insert( m_template, m_unpackedView->GetComponentsStorage() );

		for ( const auto& includeEntry : m_includesMap )
		{
			const THandle<EntityTemplate>& templateResource = includeEntry.Value();
			ent::TemplateUnpackedViewPtr view;
			if( m_customLoadingContext )
			{
				view = templateResource->CreateUnpackedView( *m_customLoadingContext, builder );
			}
			else
			{
				serialization::LoadingContext context;
				context.m_resourcePath = templateResource->GetPath(); // For debugging filename passthrough
				context.m_postLoadFlags.skipUploadRenderData = m_skipUploadRenderData;
				view = templateResource->CreateUnpackedView( context, builder );
			}
			builder.DispatchJob( "TemplateDataView/UnpackView", [ this, view, templateResource ]( const job::RunContext& ctx )
			{
				RED_FATAL_ASSERT( view, "Unpacked template view couldn't be created" );

				if ( m_templatesComponentsMap.KeyExist( templateResource ) )
				{
					const ComponentsStoragePtr& components = m_templatesComponentsMap.GetRef( templateResource );
					for ( const ComponentHandle& component : view->GetComponents() )
					{
						auto pred = [component] ( const ComponentHandle& comp )
						{
							return comp->GetComponentId() == component->GetComponentId();
						};

						if ( std::find_if( components->GetComponents().Begin(),	components->GetComponents().End(), pred )
							 == components->GetComponents().End() )
						{
							m_templatesComponentsMap.Set( templateResource, view->GetComponentsStorage() );
							break;
						}
					}
				}
				else
				{
					m_templatesComponentsMap.Insert( templateResource, view->GetComponentsStorage() );
				}
			} );
		}
	}

	AssemblerTemplateBuilderPtr TemplateDataView::AddTemplateToAssembler( const THandle< EntityTemplate >& templateResource,
	                                                                      const THandle< ComponentsStorage >& templateComponents )
	{
		AssemblerTemplateBuilderPtr templateBuilder = m_assembler->AddTemplate( templateResource ).first;
		Bool nullComponentsFound = false;
		Bool nullIncludesFound = false;

		for ( const THandle<IComponent>& comp : templateComponents->GetComponents() )     // Not using shallow copy so we use instances of components from unpackedView, not spawn new ones.
		{
			if ( comp == nullptr )
			{
				nullComponentsFound = true;
				continue;
			}

			templateBuilder->AddComponent( comp );
		}

		for ( const TemplateInclude& templateInclude : templateResource->GetIncludes() )
		{
			if ( !templateInclude.GetTemplate().IsValid() )
			{
				nullIncludesFound = true;
				continue;
			}

			res::ResourcePath includePath = templateInclude.GetTemplate().GetPath();
			THandle<EntityTemplate> includedTemplate;
			if ( m_includesMap.Find( includePath, includedTemplate ) )               // Yes, we silently skip missing templates, errors for them should be reported at an earlier stage.
				templateBuilder->AddInclude( includedTemplate, templateInclude );
		}

		if ( nullComponentsFound )
		{
			RED_DATA_ERROR_BEGIN( red::LoggerCategory_Default, false,
			                      "Null component found in template '%hs'. Maybe component class was removed or renamed?", templateResource->GetPath().ToDebugString() );
			RED_DATA_ERROR_ACTION_SHOW_ASSET( templateResource->GetPath().ToDebugString() );
			RED_DATA_ERROR_END();
		}

		if ( nullIncludesFound )
		{
			RED_DATA_ERROR_BEGIN( red::LoggerCategory_Default, false, "Null include found in template '%hs'.", templateResource->GetPath().ToDebugString() );
			RED_DATA_ERROR_ACTION_SHOW_ASSET( templateResource->GetPath().ToDebugString() );
			RED_DATA_ERROR_END();
		}

		return templateBuilder;
	}

	const DynArray< THandle<IComponent> >& TemplateDataView::GetTemplateComponents() const
	{
		return m_finalTemplateBuilder->GetTemplateComponents();
	}

	const DynArray< THandle<IComponent> >& TemplateDataView::GetResolvedComponents() const
	{
		return m_finalTemplateBuilder->GetResolvedComponents();
	}

	const DynArray< ComponentEditorInfo >& TemplateDataView::GetResolvedComponentsEditorInfo() const
	{
		return m_finalTemplateBuilder->GetResolvedComponentsEditorInfo();
	}

	const DynArray< ComponentEditorInfo >& TemplateDataView::GetSuppressedComponentsEditorInfo() const
	{
		return m_finalTemplateBuilder->GetSuppressedComponentsEditorInfo();
	}

	const THandle< IComponent >& TemplateDataView::FindComponent( CName componentName ) const
	{
		const DynArray< THandle<IComponent> >& components = m_finalTemplateBuilder->GetResolvedComponents();
		if (m_finalTemplateBuilder)
		{
			for (const THandle<IComponent>& comp : components)
			{
				if (comp->GetName() == componentName)
					return comp;
			}
		}

		return c_nullComponentPtr;
	}

	const TemplateBindingOverride* TemplateDataView::FindResolvedBindingOverride( CName componentName, CName bindingSlotName ) const
	{
		const DynArray< TemplateBindingOverride >& bindingOverrides = m_finalTemplateBuilder->GetResolvedBindingOverrides();
		for ( const TemplateBindingOverride& bindingOverride : bindingOverrides )
		{
			if ( (bindingOverride.m_componentName == componentName) && (bindingOverride.m_propertyName == bindingSlotName) )
			{
				return &bindingOverride;
			}
		}

		return nullptr;
	}

	Bool TemplateDataView::IsComponentBindingDefinedInInclude( CName componentName, CName bindingSlotName ) const
	{
		// Test if binding is defined in the current version of a component (defined in current template).
		const DynArray< THandle<IComponent> >& components = m_mainTemplateBuilder->GetTemplateComponents();
		for ( const THandle<IComponent>& comp : components )
		{
			if ( comp->GetName() == componentName )
			{
				THandle< IBinding > binding;
				if ( !comp->GetBinding( bindingSlotName, binding ) )
					return false;

				if ( binding != nullptr )
					return false;
			}
		}

		// Test if binding is defined in binding overrides in the current template.
		const DynArray< TemplateBindingOverride >& bindingOverrides = m_mainTemplateBuilder->GetTemplateBindingOverrides();
		for ( const TemplateBindingOverride& bindingOverride : bindingOverrides )
		{
			if ( (bindingOverride.m_componentName == componentName) && (bindingOverride.m_propertyName == bindingSlotName) )
			{
				return false;
			}
		}

		// Not defined in current template, assuming it's coming from the include. This will also count appearance bindings.
		return true;
	}

	void TemplateDataView::UpdateNamesMap( job::Builder& builder )
	{
		red::HashMap< red::RUID, CName >  newMap = { red::PoolBackend() };
		red::HashMap< CName, CName > renameInfos = { red::PoolBackend() };

		auto resolvedComponents = GetResolvedComponents();

		newMap.Reserve( resolvedComponents.Size() );
		renameInfos.Reserve( resolvedComponents.Size() );

		for ( const THandle< IComponent >& component : resolvedComponents )
		{
			if ( component->GetComponentId().IsZero() )
			{
				// it's not a valid id, may result in errors if we take it into account
				continue;
			}

			if ( const CName* previousName = m_namesMap.FindPtr( component->GetComponentId() ) )
			{
				if ( *previousName != component->GetName() )
				{
					renameInfos.Insert( *previousName, component->GetName() );
				}
			}
			newMap.Insert( component->GetComponentId(), component->GetName() );
		}
		m_namesMap.Clear();
		m_namesMap.Swap( newMap );

		Bool updated = false;
		for ( const auto renameInfo : renameInfos )
		{
			const CName& oldName = renameInfo.Key();
			const CName& newName = renameInfo.Value();

			if ( oldName != newName )
			{
				RED_DATA_ERROR_BEGIN( red::LoggerCategory_Default, false,
					"Duplicated component IDs in components %hs and %hs", oldName.AsChar(), newName.AsChar() );
				RED_DATA_ERROR_ACTION_SHOW_ASSET( m_template->GetPath().ToDebugString() );
				RED_DATA_ERROR_END();

				continue;
			}

			// before updating bindings, check if the component that was renamed
			// wasn't covering included component
			if ( IsComponentNameUsed( oldName.AsChar() ) )
			{
				const TemplateComponentResolveSettings* existingSettings = m_template->GetComponentResolveSettings( oldName );
				if ( !existingSettings || existingSettings->m_mode != TemplateComponentResolveMode::Suppress )
				{
					TemplateComponentResolveSettings resolveSettings;
					resolveSettings.m_componentName = oldName;
					resolveSettings.m_mode = TemplateComponentResolveMode::Suppress;
					tools::SetComponentResolveSettings( m_template, resolveSettings );
				}

				updated = true;
			}

			if ( UpdateBindName( oldName, newName ) )
			{
				updated = true;
			}
		}

		if ( updated )
		{
			SubmitChanges();
			Rebuild( builder );
		}
	}

	Bool TemplateDataView::UpdateBindName( const CName& oldName, const CName& newName )
	{
		Bool updated = false;

		for ( TemplateBindingOverride& bindingOverride : m_template->GetBindingOverrides() )
		{
			if ( bindingOverride.m_binding && bindingOverride.m_binding->GetBindName() == oldName )
			{
				bindingOverride.m_binding->SetBindName( newName );
				updated = true;
			}
		}

		for ( const THandle< IComponent >& component : GetResolvedComponents() )
		{
			if ( component->GetName() == newName || IsComponentDefinedInAppearance( component ) )
			{
				continue;
			}

			THandle<EntityTemplate> originTemplate = GetComponentOriginTemplate( component );

			IComponent::TBindingList bindings;
			component->CollectBindingsInfo( bindings );

			for ( ComponentBindingInfo& info : bindings )
			{
				THandle< IBinding > binding;
				if ( component->GetBinding( info.m_name, binding ) && binding && binding->GetBindName() == oldName )
				{
					binding->SetBindName( newName );
					updated = true;

					if ( originTemplate == m_template )
					{
						component->SetBinding( info.m_name, binding );
					}
					else
					{
						// it's included component that needs to change its binding
						// - add binding override if it doesn't exist
						if ( m_template->GetBindingOverride( component->GetName(), info.m_name ) == nullptr )
						{
							TemplateBindingOverride bindingOverride;
							bindingOverride.m_componentName = component->GetName();
							bindingOverride.m_propertyName = info.m_name;
							bindingOverride.m_binding = binding;
							m_template->GetBindingOverrides().PushBack( bindingOverride );
						}
					}
				}
			}
		}

		return updated;
	}

	const ComponentEditorInfo* TemplateDataView::GetComponentInfoForComponent( CName componentName ) const
	{
		RED_FATAL_ASSERT( m_finalTemplateBuilder->GetResolvedComponentsEditorInfo().Size() == m_finalTemplateBuilder->GetResolvedComponents().Size() );
		return m_finalTemplateBuilder->GetComponentEditorInfo( componentName );
	}

	Bool TemplateDataView::IsComponentNameUsed( const AnsiChar* componentName, Bool checkAlsoIncludedComponents ) const
	{
		DynArray< THandle<EntityTemplate> > templatesToCheck = { red::PoolFrame() };
		templatesToCheck.Reserve( 32 );

		if ( checkAlsoIncludedComponents )
			m_includesMap.GetValues( templatesToCheck );
		templatesToCheck.PushBack( m_template );

		for ( const THandle<EntityTemplate>& entityTemplate : templatesToCheck )
		{
			const THandle<ComponentsStorage> templateComponents = m_templatesComponentsMap[entityTemplate];
			RED_FATAL_ASSERT( templateComponents != nullptr );
			if ( templateComponents->IsComponentNameUsed( componentName ) )
				return true;
		}

		return false;
	}

	namespace
	{
		static String RemoveNumbersFromTheEnd( const String& name )
		{
			Uint32 trimLength = 0;
			while ( trimLength < name.Length() )
			{
				const auto ch = name.AsChar()[name.Length() - 1 - trimLength];
				if ( !(ch >= '0' && ch <= '9') )
					break;

				trimLength += 1;
			}
			return name.LeftString( name.Length() - trimLength );
		}

		static void CleanUpComponentName( red::String& name )
		{
			if ( name.BeginsWith( "ent" ) )
				name = name.StringAfter( "ent" );
			else if ( name.BeginsWith( "r6" ) )
				name = name.StringAfter( "r6" );
			else if ( name.BeginsWith( "game" ) && (name != "gameComponent") )       // Yes, we do have a component named just "game"...
				name = name.StringAfter( "game" );

			if ( name.EndsWith( "Component" ) )
				name = name.StringBefore( "Component" );
		}
	}

	String TemplateDataView::GenerateComponentNameRandomSuffixString( Uint32 numChars )
	{
		char charsTable[] = "0123456789"; // For now I'm using only numbers. It looks better and works better for current CName implementation.
		String result;                    // We can safely change this to alphanumeric when CName is changed to hash based (no trailing digits optimization).
		result.Resize( numChars );
		for ( Uint32 i = 0; i < numChars; ++i )
		{
			Uint32 charIndex = math::DefaultRandom().Get<Uint32>( sizeof( charsTable ) - 1u - 1u );
			result[i] = charsTable[charIndex];
		}
		return result;
	}

	CName TemplateDataView::GetBestComponentName( CName existingName, const rtti::ClassType* componentClass, Bool allowOverridingIncludedComponent ) const
	{
		String coreName;
		if ( existingName.Empty() )
		{
			// First check if the component has some defined initial name (other than a default one), if it does, try use it.
			THandle<IComponent> tempComp = componentClass->CreateHandle<IComponent>();

			Bool tryWithoutSaltingFirst = false;

			CName initialName = tempComp->GetName();
			if ( initialName != IComponent::c_defaultComponentName )
			{
				tryWithoutSaltingFirst = true;
				coreName = initialName.AsChar();
			}
			else
			{
				coreName = componentClass->GetName().AsChar();
				CleanUpComponentName( coreName );
			}

			// If the component class is single-instance-per-entity, then we first try without salting to keep names clean.
			if ( tempComp->AllowOnlyOneInstancePerEntity() )
				tryWithoutSaltingFirst = true;

			if ( tryWithoutSaltingFirst )
			{
				if ( !IsComponentNameUsed( coreName.AsChar(), !allowOverridingIncludedComponent ) )
					return RED_NAME( coreName.AsChar() );
			}
		}
		else
		{	// When the coreName is explicit we first try without salting.
			coreName = existingName.AsChar();
			if ( !IsComponentNameUsed( coreName.AsChar(), !allowOverridingIncludedComponent ) )
				return RED_NAME( coreName.AsChar() );

			coreName = RemoveNumbersFromTheEnd( coreName );
		}

		for ( Uint32 i = 0; i < 1000; ++i )
		{
			Uint32 numRandomDigits = 4u + (i > 100 ? (i / 200u) : 0);                 // After 100 tries add extra digits to minimize probability of collision.
			String saltedName = coreName + GenerateComponentNameRandomSuffixString( numRandomDigits );
			if ( !IsComponentNameUsed( saltedName.AsChar(), !allowOverridingIncludedComponent ) )
				return RED_NAME( saltedName.AsChar() );
		}

		RED_FATAL( "To many components with the same naming pattern '%hs'", coreName.AsChar() );
		return RED_NAME( coreName.AsChar() );
	}

	CName TemplateDataView::GetInitialComponentName( const rtti::ClassType* componentClass ) const
	{
		return GetBestComponentName( CName::NONE(), componentClass, false );
	}

	Bool TemplateDataView::CanCreateComponent( const rtti::ClassType* componentClass ) const
	{
		const IComponent* defaultComponent = componentClass->GetDefaultObject<IComponent>();
		if ( defaultComponent->AllowOnlyOneInstancePerEntity() )
		{
			for ( const THandle<IComponent>& existingComponent : m_unpackedView->GetComponents() )
			{
				if ( existingComponent->IsA( componentClass ) )
				{
					RED_LOG_INFO( "WARNING: entity already contains component '%hs' of type %hs",
						existingComponent->GetName().AsChar(), componentClass->GetName().AsChar() );
					return false;
				}
			}
		}
		return true;
	}

	Bool TemplateDataView::AddComponent( const THandle<IComponent>& newComponent, Bool allowOverridingIncludedComponent )
	{
		if ( !CanCreateComponent( newComponent->GetClass() ) )
		{
			RED_LOG_INFO( "WARNING: entity already contains component of type %hs",
				newComponent->GetClass()->GetName().AsChar() );
			return false;
		}

		CName initialComponentName = newComponent->GetName();

		RED_LOG_INFO( "TemplateDataView::AddComponent '%hs'", initialComponentName.AsChar() );

		// First take care of the name of the component. We need to make sure we don't generate name conflicts.
		CName componentName = GetBestComponentName( initialComponentName, newComponent->GetClass(), allowOverridingIncludedComponent );
		if ( componentName != initialComponentName )
		{
			RED_LOG_INFO( "   Renaming component to '%hs' to avoid name conflicts", componentName.AsChar() );
			newComponent->SetName( componentName );
		}

		// When adding new component we have no bindings for it.
		// But there might be a version of this component in the includes, we should copy bindings from it.
		// This will greatly improve the "detach" operation experience, since we won't end up with a disconnected component.
		// Also there might be binding overrides set for included versions of this component.
		// If there are overrides, we should move them from binding overrides to binding properties of this component (remove those overrides from the template).
		// This way we will keep the invariant, that overrides are present only when there is no current version of this component in the template.

		DynArray<TemplateBindingOverride> currentBindings;
		GetComponentBindings( componentName, currentBindings );

		m_unpackedView->AddComponent( newComponent );         // TemplateUnpackedView does renaming inside when encounters name collisions... It should not happen here, we corrected the name above.

		for ( const TemplateBindingOverride& currentBinding : currentBindings )
		{
			if ( !newComponent->SetBinding( currentBinding.m_propertyName, currentBinding.m_binding ) )
			{
				// No such binding point in newComponent? We might have added a component of a different class than the ones from includes.
				/// @todo DP: what about this? Do we want to keep the overrides?
				RED_LOG_INFO( "WARNING: unable to transfer binding '%hs' from override to a component.", currentBinding.m_propertyName.AsChar() );
			}
		}

		tools::RemoveBindingOverrides( m_template, componentName );
		tools::ClearComponentResolveSettings( m_template, componentName );				/// @todo DP: what about Jowitas  createNode, CloneNode i RestoreNode (especially the last one, which probably should not have this called)
		tools::ClearComponentBackendDataOverride( m_template, componentName );
		return true;
	}

	Bool TemplateDataView::RemoveComponent( const THandle<IComponent>& componentToRemove )
	{
		RED_LOG_INFO( "TemplateDataView::RemoveComponent '%hs' (0x%p)", componentToRemove->GetName().AsChar(), componentToRemove.Get() );

		if ( !m_unpackedView->HasComponent( componentToRemove ) )
		{
			// Attempt to remove included component - included components are read-only, they can only be suppressed
			return false;
		}

		// When we are removing current version we should extract bindings out of it and set binding overrides.
		// But only when there are included versions still in the entity. But what about suppressing bindings (set to nullptr)?
		m_unpackedView->RemoveComponent( componentToRemove );

		if ( GetNumComponentVersions( componentToRemove->GetName() ) > 1u )
		{
			DynArray< TemplateBindingOverride > componentBindings;
			GetComponentBindings( componentToRemove, componentBindings );

			DynArray< TemplateBindingOverride > newBindingOverrides;
			for ( const TemplateBindingOverride& bindingOverride : componentBindings )
			{
				if ( bindingOverride.m_binding != nullptr )									/// @todo DP: what about nullptr override (binding suppress)?
				{
					newBindingOverrides.PushBack( bindingOverride );
				}
			}

			// Now add the overrides.
			m_template->GetBindingOverrides().PushBack( newBindingOverrides );

			// Set proper backend data override (position in presentation graph etc).
			const THandle<IBackendData>& backendData = componentToRemove->GetBackendData();
			if ( backendData != nullptr )
			{
				Point offset = Cast< tools::NodeDescription >( componentToRemove->GetBackendData() )->m_position;

				TemplateComponentBackendDataOverrideInfo info;
				info.m_componentName = componentToRemove->GetName();
				info.m_offset = Vector2( (Float)offset.x, (Float)offset.y );

				tools::SetComponentBackendDataOverride( m_template, info );
			}
		}

		return true;
	}

	Bool TemplateDataView::GetComponentBinding( const THandle<IComponent>& component, CName bindingPropertyName, THandle<IBinding>& outBinding ) const
	{
		return m_finalTemplateBuilder->GetResolvedBinding( component->GetName(), bindingPropertyName, outBinding );
	}

	Uint32 TemplateDataView::GetNumComponentVersions( CName componentName ) const
	{
		const ComponentEditorInfo* compInfo = GetComponentInfoForComponent( componentName );
		return (compInfo != nullptr) ? ((compInfo->m_currentVersion != nullptr) + compInfo->m_includedVersions.Size()) : 0;
	}

	const THandle<EntityTemplate>& TemplateDataView::GetComponentAppearanceTemplate( const THandle<IComponent>& component ) const
	{
		for ( Uint32 i = 0; i < m_appearanceParts.Size(); ++i )
		{
			if ( m_appearancePartsComponents[i]->HasComponent( component ) )
			{
				return m_appearanceParts[i];
			}
		}
		if ( component->IsDefinedInAppearance() )
		{
			// This can happen for auto-generated appearance components like appearance proxy mesh.
			// They are not defined in external template, but they are considered to be appearance components.
			return m_template;
		}
		return c_nullTemplatePtr;
	}

	THandle<EntityTemplate> TemplateDataView::GetComponentDirectIncludeTemplate( const THandle<IComponent>& component ) const
	{
		const THandle<EntityTemplate>& appearanceTemplate = GetComponentAppearanceTemplate( component );
		if ( appearanceTemplate != nullptr )
			return appearanceTemplate;

		const ComponentEditorInfo* componentEditorInfo = GetComponentInfoForComponent( component->GetName() );
		RED_FATAL_ASSERT( componentEditorInfo != nullptr, "Unable to find editor info for a component '%hs'", component->GetName().AsChar() );
		return GetComponentOriginTemplate( m_template, component, *componentEditorInfo, false );
	}

	THandle<EntityTemplate> TemplateDataView::GetComponentOriginTemplate( const THandle<IComponent>& component, Bool recurse ) const
	{
		const THandle<EntityTemplate>& appearanceTemplate = GetComponentAppearanceTemplate( component );
		if ( appearanceTemplate != nullptr )
			return appearanceTemplate;

		const ComponentEditorInfo* componentEditorInfo = GetComponentInfoForComponent( component->GetName() );
		RED_FATAL_ASSERT( componentEditorInfo != nullptr, "Unable to find editor info for a component '%hs'", component->GetName().AsChar() );
		return GetComponentOriginTemplate( m_template, component, *componentEditorInfo, recurse );
	}

	THandle<EntityTemplate> TemplateDataView::GetComponentOriginTemplate( const THandle<EntityTemplate>& templateResource,
	                                                                        const THandle<IComponent>& component,
	                                                                        const ComponentEditorInfo& componentEditorInfo,
	                                                                        Bool recurse ) const
	{
		if ( component == componentEditorInfo.m_currentVersion )
			return templateResource;

		for ( const ComponentEditorInfo::ComponentEntry& includeEntry : componentEditorInfo.m_includedVersions )
		{
			if ( component == includeEntry.m_component )
			{
				const res::ResourcePath& includedTemplateResPath = includeEntry.m_include->GetTemplate().GetPath();
				RED_FATAL_ASSERT( m_includesMap.KeyExist( includedTemplateResPath ) );
				THandle<EntityTemplate> includedTemplate = m_includesMap[includedTemplateResPath];
				AssemblerTemplateBuilderPtr assemblerBuilder = m_assembler->GetTemplateBuilder( includedTemplate );
				RED_FATAL_ASSERT( assemblerBuilder != nullptr );
				const ComponentEditorInfo* includedComponentEditorInfo = assemblerBuilder->GetComponentEditorInfo( component->GetName() );
				RED_FATAL_ASSERT( includedComponentEditorInfo != nullptr, "Unable to find editor info for an included component '%hs'", component->GetName().AsChar() );

				return recurse ? GetComponentOriginTemplate( includedTemplate, component, *includedComponentEditorInfo ) : includedTemplate;
			}
		}

		RED_ASSERT( false, "Unable to determine origin template for component '%hs', maybe unsupported resolve mode?", component->GetName().AsChar() );
		return nullptr;
	}

	void TemplateDataView::ListComponentIncludePath( DynArray<ComponentIncludePathEntry>& outIncludePath,
	                                                 const THandle<IComponent>& component,
	                                                 const ComponentEditorInfo& componentEditorInfo ) const
	{
		if ( component == componentEditorInfo.m_currentVersion )
			return;             // Not from include.

		for ( const ComponentEditorInfo::ComponentEntry& includeEntry : componentEditorInfo.m_includedVersions )
		{
			if ( component == includeEntry.m_component )
			{
				const res::ResourcePath& includedTemplateResPath = includeEntry.m_include->GetTemplate().GetPath();
				RED_FATAL_ASSERT( m_includesMap.KeyExist( includedTemplateResPath ) );
				THandle<EntityTemplate> includedTemplate = m_includesMap[includedTemplateResPath];
				AssemblerTemplateBuilderPtr assemblerBuilder = m_assembler->GetTemplateBuilder( includedTemplate );
				RED_FATAL_ASSERT( assemblerBuilder != nullptr );
				const ComponentEditorInfo* includedComponentEditorInfo = assemblerBuilder->GetComponentEditorInfo( component->GetName() );
				RED_FATAL_ASSERT( includedComponentEditorInfo != nullptr, "Unable to find editor info for an included component '%hs'", component->GetName().AsChar() );

				outIncludePath.PushBack( ComponentIncludePathEntry{ includeEntry.m_include->GetName(), includedTemplate } );
				ListComponentIncludePath( outIncludePath, component, *includedComponentEditorInfo );
				break;
			}
		}
	}

	Bool TemplateDataView::IsComponentDefinedInAppearance( const THandle<IComponent>& component ) const
	{
		return GetComponentAppearanceTemplate( component ) != nullptr;
	}

	Bool TemplateDataView::IsOverridenIncludedComponent( const THandle< IComponent >& component ) const
	{
		for ( const red::RUID& componentId : m_template->Editor_GetIncludeInstanceDataLayout().indices )
		{
			if ( componentId == component->GetComponentId() )
			{
				return true;
			}
		}
		return false;
	}

	void TemplateDataView::GetComponentBindings( CName componentName, red::DynArray<TemplateBindingOverride>& outBindings ) const
	{
		// Gather both binding overrides and bindings from the current version of the component.
																														/// @todo DP: use FindBinding
		const DynArray< THandle<IComponent> >& resolvedComponents = m_finalTemplateBuilder->GetResolvedComponents();
		for ( Uint32 i = 0; i < resolvedComponents.Size(); ++i )
		{
			if ( resolvedComponents[i]->GetName() == componentName )
			{
				GetComponentBindings( resolvedComponents[i], outBindings );
				return;
			}
		}
	}

	void TemplateDataView::GetComponentBindings( const THandle<IComponent>& component, red::DynArray<TemplateBindingOverride>& outBindings ) const
	{
		// Gather both binding overrides and bindings from the component.
		IComponent::TBindingList bindings;
		component->CollectBindingsInfo( bindings );

		// try to resolve the binding by looking at the components
		for ( const ComponentBindingInfo& it : bindings )
		{
			// get actual binding
			THandle< IBinding > binding;
			component->GetBinding( it.m_name, binding );

			const TemplateBindingOverride* bindingOverride = FindResolvedBindingOverride( component->GetName(), it.m_name );
			if ( bindingOverride != nullptr )
			{
				binding = bindingOverride->m_binding;
			}

			outBindings.PushBack( TemplateBindingOverride{ component->GetName(), it.m_name, binding } );
		}
	}

	namespace
	{
		Bool AreBindingsSameType( const THandle<IBinding>& bindingA, const THandle<IBinding>& bindingB )
		{
			Bool bindingAisNull = (bindingA == nullptr);
			Bool bindingBisNull = (bindingB == nullptr);

			if ( bindingAisNull != bindingBisNull )
				return false;                        // One binding is nullptr, the other is not.

			if ( bindingAisNull )
				return true;                         // Both binding are nullptr.

			const rtti::ClassType* classA = bindingA->GetClass();   // Both binding are non nullptr, compare them by value using RTTI.
			const rtti::ClassType* classB = bindingB->GetClass();
			if ( classA != classB )
				return false;                        // Different types of bindings.

			return true;        // Third argument is deprecated.
		}

		static Bool AreBindingsEqual( const THandle<ent::IBinding>& bindingA, const THandle<ent::IBinding>& bindingB )
		{
			if ( !AreBindingsSameType( bindingA, bindingB ) )
				return false;

			if ( (bindingA == nullptr) && (bindingB == nullptr) )
				return true;

			const rtti::ClassType* classA = bindingA->GetClass();   // Both binding are non nullptr, compare them by value using RTTI.
			return classA->Compare( bindingA.Get(), bindingB.Get(), 0 );        // Third argument is deprecated.
		}
	}

	void TemplateDataView::SetComponentBinding( const THandle<IComponent>& component, const THandle<EntityTemplate>& entityTemplateOfComponent,
	                                            CName bindingPropertyName, const THandle<IBinding>& newBinding, Bool overrideBinding )
	{
		Bool componentIsFromInclude = (entityTemplateOfComponent != m_template);

		if ( !componentIsFromInclude && m_unpackedView->HasComponent( component ) )
		{
			// Component is from current template, just set the binding property.
			const auto* prop = component->GetClass()->FindProperty( bindingPropertyName );
			prop->Set( component.Get(), &newBinding );
		}
		else
		{
			// Component is from included template, we need to set binding override.
			// Also a binding override might be already present in the template, in which case we will only update it.
			// To avoid setting overrides everywhere we test if it actually differs from the original (included) binding for this component.
			// If it is identical we don't need to override it and remove the binding override.
			THandle<EntityTemplate> includedTemplate = GetComponentDirectIncludeTemplate( component );
			AssemblerTemplateBuilderPtr includedTempateBuilder = m_assembler->GetTemplateBuilder( includedTemplate );
			RED_FATAL_ASSERT( includedTempateBuilder != nullptr );

			THandle<IBinding> oldBinding;
			Bool oldBindingWasFound = includedTempateBuilder->GetResolvedBinding( component->GetName(), bindingPropertyName, oldBinding );

			if ( !overrideBinding && oldBindingWasFound && AreBindingsEqual( oldBinding, newBinding ) )
			{
				tools::RemoveBindingOverride( m_template, component->GetName(), bindingPropertyName );   // Remove the binding override to reduce the template data size.
			}
			else
			{
				tools::SetBindingOverride( m_template, component->GetName(), bindingPropertyName, newBinding );
			}
		}
	}

	void TemplateDataView::UnpackBinding( const TemplateBindingOverride& bindingInfo, THandle<IComponent>& outSourceComponent,
	                                      THandle<IComponent>& outDestinationComponent, CName& outDestinationBindingSlotName, Bool& outIsEnabled ) const
	{
		RED_FATAL_ASSERT( bindingInfo.m_binding != nullptr );

		const THandle<IBinding>& binding = bindingInfo.m_binding;

		const THandle<IComponent>& comp = FindComponent( bindingInfo.m_componentName );
		const THandle<IComponent>& otherComp = FindComponent( binding->GetBindName() );

		// resolve the source
		THandle<IComponent> source, destination;
		if ( binding->GetDirection() == EBindingDirection::BindToSource )
		{
			source = otherComp;
			destination = comp;
		}
		else if ( binding->GetDirection() == EBindingDirection::BindToDestination )
		{
			source = comp;
			destination = otherComp;
		}
		else
		{
			RED_FATAL( "Invalid binding model" );
		}

		outSourceComponent = source;
		outDestinationComponent = destination;
		outDestinationBindingSlotName = bindingInfo.m_propertyName;
		outIsEnabled = bindingInfo.m_binding->IsEnabled();
	}

	Uint32 TemplateDataView::RemoveDanglingResolveSettings( Bool doNotRemoveJustCheck )
	{
		RED_LOG_INFO( "RemoveDanglingResolveSettings..." );

		DynArray<TemplateComponentResolveSettings> newResolveSettings;

		for ( const TemplateComponentResolveSettings& resolveSettings : m_template->GetComponentResolveSettings() )
		{
			const CName& componentName = resolveSettings.m_componentName;

			// Component might be missing.
			// Note, that we must look also for suppressed components.
			// Suppressed components are not present in the final entity, but they do have resolve settings.
			const ComponentEditorInfo* componentInfo = GetComponentInfoForComponent( componentName );
			if ( componentInfo == nullptr )
			{
				// No such component, removing resolve settings.
				RED_LOG_INFO( "   RemoveDanglingResolveSettings, component missing '%hs'", componentName.AsChar() );
				continue;
			}

			// Selected include might be missing.
			if ( resolveSettings.m_mode == TemplateComponentResolveMode::Select )
			{
				const CName& includeName = resolveSettings.m_nameParam;
				if ( !includeName.Empty() && !HasInclude( includeName ) )
				{
					// We have some component versions (or version), but selected version references missing include.
					// It's best just to remove this resolve settings and use default resolving mode for this component.
					RED_LOG_INFO( "   RemoveDanglingResolveSettings, selected include '%hs' is missing for component '%hs'",
						includeName.AsChar(), componentName.AsChar() );
					continue;
				}

				// Component might be missing in the selected template (current or included).
				//AssemblerTemplateBuilderPtr includedTemplateBuilder = GetIncludedTemplateBuilder( includeName );
				//const ComponentEditorInfo* componentInfo = includedTemplateBuilder->GetComponentEditorInfo( componentName )
				if ( componentInfo == nullptr )
				{
					// No such component in the included template, removing resolve settings.
					continue;
				}

				// Component might be suppressed in the selected template (current or included). We do not want to remove resolve setting then.
			}

			newResolveSettings.PushBack( resolveSettings );
		}

		Uint32 numRemoved = m_template->GetComponentResolveSettings().Size() - newResolveSettings.Size();
		if ( (numRemoved > 0) && !doNotRemoveJustCheck )
			m_template->GetComponentResolveSettings().Swap( newResolveSettings );
		RED_LOG_INFO( "RemoveDanglingResolveSettings ended, removed %u dangling resolve settings.", numRemoved );
		return numRemoved;
	}

	Uint32 TemplateDataView::RemoveDanglingBindingOverrides( Bool doNotRemoveJustCheck )
	{
		RED_LOG_INFO( "RemoveDanglingBindingOverrides..." );

		DynArray<TemplateBindingOverride> newBindingOverrides;

		for ( const TemplateBindingOverride& bindingOverride : m_template->GetBindingOverrides() )
		{
			const CName& componentName = bindingOverride.m_componentName;

			// Component might be missing.
			const ComponentEditorInfo* componentInfo = GetComponentInfoForComponent( componentName );
			if ( componentInfo == nullptr )
			{
				// Component is missing, removing binding override.
				RED_LOG_INFO( "   RemoveDanglingBindingOverrides, component missing '%hs'", componentName.AsChar() );
				continue;
			}

			// Component might be defined in this template. In such case it is not supposed to have binding override.
			if ( componentInfo->m_currentVersion != nullptr )
			{
				RED_LOG_INFO( "   RemoveDanglingBindingOverrides, component '%hs' is defined in current template, "
					"but it also has binding override defined at the same time", componentName.AsChar() );
				continue;
			}

			// Binding property might be missing.
			const THandle< IComponent >& component = FindComponent( componentName );
			if ( component != nullptr )
			{
				THandle<IBinding> binding;
				if ( !component->GetBinding( bindingOverride.m_propertyName, binding ) )
				{
					// No such binding property was found or the property is not a handle to IBinding.
					RED_LOG_INFO( "   RemoveDanglingBindingOverrides, wrong binding override, binding property '%hs' was not found in omponent '%hs'",
						bindingOverride.m_propertyName.AsChar(), componentName.AsChar() );
					continue;
				}
			}

			newBindingOverrides.PushBack( bindingOverride );
		}

		Uint32 numRemoved = m_template->GetBindingOverrides().Size() - newBindingOverrides.Size();
		if ( (numRemoved > 0) && !doNotRemoveJustCheck )
			m_template->GetBindingOverrides().Swap( newBindingOverrides );
		RED_LOG_INFO( "RemoveDanglingBindingOverrides ended, removed %u dangling (or invalid) binding overrides.", numRemoved );
		return numRemoved;
	}

	Uint32 TemplateDataView::RemoveDanglingBackendDataOverrides( Bool doNotRemoveJustCheck )
	{
		RED_LOG_INFO( "RemoveDanglingBackendDataOverrides..." );

		DynArray<TemplateComponentBackendDataOverrideInfo> newBackendDataOverrides;

		for ( const TemplateComponentBackendDataOverrideInfo& backendDataOverride : m_template->GetBackendDataOverrides() )
		{
			const CName& componentName = backendDataOverride.m_componentName;

			// Component might be missing.
			const ComponentEditorInfo* componentInfo = GetComponentInfoForComponent( componentName );
			if ( componentInfo == nullptr )
			{
				// Component is missing, removing binding override.
				RED_LOG_INFO( "   RemoveDanglingBackendDataOverrides, component missing '%hs'", componentName.AsChar() );
				continue;
			}

			// Component might be defined in this template. In such case it is not supposed to have backend data override.
			if ( (componentInfo->m_currentVersion != nullptr) && !componentInfo->m_isDefinedInAppearance )
			{
				RED_LOG_INFO( "   RemoveDanglingBackendDataOverrides, component '%hs' is defined in current template, "
					"but it also has backend data override defined at the same time", componentName.AsChar() );
				continue;
			}

			newBackendDataOverrides.PushBack( backendDataOverride );
		}

		Uint32 numRemoved = m_template->GetBackendDataOverrides().Size() - newBackendDataOverrides.Size();
		if ( (numRemoved > 0) && !doNotRemoveJustCheck )
			m_template->GetBackendDataOverrides().Swap( newBackendDataOverrides );
		RED_LOG_INFO( "RemoveDanglingBackendDataOverrides ended, removed %u dangling (or invalid) backend data overrides.", numRemoved );
		return numRemoved;
	}

	void TemplateDataView::CorrectResolveSettingsAfterIncludeRemoval( CName nameOfRemovedInclude )
	{
		// Update resolve settings after include remove. They might reference this removed include.
		DynArray<TemplateComponentResolveSettings> newResolveSettings;
		for ( const TemplateComponentResolveSettings& resolveSettings : m_template->GetComponentResolveSettings() )
		{
			if ( resolveSettings.m_nameParam != nameOfRemovedInclude )
			{
				newResolveSettings.PushBack( resolveSettings );
			}
		}

		if ( newResolveSettings.Size() != m_template->GetComponentResolveSettings().Size() )
			m_template->GetComponentResolveSettings().Swap( newResolveSettings );
	}

	void TemplateDataView::CorrectBindingOverridesAfterIncludeRemoval( CName nameOfRemovedInclude )
	{
		// Update binding overrides after include remove. They might be defined for components, that are defined only in that removed include.
		// We know there is no current version, since then the binding override would be moved into the component.
		DynArray<TemplateBindingOverride> newBindingOverrides;
		for ( const TemplateBindingOverride& bindingOverride : m_template->GetBindingOverrides() )
		{
			const CName& componentName = bindingOverride.m_componentName;
			const ComponentEditorInfo* componentInfo = GetComponentInfoForComponent( componentName );
			if ( componentInfo != nullptr )
			{
				// Check if this component was defined in removed include.
				if ( (componentInfo->m_currentVersion == nullptr) && (componentInfo->m_includedVersions.Size() == 1) )
				{
					// There is only one included component version.
					if ( componentInfo->m_includedVersions[0].m_include->GetName() == nameOfRemovedInclude )
					{
						continue;        // Skipping this binding override, so it will be removed.
					}
				}
			}

			newBindingOverrides.PushBack( bindingOverride );
		}

		if ( newBindingOverrides.Size() != m_template->GetBindingOverrides().Size() )
			m_template->GetBindingOverrides().Swap( newBindingOverrides );
	}

	void TemplateDataView::CorrectBackendDataOverridesAfterIncludeRemoval( CName nameOfRemovedInclude )
	{
		// Update backend data override after include remove.
		DynArray<TemplateComponentBackendDataOverrideInfo> newBackendDataOverrides;
		for ( const TemplateComponentBackendDataOverrideInfo& backendDataOverride : m_template->GetBackendDataOverrides() )
		{
			const CName& componentName = backendDataOverride.m_componentName;
			const ComponentEditorInfo* componentInfo = GetComponentInfoForComponent( componentName );
			if ( componentInfo != nullptr )
			{
				// Check if this component was defined in removed include.
				if ( (componentInfo->m_currentVersion == nullptr) && (componentInfo->m_includedVersions.Size() == 1) )
				{
					// There is only one included component version.
					if ( componentInfo->m_includedVersions[0].m_include->GetName() == nameOfRemovedInclude )
					{
						continue;        // Skipping this binding override, so it will be removed.
					}
				}
			}

			newBackendDataOverrides.PushBack( backendDataOverride );
		}

		if ( newBackendDataOverrides.Size() != m_template->GetBackendDataOverrides().Size() )
			m_template->GetBackendDataOverrides().Swap( newBackendDataOverrides );
	}

	void TemplateDataView::DebugPrintEntityTemplate() const
	{
		RED_LOG_INFO( "Printing entity template '%hs' (Debug)...", m_template->GetPath().ToDebugString() );

		RED_LOG_INFO( "   Components (%u):", m_unpackedView->GetComponents().Size() );
		for ( const THandle<IComponent>& comp : m_unpackedView->GetComponents() )
		{
			RED_LOG_INFO( "      Component '%hs' (0x%p)", comp->GetName().AsChar(), comp.Get() );

			IComponent::TBindingList bindings;
			comp->CollectBindingsInfo( bindings );

			for ( const ComponentBindingInfo& it : bindings )
			{
				THandle< IBinding > binding;
				comp->GetBinding( it.m_name, binding );

				RED_LOG_INFO( "         Binding:   prop '%hs' = %hs", it.m_name.AsChar(),
					( binding != nullptr ) ? String::Printf( "'%hs'", binding->GetBindName().AsChar() ).AsChar() : "nullptr" );
			}
		}

		RED_LOG_INFO( "   Binding overrides (%u):", m_template->GetBindingOverrides().Size() );
		for ( const TemplateBindingOverride& bindingOverride : m_template->GetBindingOverrides() )
		{
			RED_LOG_INFO( "      Binding override:   comp '%hs', prop '%hs' = %hs", bindingOverride.m_componentName.AsChar(), bindingOverride.m_propertyName.AsChar(),
				( bindingOverride.m_binding != nullptr ) ? String::Printf( "'%hs'", bindingOverride.m_binding->GetBindName().AsChar() ).AsChar() : "nullptr" );

		}

		RED_LOG_INFO( "   Resolve settings (%u):", m_template->GetComponentResolveSettings().Size() );
		for ( const TemplateComponentResolveSettings& resolveSettings : m_template->GetComponentResolveSettings() )
		{
			Bool usesParam = false;
			String modeName = "Unknown";
			if ( resolveSettings.m_mode == TemplateComponentResolveMode::AutoSelect )
			{
				modeName = "AutoSelect";
			}
			else if ( resolveSettings.m_mode == TemplateComponentResolveMode::Suppress )
			{
				modeName = "Suppress";
			}
			else if ( resolveSettings.m_mode == TemplateComponentResolveMode::Select )
			{
				modeName = "Select";
				usesParam = true;
			}

			if ( usesParam )
			{
				RED_LOG_INFO( "      Resolve setting:   comp '%hs', mode %hs, param '%hs'", resolveSettings.m_componentName.AsChar(),
					modeName.AsChar(), resolveSettings.m_nameParam.AsChar() );
			}
			else
			{
				RED_LOG_INFO( "      Resolve setting:   comp '%hs', mode %hs", resolveSettings.m_componentName.AsChar(), modeName.AsChar() );
			}
		}

		RED_LOG_INFO( "   Backend data overrides (%u):", m_template->GetBackendDataOverrides().Size() );
		for ( const TemplateComponentBackendDataOverrideInfo& backendDataOverride : m_template->GetBackendDataOverrides() )
		{
			RED_LOG_INFO( "      Backend data override:   comp '%hs'", backendDataOverride.m_componentName.AsChar() );
		}

		RED_LOG_INFO( "Printing entity template ended." );
	}

} // ent
