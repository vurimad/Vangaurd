/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/worldEntities/include/entityComponent.h"
#include "../../../common/worldEntities/include/entityTemplate.h"          // For ent::TemplateComponentResolveSettings.

namespace ent
{
	class IComponent;
	class EntityTemplate;
	class TemplateComponentResolveSettings;
	enum class TemplateComponentResolveMode : Uint8;
	struct ComponentEditorInfo;
}

namespace tools
{
	class EntityResourceEditor;

	/// One of available options for given component resolve settings.
	struct ComponentResolveOption
	{
		String m_label;
		ent::TemplateComponentResolveSettings m_resolveSettings;
		String m_tooltip;
	};

	extern BACKEND_ENGINE_API void SetComponentResolveSettings( const THandle<ent::EntityTemplate>& templateResource,
	                                                              const ent::TemplateComponentResolveSettings& newResolveSettings );

	extern BACKEND_ENGINE_API void ClearComponentResolveSettings( const THandle<ent::EntityTemplate>& templateResource,
																	const CName& componentName );

	/// Returns index of currently selected option.
	extern BACKEND_ENGINE_API Int32 ListPossibleComponentResolveOptions( red::DynArray<ComponentResolveOption>& outOptions,
	                                                                        const THandle<ent::IComponent>& component,
	                                                                        const ent::ComponentEditorInfo& componentEditorInfo );

	extern BACKEND_ENGINE_API void SetComponentBackendDataOverride( const THandle<ent::EntityTemplate>& templateResource,
																	  const ent::TemplateComponentBackendDataOverrideInfo& newOverrideInfo );

	extern BACKEND_ENGINE_API void ClearComponentBackendDataOverride( const THandle<ent::EntityTemplate>& templateResource,
																		const CName& componentName );
	extern BACKEND_ENGINE_API void SetBindingOverride( const THandle<ent::EntityTemplate>& templateResource,
	                                                     const ent::TemplateBindingOverride& newBindingOverride );

	extern BACKEND_ENGINE_API void SetBindingOverride( const THandle<ent::EntityTemplate>& templateResource,
	                                                     CName componentName, CName bindingPropertyName, const THandle<ent::IBinding>& binding );

	extern BACKEND_ENGINE_API void RemoveBindingOverride( const THandle<ent::EntityTemplate>& templateResource,
	                                                        CName componentName, CName bindingPropertyName );

	extern BACKEND_ENGINE_API void RemoveBindingOverrides( const THandle<ent::EntityTemplate>& templateResource, CName componentName );

} // tools
