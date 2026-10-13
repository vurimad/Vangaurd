/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "physicsShapeRenderer.h"
#include "../../../common/redReflection/include/types.h"
#include "../../../common/redReflection/include/rttiClassBuilder.h"
#include "../../../common/redReflection/include/serializable.h"
#include "../../../common/renderData/include/renderSelectable.h"
#include "../../../common/redMemory/include/sharedPtr.h"
#include "../../../common/redContainers/include/dynArray.h"
#include "../../../common/interop/include/interopUtilsManual.h"
#include "../../../common/redSystem/include/redThreadsAtomic.h"
#include "../../../common/redMemory/include/atomicWeakPtr.h"

namespace tools
{
	class IMeshPhysicsStructureNode;
	class IEditableProxy;
	class IEditableView;
}

namespace rend
{
	class IDebugDrawer;
	class FragmentBuilder;
}

namespace physics
{
	//////////////////////////////////////////////////////////////////////////	 

	namespace prv
	{
		class PhysicsDecorationBaseEditableProxy;
	}

	class PhysicsEditorScene;
	class PhysicsBase;
	class IPhysicsNotificationHandler;
	using PhysicsBasePtr = THandle< PhysicsBase >;
	using PhysicsBaseWeakPtr = WeakHandle<PhysicsBase>;
	using PhysicsBaseID = tools::EditorObjectInternalID;

	//////////////////////////////////////////////////////////////////////////	 

	struct SPhysicsTolerancesScale
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

		Float			m_length;
		Float			m_mass;
		Float			m_speed;

		RED_INLINE SPhysicsTolerancesScale::SPhysicsTolerancesScale()
			: m_length( 1 )
			, m_mass( 1000 )
			, m_speed( 10 )
		{}

		RED_INLINE Bool SPhysicsTolerancesScale::IsValid() const
		{
			return m_length > 0 && m_mass > 0;
		}
	};

	// --- PhysicsBase ---

	enum EPhysicsBaseType : Uint8
	{
		EPBT_Actor,
		EPBT_Shape,
		EPBT_Joint,
		EPBT_Mesh,
		EPBT_Base,
		EPBT_COM,
		EPBT_Max
	};

	typedef Uint32 PhysicsBaseFlag;
	enum EPhysicsBaseFlags : Uint32
	{
		EPBF_WasModified = RED_FLAG( 0 ),		///	Was it modified and requires processing?
		EPBF_SelectionVisualizationEnabled = RED_FLAG( 1 ), 		/// Is the selection visualization enabled ?
		EPBF_IsSelected = RED_FLAG( 2 ), 		/// Is it selected
		EPBF_ScheduleWakeUp = RED_FLAG( 3 ),
		EPBF_SimulationRunning = RED_FLAG( 4 ),
		EPBF_RequestRenderingRebuild = RED_FLAG( 5 ),
		EPBF_IsVisible = RED_FLAG( 6 ),

		EPBF_Reserved_1 = RED_FLAG( 30 ),
		EPBF_Reserved_2 = RED_FLAG( 29 ),
		EPBF_Reserved_3 = RED_FLAG( 28 ),
		EPBF_Reserved_4 = RED_FLAG( 27 ),
		EPBF_Reserved_5 = RED_FLAG( 26 ),
		EPBF_Reserved_6 = RED_FLAG( 25 )
	};

	class BACKEND_MESH_API PhysicsBase : public ISerializable, public IRenderSelectable
	{
		RED_BASE_CLASS( ISerializable );
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RED_EMPTY_DEFAULT_CONSTRUCTOR( PhysicsBase );
		RTTI_DECLARE_TYPE( PhysicsBase );
		DECLARE_INTEROP( PhysicsBase );

	public:
		PhysicsBase( PhysicsBaseID id, PhysicsEditorScene* editorParent = nullptr );
		PhysicsBase( PhysicsBaseID id, EPhysicsBaseType type, PhysicsEditorScene* editorParent = nullptr );

		virtual ~PhysicsBase();

		virtual const PhysicsBaseID					GetPhysicsObjectID() const
		{
			return m_physicsBaseId;
		}

		virtual void								OnUpdate();
		virtual void								OnRender( rend::IDebugDrawer& debugDrawer, PhysicsRendererOptionsPtr options );
		virtual void								OnPostLoad( const PostLoadContext& context ) override;
		virtual void								OnRestore(PhysicsEditorScene*  scene );
		virtual void								OnRemove();
		virtual void								PrepareForRendering( PhysicsRendererOptionsPtr options );

		/// Get the selectable interface
		RED_FORCE_INLINE const IRenderSelectable*	GetSelectableObject() const
		{
			return static_cast<const IRenderSelectable*>( this );
		}

		/// Get current local to world transform, this is up to date most of the time (unless the callbacks failed)
		RED_FORCE_INLINE const Matrix&				GetLocalToWorld() const
		{
			return m_localToWorld;
		}

		/// Set current local to world transform, this is up to date most of the time (unless the callbacks failed)
		virtual void								SetLocalToWorld( const Matrix& localToWorld );
		virtual Bool SetScale( const Vector3& newScale );
		virtual Vector3 GetScale() const;

		virtual void SetVisible( Bool decision );

		void NotifyTransformChanged();

		// toggle the selection for this PhysicsDecorationBase
		virtual void								ToggleSelectionVisualization( const Bool drawAsSelected );
		void NotifySelectionHandlers();

		/// Create an editable proxy for this PhysicsDecorationBase
		virtual red::SharedPtr< tools::IEditableProxy > CreateEditableProxy( Bool getParentProxy = false ) const;
		virtual red::SharedPtr< tools::IEditableView > CreateView( const red::SharedPtr< tools::IEditableView >& parentView = nullptr, const Bool isReadOnly = false, const CName viewClass = CName::NONE(), THandle< ISerializable > userContext = nullptr ) const;

		virtual tools::EditorObjectIDPath			Selectable_CreateEditorId() const override;

		virtual tools::EditorObjectID			GetEditorObjectID() const;
		
		PhysicsEditorScene* GetEditorScene() const;

		RED_INLINE EPhysicsBaseType					GetBaseType() const
		{
			return m_baseType;
		};

		virtual String								GetName() const;

		RED_INLINE Box								GetBoundsLocal()
		{
			return m_bounds;
		}

		RED_INLINE Box								GetBoundsWorld()
		{
			return m_localToWorld.TransformBox( m_bounds );
		}

		virtual Bool IsValid()
		{
			return true;
		}

		Bool										IsFlagSet( PhysicsBaseFlag flag ) const;
		void										SetFlag( PhysicsBaseFlag flag, Bool decision );

		void										RegisterListener( PhysicsBaseWeakPtr listener );
		void										UnregisterListener( PhysicsBaseWeakPtr listener );
		virtual void								OnNotifyTransformChanged( PhysicsBaseID id )
		{};

		virtual void								OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value ) override;

		void AttachNotificationHandler( const red::SharedPtr<tools::IMeshPhysicsStructureNode>& handler );

		virtual void EvaluateSerializationDependencies( red::DynArray< THandle< ISerializable > >& dependenciesArray );
	protected:
		/// current world space transform, recalculated when original base object is moved
		Matrix										m_localToWorld;
		Box											m_bounds; // Object bounds in local space
		red::UniquePtr< PhysicsShapeRenderer > 		m_physicsRenderHelper;
		Uint32										m_flags;
		PhysicsBaseID								m_physicsBaseId;
		Uint8										m_alpha;

		red::DynArray< red::WeakPtr<tools::IMeshPhysicsStructureNode> > m_notificationHandlers;

	private:
		red::DynArray< PhysicsBaseWeakPtr >		m_transformChangeListeners;
		PhysicsEditorScene* 				m_editorScene;
		EPhysicsBaseType							m_baseType;

		friend class PhysicsEditorScene;
		friend class PhysicsBackendHelper;
	};

	// --- !PhysicsBase ---
}

RTTI_DECLARE_ENUM_IN_NAMESPACE( EPhysicsBaseType, physics );
