
#include "build.h"
#include "AppearancesListInterop.h"
#include "../../../common/interop/include/interopUtilsRTTI.h"
#include "../../../common/resourceMaterial/include/material.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshAppearance.h"
#include "../../backendEngine/include/backendEnginePublic.h"
#include "../../backendEngine/include/editActionHistory.h"
#include "../../../common/redReflection/include/rttiPathParser.h"
#include "meshStructureVisualization.h"
#include "meshDataView.h"

namespace tools
{
	//////////////////////////////////////////////////////////////////////////////////////////////
	class Action_AddApperance : public Action_AddListItem
	{
	public:
		Action_AddApperance( AppearancesListInterop& list, const Uint32 baseAppearanceIndex, const String& name, Bool appearanceCopy = false )
			: Action_AddListItem( list.SharedFromThis() )
			, m_appearanceName( name )
			, m_baseAppearanceIndex( baseAppearanceIndex )
			, m_appearanceCopy( appearanceCopy )
			, m_mesh( list.GetMesh() )
		{}

		virtual String GetDescription() const override { return "Add appearance"; }

		virtual Bool AddItem() override
		{
			mesh::DataView meshView( *m_mesh );
			if ( m_appearanceCopy )
			{
				THandle< mesh::MeshAppearance > sourceAppearance = meshView.GetAppearanceData( m_baseAppearanceIndex );
				return meshView.AddAppearanceCopy( RED_NAME( m_appearanceName ), *sourceAppearance );
			}
			return meshView.AddAppearance( RED_NAME( m_appearanceName ) );
		}

		virtual Bool RemoveItem() override
		{
			return mesh::DataView( *m_mesh ).RemoveAppearance( RED_NAME( m_appearanceName ) );
		}

		virtual Uint32 GetItemIndex() const override
		{
			return mesh::DataViewRO( *m_mesh ).GetAppearanceIndex( m_appearanceName );
		}

	private:
		String m_appearanceName;
		Uint32 m_baseAppearanceIndex;               // Used as source when copying.
		Bool m_appearanceCopy;                      // Set to true when copying.
		const THandle< CMesh >& m_mesh;
	};

	//////////////////////////////////////////////////////////////////////////////////////////////
	class Action_RemoveAppearance : public Action_RemoveListItem
	{
	public:
		Action_RemoveAppearance( AppearancesListInterop& list, const Uint32 index )
			: Action_RemoveListItem( list.SharedFromThis(), index )
			, m_mesh( list.GetMesh() )
			, m_list( list )
		{
			mesh::DataViewRO meshView( *m_mesh );
			m_appearanceName = meshView.GetAppearanceName( index );
			m_appearance = meshView.GetAppearanceData( index );
		}

		virtual String GetDescription() const override { return "Remove appearance"; }

		virtual Bool PerformRemoval() override
		{
			Bool result = mesh::DataView( *m_mesh ).RemoveAppearance( m_appearanceName );
			if ( result && (GetItemIndex() == m_list.GetActiveItemIndex()) )
			{
				m_list.ActivateItem( 0 );
			}
			return result;
		}

		virtual Bool RevertRemoval() override
		{
			return mesh::DataView( *m_mesh ).AddAppearance( m_appearance, GetItemIndex() );
		}

		virtual Uint32 GetItemIndex() const override
		{
			return mesh::DataViewRO( *m_mesh ).GetAppearanceIndex( m_appearanceName );
		}

	private:
		CName m_appearanceName;
		THandle< mesh::MeshAppearance > m_appearance;
		const THandle< CMesh >& m_mesh;
		AppearancesListInterop& m_list;
	};

	//////////////////////////////////////////////////////////////////////////////////////////////
	class Action_RenameAppearance : public Action_RenameListItem
	{
	public:
		Action_RenameAppearance( const String& newName, AppearancesListInterop& list, const Int32 index )
			: Action_RenameListItem( list.SharedFromThis() )
			, m_newAppearanceName( newName )
			, m_mesh( list.GetMesh() )
		{
			m_appearanceName = mesh::DataViewRO( *m_mesh ).GetAppearanceName( index ).AsChar();
		}

		Action_RenameAppearance( const String& newName, AppearancesListInterop& list, const String& materialName )
			: Action_RenameListItem( list.SharedFromThis() )
			, m_newAppearanceName( newName )
			, m_appearanceName( materialName )
			, m_mesh( list.GetMesh() )
		{
		}

		virtual String GetDescription() const override { return "Rename appearance"; }

		virtual Bool PerformRename() override
		{
			mesh::DataView meshView( *m_mesh );
			Int32 appearanceInd = meshView.GetAppearanceIndex( m_appearanceName );
			if ( appearanceInd >= 0 )
			{
				return meshView.RenameAppearance( appearanceInd, RED_NAME( m_newAppearanceName ) );
			}
			return false;
		}

		virtual Bool RevertRename() override
		{
			mesh::DataView meshView( *m_mesh );
			Int32 appearanceInd = meshView.GetAppearanceIndex( m_newAppearanceName );
			if ( appearanceInd >= 0 )
			{
				return meshView.RenameAppearance( appearanceInd, RED_NAME( m_appearanceName ) );
			}
			return false;
		}

	private:
		String m_newAppearanceName;
		String m_appearanceName;
		const THandle< CMesh >& m_mesh;
	};

	//////////////////////////////////////////////////////////////////////////////////////////////
	class AppearanceListItem : public InteropListItem
	{
	public:
		explicit AppearanceListItem( AppearancesListInterop& appearancesList, Uint32 index, IEditActionHistory& actionHistory )
			: InteropListItem( index, appearancesList.SharedFromThis(), actionHistory, "Appearance" )
		{
			::interop::utils::Object::RegisterFunctor<Bool, String>( "addAppearance",
				[&appearancesList, index, &actionHistory]( interop::IContext& ctx, const String& appearanceName )
			{
				Bool result = actionHistory.Execute( red::CreateUniquePtr< Action_AddApperance >( appearancesList, index, appearanceName ) );

				if ( !result )
				{
					ctx.ThrowException( "Couldn't add appearance. Possible reasons: appearance with a given name already exists or wrong appearance index was passed." );
				}

				return result;
			} );

			::interop::utils::Object::RegisterFunctor<Bool, String>( "copyAppearance",
				[&appearancesList, index, &actionHistory]( interop::IContext& ctx, const String& appearanceName )
			{
				Bool result = actionHistory.Execute( red::CreateUniquePtr< Action_AddApperance >( appearancesList, index, appearanceName, true ) );

				if ( !result )
				{
					ctx.ThrowException( "Couldn't copy appearance. Possible reasons: appearance with a given name already exists or wrong appearance index was passed." );
				}

				return result;
			} );
		}
	};

	//////////////////////////////////////////////////////////////////////////////////////////////
	AppearancesListInterop::AppearancesListInterop( IEditActionHistory& actionHistory, ObjectCollection* sharedSelection, const THandle< CMesh > mesh,
	                                                TCallback appearancesChangedCallback, TActivationCallback appearanceActivatedCallback )
		: InteropList( actionHistory, sharedSelection, InteropListSetupInfo( true, true, true ) )
		, m_mesh( mesh )
		, m_appearancesChangedCallback( appearancesChangedCallback )
		, m_appearanceActivatedCallback( appearanceActivatedCallback )
	{
		ActivateItem( 0 );
	}

	AppearancesListInterop::~AppearancesListInterop()
	{
	}

	void AppearancesListInterop::SetMesh( const THandle<CMesh>& mesh )
	{
		m_mesh = mesh;
	}

	Uint32 AppearancesListInterop::GetSize() const
	{
		return mesh::DataViewRO( *m_mesh ).GetNumAppearances();
	}

	::interop::ObjectPtr AppearancesListInterop::GetElement( Uint32 index )
	{
		return ::interop::ObjectPtr( RED_NEW( AppearanceListItem )( *this, index, GetActionHistory() ) );
	}

	ISerializable* AppearancesListInterop::GetItem( Uint32 index ) const
	{
		mesh::DataViewRO meshView( *m_mesh );
		return ( index >= meshView.GetNumAppearances() ) ? nullptr : meshView.GetAppearanceData( index ).Get();
	}

	String AppearancesListInterop::GetItemName( Uint32 index ) const
	{
		return mesh::DataViewRO( *m_mesh ).GetAppearanceName(index).AsChar();
	}

	Bool AppearancesListInterop::IsReadonlyItem( Uint32 index ) const
	{
		return false;
	}

	void AppearancesListInterop::OnListChanged()
	{
		m_appearancesChangedCallback();
	}

	void AppearancesListInterop::OnActivation( Int32 index )
	{
		InteropList::OnActivation( index );

		if ( index >= 0 )
		{
			String appearanceName = GetItemName( static_cast<Uint32>(index) );
			m_appearanceActivatedCallback( RED_NAME( appearanceName ) );
		}
	}

	red::UniquePtr< tools::Action_AddListItem > AppearancesListInterop::GetAddItemAction( const String& newName )
	{
		return red::CreateUniquePtr< Action_AddApperance >( *this, 0, newName );
	}

	red::UniquePtr< tools::Action_RenameListItem > AppearancesListInterop::GetRenameItemAction( Uint32 index, const String& newName )
	{
		return red::CreateUniquePtr< Action_RenameAppearance >( newName, *this, index );
	}

	red::UniquePtr< tools::Action_RemoveListItem > AppearancesListInterop::GetRemoveItemAction( Uint32 index )
	{
		return red::CreateUniquePtr< Action_RemoveAppearance >( *this, index );
	}

	void AppearancesListInterop::OnItemPropertyChanged( const String& name )
	{
		m_appearancesChangedCallback();
	}
} // tools
