/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "../../../common/generalEditorBackend/include/meshEditorInterop.h"

namespace houdini_legacy
{
	class HoudiniEngineSession;
	class IHDAInterfaceWrapper;
}

namespace tools
{
	class MeshPanelSharedData;

	class LODGenerationInterop
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		DECLARE_INTEROP( LODGenerationInterop );

	public:
		explicit LODGenerationInterop( MeshPanelSharedData* sharedData );
		virtual ~LODGenerationInterop();

		Bool HasClothGraphicalParameter() const;

		Float GetAutoHideDistance() const;
		red::DynArray< Float > GetDistances() const;
		LODGenerationInfo& GetLODGenerationInfo() { return m_lodGenerationInfo; }
		red::DynArray< String > GetMaterialNames() const;
		Uint32 GetTriangleCount( Uint32 lodIndex ) const;

		Bool Generate();

	private:
		static const Float s_lodDistanceExponent;

		houdini_legacy::HoudiniEngineSession* StartHoudiniEngine() const;
		Bool LoadHDA( const String& hdaPath, houdini_legacy::HoudiniEngineSession& session ) const;
		Bool SetHDAPropertyValues( houdini_legacy::HoudiniEngineSession& session ) const;

		template< typename T >
		Bool SetHDAPropertyValue( const String& propertyName, T value, houdini_legacy::IHDAInterfaceWrapper& wrapper ) const;

		Bool IsLODDistancesValid() const;
		Bool IsLODTrianglePercentagesValid() const;

		void DisplayError( const String& errorMessage ) const;

		MeshPanelSharedData* m_sharedData;
		LODGenerationInfo m_lodGenerationInfo;

		interop::InteropNotifier m_lodGenerationFinished;
	};
} // world