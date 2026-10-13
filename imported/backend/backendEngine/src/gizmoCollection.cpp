/**
* Copyright (c) 2015-2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "gizmoCollection.h"
#include "gizmoGroupTranslate.h"
#include "gizmoGroupRotate.h"
#include "gizmoGroupScale.h"
#include "gizmoTransformHandler.h"
#include "selectionIterator.h"

namespace tools
{

	GizmoCollection::GizmoCollection( GizmoManager* manager, TTransformHandlerFactory transformHandlerFactory )
		: m_gizmoManager( manager )
		, m_transformHandlerFactory( transformHandlerFactory )
		, m_activeGizmo( GizmoType::Translation )
		, m_activeSpace( GizmoSpaceType::World )
	{
	}

	GizmoCollection::~GizmoCollection()
	{
	}

	Bool GizmoCollection::StrideOverGizmos( Uint8 step )
	{
		Uint8 gizmo = static_cast<Uint8>( GetGizmoType() );
		gizmo = ( gizmo + step ) % static_cast<Uint8>( GizmoType::Count );
		return SelectGizmo( static_cast<GizmoType>( gizmo ) );
	}

	void GizmoCollection::CycleGizmos()
	{
		if ( !StrideOverGizmos( 1 ) )
		{
			if ( !StrideOverGizmos( 2 ) )
			{
				StrideOverGizmos( 3 );
			}
		}
	}

	void GizmoCollection::CycleSpaces()
	{
		Uint8 space = static_cast<Uint8>( GetSpaceType() );
		space = ( space + 1 ) % static_cast<Uint8>( GizmoSpaceType::Count );
		SelectSpace( static_cast<GizmoSpaceType>( space ) );
	}

	void GizmoCollection::SelectionChanged( SelectionStatePtr newState )
	{
		m_selection = newState->Clone();
		m_selection->RemoveAll( RED_NAME_CONSTEXPR( "ComponentNode" ) );
		RefreshGizmo();
	}

	Transform GizmoCollection::GetGizmoPlacement() const
	{
		if( m_currTransformHandler )
		{
			return m_currTransformHandler->GetGizmoPlacement();
		}

		return Transform::IDENTITY();
	}

	Bool GizmoCollection::SelectGizmo( GizmoType newGizmo )
	{
		if ( CanSelectGizmo( newGizmo ) )
		{
			// recreate transform handler
			GizmoTransformHandlerPtr transformHandler = m_transformHandlerFactory( m_selection, m_activeSpace );

			if ( transformHandler && transformHandler->CanOverrideGizmoType() )
			{
				newGizmo = transformHandler->GetGizmoType();
				if ( !CanSelectGizmo( newGizmo ) )
					return false;
			}

			//Clearing gizmos
			m_gizmoGroupRotate.Reset();
			m_gizmoGroupTranslate.Reset();
			m_gizmoGroupScale.Reset();
			m_currTransformHandler.Reset();

			if ( transformHandler )
			{
				m_currTransformHandler = transformHandler;
				if ( newGizmo == GizmoType::Translation )
				{
					m_gizmoGroupTranslate = red::CreateSharedPtr< GizmoGroupTranslate >( m_gizmoManager, transformHandler );
				}
				else if ( newGizmo == GizmoType::Rotation )
				{
					m_gizmoGroupRotate = red::CreateSharedPtr< GizmoGroupRotate >( m_gizmoManager, transformHandler );
				}
				else if ( newGizmo == GizmoType::Scale )
				{
					world::NodeScalingMode currentScalingMode = world::NodeScalingMode::All;

					for ( auto it = m_selection->Iterate(); it; ++it )
					{
						Uint8 scalingModeInt = Min( static_cast<Uint8>( currentScalingMode ), static_cast<Uint8>( it->GetScalingMode() ) );
						currentScalingMode = static_cast<world::NodeScalingMode>( scalingModeInt );
					}

					m_gizmoGroupScale = red::CreateSharedPtr< GizmoGroupScale >( m_gizmoManager, transformHandler, currentScalingMode );
				}
				else
				{
					m_currTransformHandler.Reset();
				}

				m_activeGizmo = newGizmo;
				m_gizmoRefreshedNotifier.Notify();

				return true;
			}
		}

		return false;
	}

	void GizmoCollection::SelectSpace( const GizmoSpaceType spaceType )
	{
		if ( m_activeSpace != spaceType )
		{
			m_activeSpace = spaceType;
			RefreshGizmo();

			m_spaceRefreshedNotifier.Notify();
		}
	}

	Bool GizmoCollection::CanSelectGizmo( const GizmoType newGizmo ) const
	{
		if ( !m_selection || m_selection->IsEmpty() || m_selection->IsReadOnly() )
			return false;

		for ( auto it = m_selection->Iterate(); it; ++it )
		{
			if ( !it->SupportsGizmoTransformation() )
				return false;
		}

		if ( newGizmo == GizmoType::Scale )
		{
			for ( auto it = m_selection->Iterate(); it; ++it )
			{
				if ( it->GetScalingMode() == world::NodeScalingMode::None )
					return false;
			}
		}

		return true;
	}

	void GizmoCollection::RefreshGizmo()
	{
		m_gizmoGroupRotate.Reset();
		m_gizmoGroupTranslate.Reset();
		m_gizmoGroupScale.Reset();
		m_currTransformHandler.Reset();

		if ( !SelectGizmo( m_activeGizmo ) )
			CycleGizmos();
	}

	void GizmoCollection::SetupInterop( GizmoCollection* ptr, ::interop::utils::Object& op )
	{
		op.RegisterValue< Int8 >( "type",
			/*GET*/ [ptr]( ::interop::IContext& ctx, Int8& outValue ) { outValue = (Int32)ptr->GetGizmoType(); },
			/*SET*/ [ptr]( ::interop::IContext& ctx, const Int8& newValue ) { ptr->SelectGizmo( (GizmoType) newValue ); },
			&ptr->m_gizmoRefreshedNotifier );

		op.RegisterValue< Int8 >( "space",
								  /*GET*/ [ ptr ]( ::interop::IContext& ctx, Int8& outValue ) { outValue = (Int32)ptr->GetSpaceType(); },
								  /*SET*/ [ ptr ]( ::interop::IContext& ctx, const Int8& newValue ) { ptr->SelectSpace( (GizmoSpaceType)newValue ); },
								  &ptr->m_spaceRefreshedNotifier );

		op.RegisterFunctor<void>( "cycleGizmos", [ptr](::interop::IContext& ctx) { return ptr->CycleGizmos(); } );
		op.RegisterFunctor<void>( "cycleSpaces", [ptr] ( ::interop::IContext& ctx ) { return ptr->CycleSpaces(); } );
	}

} // tools
