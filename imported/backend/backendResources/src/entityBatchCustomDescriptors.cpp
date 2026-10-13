#include "build.h"

#include "../../backendEngine/include/batchTool.h"
#include "../../../common/redJobs2/include/jobBuilder.h"
#include "../../../common/worldEntities/include/entityComponent.h"
#include "../../../common/worldEntities/include/entityTemplate.h"
#include "../../backendEngine/include/entityTemplateDataView.h"
#include "../../backendEntities/include/entityResourceEditor.h"
#include "../../../common/redReflection/include/editableView.h"
#include "../../../common/redReflection/include/rttiPathParser.h"
#include "../../../common/redReflection/include/handle.h"

namespace tools
{
	enum class VisaulTagSelectorOperation : Uint32
	{
		SetSchema = 0u,
		SetSchemaAndTags,

		AddTags,
		RemoveTags,
		OverrideTags,
	};

	struct VisualTagSelector
	{
		RTTI_DECLARE_TYPE( VisualTagSelector );

		VisualTagSelector()
		{
			m_visualTagsHolder = CreateHandle< VisualTagsHolder >();
			m_visualTagsSchema = CreateHandle< ent::VisualTagsSchema >();

			m_visualTagsHolder->m_holder = m_visualTagsSchema;
		}

		VisaulTagSelectorOperation m_visualTagOperation;
		THandle< VisualTagsHolder > m_visualTagsHolder;

		THandle< ent::VisualTagsSchema > m_visualTagsSchema;
	};

	class EntityCustomPropertiesDescriptor : public IResourceCustomPropertiesDescriptor
	{
		RTTI_DECLARE_TYPE( EntityCustomPropertiesDescriptor );

	public:
		EntityCustomPropertiesDescriptor();

		virtual void ApplyChanges( const job::RunContext& runContext ) const override;
		virtual Bool RequiresMainThread() const override { return true; }

		virtual Bool SupportsResource( const rtti::ClassType* resourceClass ) const;
		virtual const String& GetTitle() const override;

	private:
		THandle< ent::IComponent > m_componentTemplate;
		VisualTagSelector m_visualTagSelector;
	};

	EntityCustomPropertiesDescriptor::EntityCustomPropertiesDescriptor()
	{
	}

	void EntityCustomPropertiesDescriptor::ApplyChanges( const job::RunContext& runContext ) const
	{
		static CName componentName = RED_NAME_CONSTEXPR( "componentTemplate" );
		static CName visaulTagsName = RED_NAME_CONSTEXPR( "visualTagSelector.visualTagOperation" );

		if ( m_switches.KeyExist( componentName ) && m_switches[ componentName ] )
		{
			// runContext is invalid, we're on the main thread
			job::Builder builder;

			red::DynArray< red::SharedPtr< ent::TemplateDataView > > templateViews;
			templateViews.Resize( m_resources.Size() );

			builder.DispatchParallelForJob( "ChangeComponentsProperties", { m_resources.Size() },
				[ this, &templateViews ]( Uint32 index, const job::RunContext& context )
			{
				THandle< ent::EntityTemplate > entityTemplate = Cast< ent::EntityTemplate >( m_resources[ index ] );
				if ( !entityTemplate )
				{
					return;
				}

				job::Builder builder( context );
				templateViews[ index ] = red::CreateSharedPtr< ent::TemplateDataView >();
				red::SharedPtr< ent::TemplateDataView >& templateView = templateViews[ index ];

				templateView->Reset( entityTemplate, builder );
				templateView->Rebuild( builder );

				builder.DispatchJob( "ChangeComponentsProperties_TemplateViewLoaded", [ this, templateView ]( const job::RunContext& runContext )
				{
					tools::EditableViewPtr templateComponentView = m_componentTemplate->CreateView();
					for ( const ent::ComponentHandle& component : templateView->GetTemplateComponents() )
					{
						if ( component->IsExactlyA( m_componentTemplate->GetClass() ) )
						{
							tools::EditableViewPtr componentView = component->CreateView();
							for ( const auto& pair : m_switches )
							{
								const Bool isToggled = pair.Value();
								if ( isToggled )
								{
									const CName name = pair.Key();
									const rtti::AccessPath accessPath = rtti::AccessPath()[ name ];

									rtti::PathParser parser( accessPath );
									parser.EatName( CName() ); // discard 'componentTemplate.' part
									const rtti::AccessPath propertyPath = parser.GetUneatenPath();
									if ( !propertyPath.IsEmpty() )
									{
										rtti::ValuePtr newValue;
										templateComponentView->GetValue( IRTTIContext::GetDefault(), propertyPath, newValue );

										rtti::ValuePtr oldValue;
										componentView->GetValue( IRTTIContext::GetDefault(), propertyPath, oldValue );

										componentView->SetValue( IRTTIContext::GetDefault(), propertyPath, oldValue, newValue );
									}
								}
							}
						}
					}
				} );
			} );

			job::FlushCounter( builder.ExtractWaitCounter() );

			for ( const auto& view : templateViews )
			{
				view->SubmitChanges();
			}
		}
		else if ( m_switches.KeyExist( visaulTagsName ) && m_switches[ visaulTagsName ] )
		{
			// runContext is invalid, we're on the main thread
			job::Builder builder;

			red::DynArray< red::SharedPtr< ent::TemplateDataView > > templateViews;
			templateViews.Resize( m_resources.Size() );

			builder.DispatchParallelForJob( "ChangeVisualTags", { m_resources.Size() },
				[ this, &templateViews ]( Uint32 index, const job::RunContext& context )
			{
				THandle< ent::EntityTemplate > entityTemplate = Cast< ent::EntityTemplate >( m_resources[ index ] );
				if ( !entityTemplate )
				{
					return;
				}

				if ( m_visualTagSelector.m_visualTagOperation == VisaulTagSelectorOperation::SetSchema )
				{
					THandle< ent::VisualTagsSchema > schema = entityTemplate->Editor_GetVisualTagsSchema();
					schema->m_schema = m_visualTagSelector.m_visualTagsSchema->m_schema;
				}
				else if ( m_visualTagSelector.m_visualTagOperation == VisaulTagSelectorOperation::SetSchemaAndTags )
				{
					THandle< ent::VisualTagsSchema > schema = entityTemplate->Editor_GetVisualTagsSchema();
					schema->m_schema = m_visualTagSelector.m_visualTagsSchema->m_schema;
					schema->m_visualTags.AddTags( m_visualTagSelector.m_visualTagsSchema->m_visualTags );
				}
				else if ( m_visualTagSelector.m_visualTagOperation == VisaulTagSelectorOperation::AddTags )
				{
					THandle< ent::VisualTagsSchema > schema = entityTemplate->Editor_GetVisualTagsSchema();
					schema->m_visualTags.AddTags( m_visualTagSelector.m_visualTagsSchema->m_visualTags );
				}
				else if ( m_visualTagSelector.m_visualTagOperation == VisaulTagSelectorOperation::RemoveTags )
				{
					THandle< ent::VisualTagsSchema > schema = entityTemplate->Editor_GetVisualTagsSchema();
					schema->m_visualTags.RemoveTags( m_visualTagSelector.m_visualTagsSchema->m_visualTags );
				}
				else if ( m_visualTagSelector.m_visualTagOperation == VisaulTagSelectorOperation::OverrideTags )
				{
					THandle< ent::VisualTagsSchema > schema = entityTemplate->Editor_GetVisualTagsSchema();
					schema->m_visualTags = m_visualTagSelector.m_visualTagsSchema->m_visualTags;
				}
			} );

			job::FlushCounter( builder.ExtractWaitCounter() );
		}
	}

	Bool EntityCustomPropertiesDescriptor::SupportsResource( const rtti::ClassType* resourceClass ) const
	{
		return resourceClass->IsA< ent::EntityTemplate >();
	}

	const String& EntityCustomPropertiesDescriptor::GetTitle() const
	{
		static const String s_title( "Components" );
		return s_title;
	}
}

RTTI_BEGIN_ENUM_IN_NAMESPACE( VisaulTagSelectorOperation, tools )
	RTTI_ENUM_OPTION( SetSchema );
	RTTI_ENUM_OPTION( SetSchemaAndTags );
	RTTI_ENUM_OPTION( AddTags );
	RTTI_ENUM_OPTION( RemoveTags );
	RTTI_ENUM_OPTION( OverrideTags );
RTTI_END_ENUM();

RTTI_BEGIN_TYPE_IN_NAMESPACE( EntityCustomPropertiesDescriptor, tools );
	RTTI_PARENT_TYPE( tools::IResourceCustomPropertiesDescriptor );
	RTTI_PROPERTY( m_componentTemplate ).editable().inlined();
	RTTI_PROPERTY( m_visualTagSelector ).editable().inlined();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( VisualTagSelector, tools );
	RTTI_PROPERTY( m_visualTagOperation ).editable();
	RTTI_PROPERTY( m_visualTagsHolder ).editable().inlined();
RTTI_END_TYPE();
