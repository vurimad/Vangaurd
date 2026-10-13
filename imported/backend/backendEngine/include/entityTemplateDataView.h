/**
* Copyright (c) 2018-2019 CD Projekt Red. All Rights Reserved.
*/
#pragma once

class CMesh;

namespace appearance
{
	class AppearanceDefinition;
}

namespace serialization
{
	class LoadingContext;
}

namespace ent
{
	class Entity;
	class IComponent;
	class IBinding;
	class EntityTemplate;
	class TemplateUnpackedView;
	class TemplateBindingOverride;
	class TemplateInclude;
	class EntityParametersStorage;
	class ComponentsStorage;
	struct ComponentEditorInfo;
	class Assembler;
	class AssemblerTemplateBuilder;
	typedef AssemblerTemplateBuilder* AssemblerTemplateBuilderPtr;
	struct AssemblerValidationError;

	// Editable view for the ent::TemplateResource. Loads all includes, appearances and allows edition of the whole entity.
	class BACKEND_ENGINE_API TemplateDataView										/// @todo DP: find better name, establish good naming pattern for such scenarios
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		/// Single entry on component include path.
		struct ComponentIncludePathEntry
		{
			CName m_includeName;
			THandle< EntityTemplate > m_template;
		};

		/// Entry on includes list.
		struct IncludeEntry
		{
			String m_includePath;                             // This is equal to includeName for direct includes.
			THandle< EntityTemplate > m_template;
		};

		TemplateDataView();
		TemplateDataView( const TemplateDataView& ) = delete;
		~TemplateDataView();

		void operator=( const TemplateDataView& ) = delete;

		void SubmitChanges();

		void Reset();
		void Reset( const THandle< EntityTemplate >& newTemplate, job::Builder& builder, Bool skipUploadRenderData = false );

		void ResetUnpackedView();												/// @todo DP: this is a intermediate clean-up step for entity editor, to be removed later

		// Reload includes and appearances, re-assemble the entity.
		// All resolved components/bindings etc lists will be recreated.
		void Rebuild( job::Builder& builder, Bool refreshComponents = false );

		void EnableCustomLoadingContext();
		void DisableCustomLoadingContext();

		// Return entity template we are editing.
		RED_INLINE const THandle< EntityTemplate >& GetTemplate() const { return m_template; }

		// Get the name of the currently selected appearance.
		RED_INLINE const CName& GetCurrentAppearanceName() const { return m_currentAppearance; }
		/// Set the name of the currently selected appearance.
		void SetCurrentAppearanceName( CName newCurrentAppearanceName );

		// Get the uninitialized entity used by the template view.
		// WARNING: no component initialization was called, attachments are not created,
		//          this is just bare deserialized components list that passed through entity assembly.
		RED_INLINE const THandle< Entity >& GetEditedEntity() const { return m_entity; }

		// Get validation errors reported by the entity assembler.
		const red::DynArray< AssemblerValidationError >& GetAssemblerValidationErrors() const;

		// Get loaded template from the list of included templates.
		// The root template is also handled here.
		const THandle< EntityTemplate >& GetIncludedTemplate( const res::ResourcePath& templatePath ) const;

		////////////////////////////////////////////////////////////////////////////////////////////////
		// Includes

		// Get the list of all includes if this template (also with indirect includes, recursive).
		RED_INLINE const red::DynArray< IncludeEntry >& GetAllIncludesList() const { return m_allIncludesList; }

		// Test if this template has a direct include of a given name.
		Bool HasInclude( CName includeName ) const;

		// Used by editor actions (like Action_AddTemplateInclude).
		Bool AddInclude( CName includeName, const res::ResourcePath& includedTemplatePath, Uint32* outIncludeIndex = nullptr );
		Bool AddInclude( Uint32 includePositionOnList, const TemplateInclude& newInclude );
		Bool RemoveInclude( CName includeName );
		Bool RemoveInclude( Uint32 includeIndex );
		Bool RenameInclude( Uint32 includeIndex, CName newName );
		Bool SetIncludePath( Uint32 includeIndex, const res::ResourcePath& newIncludePath );
		void RemoveIncludes();

		// Test if the includes of this template + optional extra include form a cycle of includes.
		Bool TestTemplateIncludeCycles( const res::ResourcePath* extraInclude = nullptr ) const;

		////////////////////////////////////////////////////////////////////////////////////////////////
		// Components

		// Get component of this template only, it does not count includes.
		// Note, that resolved components might reject some of those components and select an included version instead.
		// So not all components from this list are present in the resolved components list.
		const red::DynArray< THandle< IComponent > >& GetTemplateComponents() const;

		// Get the final resolved components of edited entity template.
		const red::DynArray< THandle< IComponent > >& GetResolvedComponents() const;

		const red::DynArray< ComponentEditorInfo >& GetResolvedComponentsEditorInfo() const;

		const red::DynArray< ComponentEditorInfo >& GetSuppressedComponentsEditorInfo() const;

		// Find component within the entity, also searches in includes. Returns nullptr when not found.
		const THandle< IComponent >& FindComponent( CName componentName ) const;

		const ComponentEditorInfo* GetComponentInfoForComponent( CName componentName ) const;

		Bool IsComponentNameUsed( const AnsiChar* componentName, Bool checkAlsoIncludedComponents = true ) const;
		CName GetBestComponentName( CName existingName, const rtti::ClassType* componentClass, Bool allowOverridingIncludedComponent ) const;

		// Create initial component name.
		CName GetInitialComponentName( const rtti::ClassType* componentClass ) const;

		// Some components can have only one instance per entity, see IComponent::AllowOnlyOneInstancePerEntity.
		// Used by editor actions (like EntityAction_AddComponent), this is used when adding new node to the graph.
		Bool CanCreateComponent( const rtti::ClassType* componentClass ) const;

		Bool AddComponent( const THandle< IComponent >& newComponent, Bool allowOverridingIncludedComponent = false );
		Bool RemoveComponent( const THandle< IComponent >& componentToRemove );

		Uint32 GetNumComponentVersions( CName componentName ) const;

		const THandle<EntityTemplate>& GetComponentAppearanceTemplate( const THandle< IComponent >& component ) const;

		THandle<EntityTemplate> GetComponentOriginTemplate( const THandle< IComponent >& component, Bool recurse = true ) const;

		// If component comes from this template (not from include) empty list is returned.
		void ListComponentIncludePath( red::DynArray< ComponentIncludePathEntry >& outIncludePath, const THandle< IComponent >& component,
		                               const ComponentEditorInfo& componentEditorInfo ) const;

		// Test if given component comes from appearance.
		Bool IsComponentDefinedInAppearance( const THandle< IComponent >& component ) const;

		// Test if given included component has overriden properties
		Bool IsOverridenIncludedComponent( const THandle< IComponent >& component ) const;


		////////////////////////////////////////////////////////////////////////////////////////////////
		// Bindings

		// Find binding override in the current template. Search among resolved overrides. This is used by graphs editor.
		const TemplateBindingOverride* FindResolvedBindingOverride( CName componentName, CName bindingSlotName ) const;

		Bool GetComponentBinding( const THandle< IComponent >& component, CName bindingPropertyName, THandle< IBinding >& outBinding ) const;

		// These two methods use TemplateBindingOverride as a regular binding description (for all bindings, not only overridden ones).
		void GetComponentBindings( CName componentName, red::DynArray< TemplateBindingOverride >& outBindings ) const;
		void GetComponentBindings( const THandle< IComponent >& component, red::DynArray< TemplateBindingOverride >& outBindings ) const;

		void SetComponentBinding( const THandle< IComponent >& component, const THandle< EntityTemplate >& entityTemplateOfComponent,
		                          CName bindingPropertyName, const THandle< IBinding >& newBinding, Bool overrideBinding = false );

		// Unpack binding into source/destination component handles and destination binding slot name, useful for creating graph connections.
		void UnpackBinding( const TemplateBindingOverride& bindingInfo, THandle< IComponent >& outSourceComponent,
		                    THandle< IComponent >& outDestinationComponent, CName& outDestinationBindingSlotName, Bool& outIsEnabled ) const;

		// Test if given binding is defined in include or in this template (defined as new or overridden). Appearances here also count as includes.
		Bool IsComponentBindingDefinedInInclude( CName componentName, CName bindingSlotName ) const;


		////////////////////////////////////////////////////////////////////////////////////////////////
		// Various

		// Get access to entity appearance parameters.
		const THandle< EntityParametersStorage >& GetAppearanceParameters() const { return m_appearanceParameters; }

		// Remove resolve setting defined for missing includes or missing components.
		// Returns the number of resolve settings removed, when 0 means there was nothing to remove.
		Uint32 RemoveDanglingResolveSettings( Bool doNotRemoveJustCheck = false );
		// Remove binding overrides defined for missing components (or components defined in current template).
		// Returns the number of binding overrides removed, when 0 means there was nothing to remove.
		Uint32 RemoveDanglingBindingOverrides( Bool doNotRemoveJustCheck = false );
		// Remove backend data overrides defined for missing components (or components defined in current template).
		// Returns the number of backend data overrides removed, when 0 means there was nothing to remove.
		Uint32 RemoveDanglingBackendDataOverrides( Bool doNotRemoveJustCheck = false );


		// Print debug info about the entity template to the log.
		void DebugPrintEntityTemplate() const;

		// Generate string meant to become a suffix of a component name, a salting suffix.
		// It will probably be like a random digits string.
		// It was made public, since some of our tools use this.
		static String GenerateComponentNameRandomSuffixString( Uint32 numChars );


	private:
		void LoadTemplateIncludes( job::Builder& builder );

		void LoadTemplateAppearanceParts( job::Builder& builder );

		void UpdateAllIncludesList();
		void UpdateTemplateComponentsMap( job::Builder& builder );

		AssemblerTemplateBuilderPtr AddTemplateToAssembler( const THandle< EntityTemplate >& templateResource,
		                                                    const THandle< ComponentsStorage >& templateComponents );

		THandle< EntityTemplate > GetComponentDirectIncludeTemplate( const THandle< IComponent >& component ) const;

		THandle <EntityTemplate > GetComponentOriginTemplate( const THandle< EntityTemplate >& templateResource,
		                                                        const THandle< IComponent >& component,
		                                                        const ComponentEditorInfo& componentEditorInfo,
		                                                        Bool recurse = true ) const;

		void CorrectResolveSettingsAfterIncludeRemoval( CName nameOfRemovedInclude );
		void CorrectBindingOverridesAfterIncludeRemoval( CName nameOfRemovedInclude );
		void CorrectBackendDataOverridesAfterIncludeRemoval( CName nameOfRemovedInclude );

	private:
		THandle< EntityTemplate > m_template;                     ///< The template resource we are editing.
		Bool m_skipUploadRenderData;

		red::SharedPtr< TemplateUnpackedView > m_unpackedView;      ///< Editable view of the template object (only components from this resource)

	public:																				/// @todo DP: make this private
		THandle< Entity > m_entity;                                 ///< The entity object template + components, all uninitialized.

	private:

		void UpdateNamesMap( job::Builder& builder );
		Bool UpdateBindName( const CName& oldName, const CName& newName );

		red::SharedPtr< Assembler > m_assembler;                    ///< Assembler used to assemble & resolve entity components.
		AssemblerTemplateBuilderPtr m_mainTemplateBuilder;          ///< Assembler's builder for the main template. It does not contain appearance components.
		AssemblerTemplateBuilderPtr m_finalTemplateBuilder;         ///< Final builder, basically the same as m_mainTemplateBuilder, but with appearances added. When there are no apperances thus is the same as m_mainTemplateBuilder.

		red::RWLock m_includesMapLock;
		red::HashMap< res::ResourcePath, THandle<EntityTemplate> > m_includesMap;

		red::HashMap< THandle<EntityTemplate>, THandle<ComponentsStorage> > m_templatesComponentsMap;

		red::DynArray< IncludeEntry > m_allIncludesList;

		CName m_currentAppearance;
		Bool m_isAppearanceDirty;
		red::RWLock m_appearancePartsLock;
		red::DynArray< THandle<EntityTemplate> > m_appearanceParts;
		red::DynArray< THandle<ComponentsStorage> > m_appearancePartsComponents;
		red::DynArray< THandle<appearance::AppearanceDefinition> > m_appearancePartAppearances;		/// @todo DP: this should not be an array
		THandle< EntityParametersStorage > m_appearanceParameters;
		THandle< CMesh > m_appearanceProxyMesh;

		red::HashMap< red::RUID, CName > m_namesMap;

		red::UniquePtr<serialization::LoadingContext> m_customLoadingContext;
	};
}
 // ent
