/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "editorComponentResolveHelper.h"
#include "../../common/worldEntities/include/entityTemplateInclude.h"
#include "../../common/worldEntities/include/entityAssembler.h"         // For ent::ComponentEditorInfo.

using red::DynArray;
using ent::IComponent;
using ent::EntityTemplate;
using ent::TemplateComponentResolveMode;
using ent::TemplateComponentResolveSettings;
using ent::AssemblerTemplateBuilder;
using ent::ComponentEditorInfo;

namespace tools
{
	void SetComponentResolveSettings( const THandle<EntityTemplate>& templateResource,
	                                  const TemplateComponentResolveSettings& newResolveSettings )
	{
		EntityTemplate::TComponentResolveSettings& componentResolveSettings = templateResource->GetComponentResolveSettings();

		for ( TemplateComponentResolveSettings& settings : componentResolveSettings )
		{
			if ( settings.m_componentName == newResolveSettings.m_componentName )
			{
				settings = newResolveSettings;
				return;
			}
		}

		// Settings for this component were not found, adding new settings.
		componentResolveSettings.PushBack( newResolveSettings );

						/// @todo DP: if we use HashMap inside TemplateResource, we need to update it here. Maybe just move this to TemplateResource...
											/// @todo DP: if we set a default mode we probably should remove the setting to save file space
	}

	void ClearComponentResolveSettings( const THandle<ent::EntityTemplate>& templateResource, const CName& componentName )
	{
		EntityTemplate::TComponentResolveSettings& componentResolveSettings = templateResource->GetComponentResolveSettings();

		for ( Uint32 i = 0; i < componentResolveSettings.Size(); ++i )
		{
			if ( componentResolveSettings[i].m_componentName == componentName )
			{
				componentResolveSettings.RemoveAt( i );
				return;
			}
		}
	}

	Int32 ListPossibleComponentResolveOptions( DynArray<ComponentResolveOption>& outOptions,
	                                            const THandle<IComponent>& component,
	                                            const ComponentEditorInfo& componentEditorInfo )
	{
		if ( componentEditorInfo.m_includedVersions.Empty() )
		{
			return -1;
		}

		Int32 selectedOption = 0;

		TemplateComponentResolveMode mode = TemplateComponentResolveMode::AutoSelect;
		if ( componentEditorInfo.m_resolveSettings != nullptr )
			mode = componentEditorInfo.m_resolveSettings->m_mode;

		TemplateComponentResolveSettings settings;
		settings.m_componentName = componentEditorInfo.m_componentName;

		{
			if ( mode == TemplateComponentResolveMode::AutoSelect )								/// @todo DP: make this selection simpler
				selectedOption = outOptions.Size();

			settings.m_mode = TemplateComponentResolveMode::AutoSelect;
			settings.m_nameParam = CName::NONE();
			outOptions.PushBack( ComponentResolveOption{ "Merge", settings, "Automatically merge all the nodes together" } );
		}

		{
			if ( mode == TemplateComponentResolveMode::Suppress )
				selectedOption = outOptions.Size();

			settings.m_mode = TemplateComponentResolveMode::Suppress;
			settings.m_nameParam = CName::NONE();
			outOptions.PushBack( ComponentResolveOption{ "Suppress", settings, "Suppress the node, it will not be used" } );
		}

		if ( componentEditorInfo.m_currentVersion != nullptr )
		{
			if ( ( mode == TemplateComponentResolveMode::Select ) && componentEditorInfo.m_resolveSettings->m_nameParam.Empty() )
				selectedOption = outOptions.Size();

			settings.m_mode = TemplateComponentResolveMode::Select;
			settings.m_nameParam = CName::NONE();
			outOptions.PushBack( ComponentResolveOption{ "...this template", settings } );
		}

		for ( const ent::ComponentEditorInfo::ComponentEntry& includeEntry : componentEditorInfo.m_includedVersions )
		{
			if ( (mode == TemplateComponentResolveMode::Select) && (componentEditorInfo.m_resolveSettings->m_nameParam == includeEntry.m_include->GetName()) )
				selectedOption = outOptions.Size();

			settings.m_mode = TemplateComponentResolveMode::Select;
			settings.m_nameParam = includeEntry.m_include->GetName();
			outOptions.PushBack( ComponentResolveOption{ String::Printf( "...include %hs", settings.m_nameParam.AsChar() ), settings } );
		}

		return selectedOption;
	}

	void SetComponentBackendDataOverride( const THandle<EntityTemplate>& templateResource,
		const ent::TemplateComponentBackendDataOverrideInfo& newBackendDataOverride )
	{
		EntityTemplate::TBackendDataOverrides& backendDataOverride = templateResource->GetBackendDataOverrides();

		for ( ent::TemplateComponentBackendDataOverrideInfo& info : backendDataOverride )
		{
			if ( info.m_componentName == newBackendDataOverride.m_componentName )
			{
				info = newBackendDataOverride;
				return;
			}
		}

		// Override for this component was not found, adding new override info.
		backendDataOverride.PushBack( newBackendDataOverride );
	}

	void ClearComponentBackendDataOverride( const THandle<EntityTemplate>& templateResource,
		const CName& componentName )
	{
		EntityTemplate::TBackendDataOverrides& backendDataOverride = templateResource->GetBackendDataOverrides();

		for ( Uint32 i = 0; i < backendDataOverride.Size(); ++i )
		{
			if ( backendDataOverride[i].m_componentName == componentName )
			{
				backendDataOverride.RemoveAt( i );
				return;
			}
		}
	}

	void SetBindingOverride( const THandle<ent::EntityTemplate>& templateResource, const ent::TemplateBindingOverride& newBindingOverride )
	{
		RED_LOG_INFO( "Setting biding override:   component '%hs',   property '%hs'", newBindingOverride.m_componentName.AsChar(), newBindingOverride.m_propertyName.AsChar() );

		EntityTemplate::TBindingOverrides& bindingOverrides = templateResource->GetBindingOverrides();

		for ( Uint32 i = 0; i < bindingOverrides.Size(); ++i )
		{
			ent::TemplateBindingOverride& bindingOverride = bindingOverrides[i];

			if ( ( bindingOverride.m_componentName == newBindingOverride.m_componentName ) &&
			     ( bindingOverride.m_propertyName == newBindingOverride.m_propertyName ))
			{
				bindingOverride.m_binding = newBindingOverride.m_binding;
				return;
			}
		}

		// Settings for this component were not found, adding new settings.
		bindingOverrides.PushBack( newBindingOverride );

		/// @todo DP: if we use HashMap inside TemplateResource, we need to update it here. Maybe just move this to TemplateResource...
	}

	void SetBindingOverride( const THandle<ent::EntityTemplate>& templateResource, CName componentName, CName bindingPropertyName,
	                         const THandle<ent::IBinding>& binding )
	{
		ent::TemplateBindingOverride bindingOverride{ componentName, bindingPropertyName, binding };
		SetBindingOverride( templateResource, bindingOverride );
	}

	void RemoveBindingOverride( const THandle<ent::EntityTemplate>& templateResource, CName componentName, CName bindingPropertyName )
	{
		RED_LOG_INFO( "Removing biding override:   component '%hs',   property '%hs'", componentName.AsChar(), bindingPropertyName.AsChar() );

		EntityTemplate::TBindingOverrides& bindingOverrides = templateResource->GetBindingOverrides();

		for ( Uint32 i = 0; i < bindingOverrides.Size(); ++i )
		{
			ent::TemplateBindingOverride& bindingOverride = bindingOverrides[i];
			if ( (bindingOverride.m_componentName == componentName) && (bindingOverride.m_propertyName == bindingPropertyName) )
			{
				bindingOverrides.RemoveAt( i );
				return;
			}
		}
	}

	void RemoveBindingOverrides( const THandle<ent::EntityTemplate>& templateResource, CName componentName )
	{
		EntityTemplate::TBindingOverrides& bindingOverrides = templateResource->GetBindingOverrides();

		for ( Uint32 i = 0; i < bindingOverrides.Size(); ++i )
		{
			ent::TemplateBindingOverride& bindingOverride = bindingOverrides[i];
			if ( bindingOverride.m_componentName == componentName )
			{
				bindingOverrides.RemoveAt( i );
				return;
			}
		}
	}

} // tools
