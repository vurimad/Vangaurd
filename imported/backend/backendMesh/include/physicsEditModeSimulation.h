#pragma once

#include "backendMeshPanelSharedData.h"
#include "../../backendEngine/include/genericEditMode.h"

namespace physics
{
	class PhysicsEditorSimulation;
	class SystemResource;
	struct DestructionDesc;
}

namespace tools
{
	struct PhysicsEditModeSimulation_InputListener;
	class PhysicsEditorPreviewPanel;
    class IObjectPersistentCollection;
	struct PhysicsObject;

	// Edit mode for simulation in editor
	class BACKEND_MESH_API PhysicsEditModeSimulation : public GenericEditMode<MeshPanelSharedData>
	{
        RED_BASE_CLASS(GenericEditMode<MeshPanelSharedData>);

	public:
		PhysicsEditModeSimulation( );
		virtual ~PhysicsEditModeSimulation();

		/// IPreviewEditMode interface
		virtual Bool OnInitialize( const PreviewPanelSharedDataPtr& sharedData ) override final;
		virtual Bool OnActivate( IPreviewEditMode* currentMode ) override final;

		virtual void OnDeactivate() override final;
		virtual void OnRender( CRenderFrameInfo& frame, CRenderCameraFrameInfo& cameraInfo ) override;
		virtual void OnUpdate(const UpdateContext& ctx) override;

		virtual void OnSetupInterop( ::interop::utils::Object& op ) override;

		class impl;
		red::UniquePtr<impl> pimpl;
	private:
		void InitializeSceneData( );
		void RecreateRenderingProxy( );
		void RecreateScenePhysicsProxy();

		void RecreateGroundPhysicsProxy();
		void RecreateThrowablePhysicsProxy();

		void CreatePhysicalSystemProxy( PhysicsObject& object );
		void CreatePhysicalDestructionProxy();
		void FillDestructionHierarchyData( const THandle< physics::SystemResource >& dataFractured, physics::DestructionDesc &desc );

		void ThrowObject( const Vector3& pos, const Vector3& dir );
		void ApplyImpulse( const Vector3& pos, const Vector3& dir );
		void WakeUp( const Vector3& pos, const Vector3& dir );

		void OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path );

		red::DynArray< EditorObjectID >						m_capturedIds;
		red::SharedPtr< IObjectPersistentCollection >		m_capturedObjects;
		red::UniquePtr<PhysicsEditModeSimulation_InputListener> m_inputListener;

		// handler for physics simulation. Exists only when the mode is active
		PhysicsObjectResolver*								m_activeResolver;
		tools::SerializableValueNotifier	m_serializableNotifier;

		friend PhysicsEditModeSimulation_InputListener;
	};

}

