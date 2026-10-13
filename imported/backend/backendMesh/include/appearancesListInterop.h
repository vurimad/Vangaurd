#pragma once

#include "../../../common/interop/include/interopUtilsManual.h"
#include "../../../common/redReflection/include/serializableValueNotifier.h"

#include "../../../backend/backendEngine/include/interopList.h"
#include "../../../backend/backendMesh/include/meshDataView.h"

namespace tools
{
	class IMeshVisualizationContainer;

	class AppearancesListInterop : public InteropList
	{
	public:
		typedef std::function<void( void )> TCallback;
		typedef std::function<void( CName )> TActivationCallback;

		AppearancesListInterop( IEditActionHistory& actionHistory, ObjectCollection* sharedSelection, const THandle< CMesh > mesh,
		                        TCallback appearancesChangedCallback, TActivationCallback appearanceActivatedCallback );
		virtual ~AppearancesListInterop();

		void SetMesh( const THandle<CMesh>& mesh );

		RED_INLINE const THandle< CMesh >& GetMesh() { return m_mesh; }

		virtual ISerializable* GetItem( Uint32 index ) const override;
		virtual String GetItemName( Uint32 index ) const override;
		virtual Bool IsReadonlyItem( Uint32 index ) const override;

	protected:
		virtual Uint32 GetSize() const override;
		virtual ::interop::ObjectPtr GetElement( Uint32 ind ) override;

		virtual void OnItemPropertyChanged( const String& name ) override;
		virtual void OnActivation( Int32 index ) override;
		virtual void OnListChanged() override;

	private:
		virtual red::UniquePtr< Action_AddListItem > GetAddItemAction( const String& name ) override;
		virtual red::UniquePtr< Action_RenameListItem > GetRenameItemAction( Uint32 index, const String& newName ) override;
		virtual red::UniquePtr< Action_RemoveListItem > GetRemoveItemAction( Uint32 index ) override;

		THandle< CMesh > m_mesh;

		TCallback m_appearancesChangedCallback;
		TActivationCallback m_appearanceActivatedCallback;
	};

}
