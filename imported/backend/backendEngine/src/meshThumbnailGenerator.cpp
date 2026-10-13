#include "build.h"
#include "meshThumbnailGenerator.h"
#include "thumbnailGenerator.h"
#include "thumbnailRendering.h"

#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/renderData/include/renderProxyInitData.h"
#include "../../../common/resourceMesh/include/meshAppearance.h"
#include "../../../common/renderDraw/include/fragmentBuilder.h"
#include "../../common/renderData/include/renderer.h"
#include "../../common/renderData/include/renderCommandHandler.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshThumbnailGenerator, res );
	RTTI_PARENT_TYPE( IThumbnailGenerator );
RTTI_END_TYPE()

namespace res
{
	MeshThumbnailGenerator::MeshThumbnailGenerator()
	{
		const auto & defaultObject = ClassID< CMesh >()->GetDefaultObject< CResource >();
		const String & extension = defaultObject->GetExtension();
		const String & deprecatedExtension = defaultObject->GetDeprecatedExtension();

		m_formatsExtensions.PushBack( extension );
		if ( !deprecatedExtension.Empty() && extension != deprecatedExtension )
		{
			m_formatsExtensions.PushBack( deprecatedExtension );
		}
	}

	ThumbnailPresentation MeshThumbnailGenerator::PresentationDefaults() const
	{
		EulerAngles rot;
		rot.Yaw = -135.0f;
		rot.Pitch = -30.0f;
		rot.Roll = 0.0f;

		Vector4 right, up;
		ThumbnailPresentation presentation;
		rot.ToAngleVectors( &presentation.forward, &right, &up );
		return presentation;
	}

	void MeshThumbnailGenerator::Generate( const THandle<CResource>& resource, ThumbGenContext& ctx, red::SharedPtr<rend::ColorResult>& result )
	{
		if ( THandle< CMesh > mesh = Cast<CMesh>( resource ) )
		{
			ThumbnailRenderingManager rendering( ctx.m_size );

			SetupProxy( mesh, ctx, &rendering );
			SetupCamera( mesh, ctx, &rendering );

			red::DynArray < TRenderPtr<IRenderScene> > scenes;
			scenes.PushBack( rendering.GetScenePtr() );
			GRender->GetCommands()->FrameTick( scenes );
			rendering.Render( result, ctx.m_presetsManager );
		}
	}

	void MeshThumbnailGenerator::OnReset()
	{

	}

	void MeshThumbnailGenerator::SetupProxy( const THandle< CMesh > mesh, const ThumbGenContext& ctx, ThumbnailRenderingManager* renderingManager )
	{
		if ( !mesh->GetRenderResource() )
		{
			return;
		}

		THandle< mesh::MeshAppearance > appearance = mesh->GetAppearanceSafe( ctx.m_prefferedAppearanceName );

		RenderProxyMeshComponentInitData meshInfo;
		meshInfo.m_isCharacterShadowFallback = false;
		meshInfo.m_hasMeshResource = true;

		meshInfo.m_meshCanMergeIntoGlobalShadowMesh = false;
		meshInfo.m_renderMesh = mesh->GetRenderResource();
		meshInfo.m_materialSetup = appearance->GetRenderMaterialSetup();
		meshInfo.m_setupsNum = 1;

		// drawable component
		meshInfo.m_componentFriendlyName = "<noname>";
		meshInfo.m_forcedLODLevel = 0;
		meshInfo.m_isCastingGlobalShadows = false;
		meshInfo.m_isCastingLocalShadows = false;
		meshInfo.m_isTwoSided = false;
		meshInfo.m_initAsHidden = false;
		meshInfo.m_isCastingShadowsEvenIfNotVisible = false;

		// drawable component
		meshInfo.m_isVisible = true;
		meshInfo.m_isDynamicGeometryComponent = true;
		meshInfo.m_isForceNoAutohide = true;
		meshInfo.m_autoHideDistanceNear = 0.0f;
		meshInfo.m_autoHideDistanceFar = mesh ? mesh->GetAutoHideDistance() : 25.f;
		meshInfo.m_renderingPlane = RPl_Scene;
		meshInfo.m_objectTypeID = mesh->GetRenderObjectType();

		// base
		meshInfo.m_type = RPT_Mesh;
		meshInfo.m_localToWorld = ctx.m_transform;
		meshInfo.m_boundingBox = mesh ? mesh->GetBoundingBox() : Box( Vector4::ZERO_3D_POINT(), 1.f );

		renderingManager->SetupProxy( meshInfo );
	}

	void MeshThumbnailGenerator::SetupCamera( const THandle<CMesh> mesh, const ThumbGenContext& ctx, ThumbnailRenderingManager* renderingManager )
	{
		const Box& boundingBox = mesh->GetBoundingBox();
		if ( boundingBox.IsEmpty() )
		{
			// there's nothing to show anyway
			return;
		}

		const Box worldBox = ctx.m_transform.TransformBox( boundingBox );
		ThumbnailPresentation presentation = ctx.m_presentation;
		ThumbnailPresentation::SetupLookAtRegionCamera( worldBox, presentation, presentation.IsOrthographic() );

		renderingManager->SetupCamera( presentation );
	}
}
