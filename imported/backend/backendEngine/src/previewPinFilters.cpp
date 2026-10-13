/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../common/world/include/worldNodePinFilters.h"
#include "../../common/world/include/worldNode.h"
#include "previewPinFilters.h"

namespace tools
{
	class FiltersProperties : public ISerializable
	{
		RTTI_DECLARE_TYPE( FiltersProperties );

	public:
		FiltersProperties() = default;
		FiltersProperties( PreviewPinFilters* filtersController )
			: m_controller( filtersController )
		{
		}

	protected:
		virtual void GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const override
		{
			red::DynArray< const rtti::ClassType* > nodesTypes;
			rtti::ITypeSystem::GetInstance().EnumClasses( ClassID< world::Node >(), nodesTypes );
			red::DynArray< const world::Node* > defaultNodes;
			for ( auto classType : nodesTypes )
			{
				auto node = classType->GetDefaultObject<world::Node>();
				RED_FATAL_ASSERT( node );
				defaultNodes.PushBack( node );
			}

			std::sort( defaultNodes.Begin(), defaultNodes.End(), []( const world::Node* a, const world::Node* b )
			{
				return red::StrcmpNC( a->GetFriendlyName().AsChar(), b->GetFriendlyName().AsChar() ) < 0;
			} );

			for ( auto node : defaultNodes )
			{
				rtti::ClassEditablePropertyInfo info;
				info.m_type = GetTypeObject< Bool >();
				info.m_name = node->GetClass()->GetName();
				outRootProperties.Add( info );
			}
		}

		virtual Bool ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr<rtti::ValueHolder>& outValue ) const override
		{
			if ( !m_controller )
				return false;

			const rtti::ClassType* classType = GetRttiSystem().FindClass( propertyName );
			if ( classType )
			{
				Bool value = m_controller->IsEnabled( *classType );
				return GetTypeObject< Bool >()->ReadValue( IRTTIContext::GetDefault(), &value, rtti::AccessPath(), outValue );
			}

			return false;
		}

		virtual Bool WriteCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr<rtti::ValueHolder>& newValue ) override
		{
			if ( !m_controller )
				return false;

			const rtti::ClassType* classType = GetRttiSystem().FindClass( propertyName );
			if ( classType )
			{
				Bool value;
				if ( GetTypeObject< Bool >()->WriteValue( IRTTIContext::GetDefault(), &value, rtti::AccessPath(), *newValue ) )
				{
					m_controller->SetEnabled( *classType, value );
					return true;
				}
			}

			return false;
		}

	private:
		PreviewPinFilters* m_controller;
	};

	//////////////////////////////////////////////////////////////////////////

	PreviewPinFilters::PreviewPinFilters( IEditActionHistory& actionHistory )
		: m_filtersWrapper( actionHistory )
		, m_filtersProperties( CreateHandle< FiltersProperties >( this ) )
	{
		m_filtersWrapper.SetObject( m_filtersProperties.Get() );
	}

	PreviewPinFilters::~PreviewPinFilters()
	{
	}

	void PreviewPinFilters::Bind( const red::WeakPtr< world::NodePinFilters >& newFilters )
	{
		auto filters = newFilters.Lock();
		if ( filters )
		{
			m_filters = newFilters;
			m_savedFilters = *filters;	
		}
		else
		{
			m_filters.Reset();
			m_savedFilters = world::NodePinFilters();
		}
	}

	Bool PreviewPinFilters::IsEnabled( const rtti::ClassType& nodeType ) const
	{
		auto filters = m_filters.Lock();
		if ( !filters )
			return true;

 		return filters->IsEnabled( nodeType );
	}

	void PreviewPinFilters::SetEnabled( const rtti::ClassType& nodeType, Bool state )
	{
		auto filters = m_filters.Lock();
		if ( !filters )
			return;

		Bool oldState = filters->IsEnabled( nodeType );
		if ( oldState != state )
		{
			if ( state )
				filters->EnableNode( nodeType );
			else
				filters->DisableNode( nodeType );

			m_onChangedNotifier.Notify();
		}
	}

	void PreviewPinFilters::ApplyChanges()
	{
		auto filters = m_filters.Lock();
		if ( !filters )
			return;

		m_savedFilters = *filters;

		m_onChangedNotifier.Notify( String("apply") );
	}

	void PreviewPinFilters::CancelChanges()
	{
		auto filters = m_filters.Lock();
		if ( !filters )
			return;

		*filters = m_savedFilters;

		m_onChangedNotifier.Notify( String( "cancel" ) );
	}

	void PreviewPinFilters::EnableAll()
	{
		auto filters = m_filters.Lock();
		if ( !filters )
			return;

		// Avoid sending a notification if there will be no actual change
		if ( filters->IsAllEnabled() )
			return;

		filters->EnableAll();
		m_filtersWrapper.Refresh();

		m_onChangedNotifier.Notify();
	}

	void PreviewPinFilters::Invert()
	{
		auto filters = m_filters.Lock();
		if ( !filters )
			return;

		filters->Invert();
		m_filtersWrapper.Refresh();

		m_onChangedNotifier.Notify();
	}

	void PreviewPinFilters::SetupInterop( PreviewPinFilters* ptr, ::interop::utils::Object& op )
	{
		op.RegisterObject( "data", ptr->m_filtersWrapper.GetRootInterop() );
		op.RegisterNotifier( "_onChanged", ptr->m_onChangedNotifier );
	}

} // tools

RTTI_BEGIN_TYPE_IN_NAMESPACE( FiltersProperties, tools )
	RTTI_PARENT_TYPE( ISerializable );
RTTI_END_TYPE();