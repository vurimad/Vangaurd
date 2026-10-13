/**
* Copyright (c) 2018-2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../backendEngine/include/resourceEditor.h"
#include "../../backendEngine/include/previewPanel.h"
#include "../../backendEngine/include/genericPreviewPanelSharedData.h"
#include "../../backendEngine/include/renderingWindowCamera.h"
#include "../../backendEngine/include/objectCollection.h"
#include "../../../common/resourceMaterial/include/multilayerLayerTemplate.h"
#include "../../../common/resourceMaterial/include/materialInstance.h"
#include "../../../common/resourceMaterial/include/materialTemplate.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshDataViewRO.h"
#include "../../../common/resourceMesh/src/materialSetupParamsProvider.h"
#include "../../../common/resourceMaterial/include/bitmapTexture.h"
#include "../../../common/renderData/include/renderer.h"
#include "../../../common/renderData/include/renderProxy.h"
#include "../../../common/renderData/include/renderCommandHandler.h"
#include "../../../common/redReflection/include/gatheredResource.h"


static res::TStaticResource<CMaterialTemplate> resTemplatePreviewMaterial( RED_CONST_RESOURCEPATH( "engine\\materials\\internal\\editor_mltemplate_preview.mt" ), GRF_Backend | GRF_Startup );
static res::TStaticResource<CMesh> resPreviewSphere( RED_CONST_RESOURCEPATH( "engine\\meshes\\editor\\sphere.w2mesh" ), GRF_Backend | GRF_Startup );
static res::TStaticResource<CMesh> resPreviewCube( RED_CONST_RESOURCEPATH( "engine\\meshes\\editor\\cube.mesh" ), GRF_Backend | GRF_Startup );
static res::TStaticResource<CMesh> resPreviewPlane( RED_CONST_RESOURCEPATH( "engine\\meshes\\editor\\plane1m.mesh" ), GRF_Backend| GRF_Startup );
static res::TStaticResource<CMesh> resPreviewCylinder( RED_CONST_RESOURCEPATH( "engine\\meshes\\editor\\cylinder.w2mesh" ), GRF_Backend | GRF_Startup );
static res::TStaticResource<CMesh> resPreviewRefSphere( RED_CONST_RESOURCEPATH( "engine\\meshes\\editor\\reference_sphere.w2mesh" ), GRF_Backend | GRF_Startup );
static res::TStaticResource<CMesh> resPreviewCubeOld( RED_CONST_RESOURCEPATH( "engine\\meshes\\editor\\box.w2mesh" ), GRF_Backend| GRF_Startup );


namespace tools
{
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


	static red::DynArray<String> GetOverrideNames( const red::DynArray<CName>& namesFromTemplate )
	{
		red::DynArray<String> names;
		names.Reserve( namesFromTemplate.Size() );
		for ( const CName& name : namesFromTemplate )
		{
			names.PushBack( name.AsChar() );
		}
		return names;
	}

	static String GetDefaultOverride( CName defaultName, const red::DynArray<CName>& namesFromTemplate )
	{
		String result;
		if ( namesFromTemplate.Exist( defaultName ) )
		{
			// Use the provided default if possible.
			result = defaultName.AsChar();
		}
		else if ( !namesFromTemplate.Empty() )
		{
			// Couldn't find the default, so just use the first.
			result = namesFromTemplate[0].AsChar();
		}
		return result;
	}


	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class Multilayer_LayerTemplatePreviewPanel : public PreviewPanel
	{
		DECLARE_INTEROP_DERIVED( Multilayer_LayerTemplatePreviewPanel, PreviewPanel );

	public:
		Multilayer_LayerTemplatePreviewPanel( RenderingWindowPtr renderingWindow, const PreviewPanelSharedDataPtr& sharedState );
		virtual ~Multilayer_LayerTemplatePreviewPanel();

		void SetupRendering( const THandle< CMesh >& mesh, const THandle< Multilayer_LayerTemplate >& layerTemplate, const Multilayer_LayerOverride* layerOverride );
		void ReleaseRendering();
		TRenderPtr<IRenderProxy> CreateRenderProxy( const TRenderPtr<IRenderScene>& scene, const THandle< CMesh >& mesh, const THandle< IMaterial >& material );

	private:
		TRenderPtr<IRenderProxy> m_renderProxy;
	};
	typedef red::SharedPtr< Multilayer_LayerTemplatePreviewPanel > Multilayer_LayerTemplatePreviewPanelPtr;


	// Current settings
	struct CurrentParameters
	{
		RTTI_DECLARE_TYPE( CurrentParameters );

		Color m_colorScale;

		Float m_normalScale;

		Float m_roughScaleIn;
		Float m_roughBiasIn;
		Float m_metalScaleIn;
		Float m_metalBiasIn;

		Float m_roughScaleOut;
		Float m_roughBiasOut;
		Float m_metalScaleOut;
		Float m_metalBiasOut;

		Float m_colorMaskScaleIn;
		Float m_colorMaskBiasIn;
		Float m_colorMaskScaleOut;
		Float m_colorMaskBiasOut;

		TResRef<CBitmapTexture> m_diffuseTexture;
		TResRef<CBitmapTexture> m_normalTexture;
		TResRef<CBitmapTexture> m_roughnessTexture;
		TResRef<CBitmapTexture> m_metalnessTexture;
	};

	class Multilayer_LayerTemplateController : public ISerializable
	{
		RTTI_DECLARE_TYPE( Multilayer_LayerTemplateController );

	public:

		void SetResource( const THandle<Multilayer_LayerTemplate>& resource );
		const THandle<Multilayer_LayerTemplate>& GetResource() const;

		void SetPreviewPanel( const Multilayer_LayerTemplatePreviewPanelPtr& previewPanel );
		void SetMesh( const THandle<CMesh>& mesh );

		virtual void OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& newValue ) override;

	private:
		void UpdateOverride();
		void RefreshPreview();

	private:
		// Overrides
		String m_colorScale;
		String m_roughLevelsIn;
		String m_roughLevelsOut;
		String m_metalLevelsIn;
		String m_metalLevelsOut;
		String m_normalStrength;

		CurrentParameters m_currentParameters;

		THandle<CMesh> m_previewMesh;

		THandle<Multilayer_LayerTemplate> m_layerTemplate;
		Multilayer_LayerOverride m_currentOverride;

		Multilayer_LayerTemplatePreviewPanelPtr m_previewPanel;

		String m_value;
	};



	class Multilayer_LayerTemplateResourceEditor : public IResourceEditor
	{
		RTTI_DECLARE_TYPE( Multilayer_LayerTemplateResourceEditor );
		DECLARE_INTEROP_DERIVED( Multilayer_LayerTemplateResourceEditor, IResourceEditor );

	public:
		Multilayer_LayerTemplateResourceEditor();
		virtual ~Multilayer_LayerTemplateResourceEditor();

		/// IResourceEditor interface
		virtual Bool Initialize( const THandle< CResource >& resource ) override;
		virtual const Bool SupportsResourceClass( const rtti::ClassType* resourceClass ) const override;
		virtual const THandle< CResource > GetResource() const override;
		virtual void OnPreviewPanelRequest( const String& type, RenderingWindowPtr renderingWindowPtr, PreviewPanelPtr& outPanel ) override;

	private:
		void AddGeometry( const AnsiChar* name, const THandle<CMesh>& mesh );
		void SelectGeometry( const String& geometryName );

	private:
		red::SharedPtr< GenericPreviewPanelSharedData > m_sharedData;
		Multilayer_LayerTemplatePreviewPanelPtr m_previewPanel;

		red::HashMap<String, THandle<CMesh>> m_geometryTypes;
		String m_currentGeometryType;

		THandle<Multilayer_LayerTemplate> m_layerTemplate;
		THandle< Multilayer_LayerTemplateController > m_controller;

		red::UniquePtr< ObjectCollection > m_controllerView;
	};

	class MultilayerOverride_CustomProperty : public IEditableCustomProperty
	{
		RTTI_DECLARE_TYPE( MultilayerOverride_CustomProperty );

	public:
		virtual const Bool HandlesCustomType( const CName customTypeName, const prv::EditableCollectionRootItem* root ) const override
		{
			const String typeNameString( customTypeName.AsChar() );
			return typeNameString.BeginsWith( "multilayerOverride" );
		}

		virtual void Initialize( const CName customPropertyType, EditableCollection* collection, prv::EditableCollectionRootItem* root, const rtti::AccessPath& pathSoFar, const rtti::IType* type, const Bool isInlined, const Bool isReadOnly ) override
		{
			IEditableCustomProperty::Initialize( customPropertyType, collection, root, pathSoFar, type, isInlined, isReadOnly );

			if ( THandle< Multilayer_LayerTemplateController > controller = FindParentObject< Multilayer_LayerTemplateController >() )
			{
				const String typeNameString( customPropertyType.AsChar() );
				const String subtype = typeNameString.StringAfter( ":" );

				if ( subtype.EqualsNC( "colorScaleNames" ) )
				{
					m_options = GetOverrideNames( controller->GetResource()->GetOverrideColorScaleNames() );
				}
				else if ( subtype.EqualsNC( "roughLevelsInNames" ) )
				{
					m_options = GetOverrideNames( controller->GetResource()->GetOverrideRoughLevelsInNames() );
				}
				else if ( subtype.EqualsNC( "roughLevelsOutNames" ) )
				{
					m_options = GetOverrideNames( controller->GetResource()->GetOverrideRoughLevelsOutNames() );
				}
				else if ( subtype.EqualsNC( "metalLevelsInNames" ) )
				{
					m_options = GetOverrideNames( controller->GetResource()->GetOverrideMetalLevelsInNames() );
				}
				else if ( subtype.EqualsNC( "metalLevelsOutNames" ) )
				{
					m_options = GetOverrideNames( controller->GetResource()->GetOverrideMetalLevelsOutNames() );
				}
				else if ( subtype.EqualsNC( "normalStrengthNames" ) )
				{
					m_options = GetOverrideNames( controller->GetResource()->GetOverrideNormalStrengthNames() );
				}
			}
		}

		virtual Bool HasOptions() const override
		{
			return !m_options.Empty();
		}

		virtual const red::DynArray<String>& GetOptions() const override
		{
			return m_options;
		}

		virtual CName GetControlType() const override
		{
			return RED_NAME( String::Printf( "%hs:Restricted", ::GetTypeName<String>().AsChar() ) );
		}

	private:
		red::DynArray< String > m_options;
	};

} // namespace tools

RTTI_BEGIN_TYPE_IN_NAMESPACE( MultilayerOverride_CustomProperty, tools );
	RTTI_PARENT_TYPE( tools::IEditableCustomProperty );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( CurrentParameters, tools );
	RTTI_PROPERTY( m_diffuseTexture ).editable();
	RTTI_PROPERTY( m_normalTexture ).editable();
	RTTI_PROPERTY( m_roughnessTexture ).editable();
	RTTI_PROPERTY( m_metalnessTexture ).editable();
	
	RTTI_PROPERTY( m_colorScale ).editable();
	RTTI_PROPERTY( m_normalScale ).editable();
	
	RTTI_PROPERTY( m_roughScaleIn ).editable();
	RTTI_PROPERTY( m_roughBiasIn ).editable();
	RTTI_PROPERTY( m_metalScaleIn ).editable();
	RTTI_PROPERTY( m_metalBiasIn ).editable();
	RTTI_PROPERTY( m_roughScaleOut ).editable();
	RTTI_PROPERTY( m_roughBiasOut ).editable();
	RTTI_PROPERTY( m_metalScaleOut ).editable();
	RTTI_PROPERTY( m_metalBiasOut ).editable();
	
	RTTI_PROPERTY( m_colorMaskScaleIn ).editable();
	RTTI_PROPERTY( m_colorMaskBiasIn ).editable();
	RTTI_PROPERTY( m_colorMaskScaleOut ).editable();
	RTTI_PROPERTY( m_colorMaskBiasOut ).editable();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( Multilayer_LayerTemplateController, tools );
	RTTI_PARENT_TYPE( ISerializable );
	RTTI_PROPERTY( m_currentParameters ).readOnly();
#ifndef RED_CONFIGURATION_FINAL
	RTTI_PROPERTY_CATEGORY( "AvailableOverrides" );
	RTTI_PROPERTY( m_colorScale ).editable().customEditor( "multilayerOverride:colorScaleNames" );
	RTTI_PROPERTY( m_roughLevelsIn ).editable().customEditor( "multilayerOverride:roughLevelsInNames" );
	RTTI_PROPERTY( m_roughLevelsOut ).editable().customEditor( "multilayerOverride:roughLevelsOutNames" );
	RTTI_PROPERTY( m_metalLevelsIn ).editable().customEditor( "multilayerOverride:metalLevelsInNames" );
	RTTI_PROPERTY( m_metalLevelsOut ).editable().customEditor( "multilayerOverride:metalLevelsOutNames" );
	RTTI_PROPERTY( m_normalStrength ).editable().customEditor( "multilayerOverride:normalStrengthNames" );
#endif
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( Multilayer_LayerTemplateResourceEditor, tools );
	RTTI_PARENT_TYPE( tools::IResourceEditor );
RTTI_END_TYPE();



namespace tools
{


	void Multilayer_LayerTemplateController::SetResource( const THandle<Multilayer_LayerTemplate>& resource )
	{
		m_layerTemplate = resource;
#ifndef RED_CONFIGURATION_FINAL
		m_colorScale = GetDefaultOverride( resource->GetDefaultOverrides().colorScale, resource->GetOverrideColorScaleNames() );
		m_roughLevelsIn = GetDefaultOverride( resource->GetDefaultOverrides().roughLevelsIn, resource->GetOverrideRoughLevelsInNames() );
		m_roughLevelsOut = GetDefaultOverride( resource->GetDefaultOverrides().roughLevelsOut, resource->GetOverrideRoughLevelsOutNames() );
		m_metalLevelsIn = GetDefaultOverride( resource->GetDefaultOverrides().metalLevelsIn, resource->GetOverrideMetalLevelsInNames() );
		m_metalLevelsOut = GetDefaultOverride( resource->GetDefaultOverrides().metalLevelsOut, resource->GetOverrideMetalLevelsOutNames() );
		m_normalStrength = GetDefaultOverride( resource->GetDefaultOverrides().normalStrength, resource->GetOverrideNormalStrengthNames() );
#endif // RED_CONFIGURATION_FINAL
		UpdateOverride();
	}

	const THandle<Multilayer_LayerTemplate>& Multilayer_LayerTemplateController::GetResource() const
	{
		return m_layerTemplate;
	}

	void Multilayer_LayerTemplateController::SetPreviewPanel( const Multilayer_LayerTemplatePreviewPanelPtr& previewPanel )
	{
		m_previewPanel = previewPanel;
		RefreshPreview();
	}

	void Multilayer_LayerTemplateController::SetMesh( const THandle<CMesh>& mesh )
	{
		m_previewMesh = mesh;
		RefreshPreview();
	}

	void Multilayer_LayerTemplateController::OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& newValue )
	{
		ISerializable::OnPropertyPostChange( path, oldValue, newValue );

		UpdateOverride();
	}

	void Multilayer_LayerTemplateController::UpdateOverride()
	{
		Multilayer_LayerOverrideSelection overrideSelection;
		overrideSelection.colorScale = RED_NAME(m_colorScale);
		overrideSelection.normalStrength = RED_NAME(m_normalStrength);
		overrideSelection.roughLevelsIn = RED_NAME(m_roughLevelsIn);
		overrideSelection.roughLevelsOut = RED_NAME(m_roughLevelsOut);
		overrideSelection.metalLevelsIn = RED_NAME(m_metalLevelsIn);
		overrideSelection.metalLevelsOut = RED_NAME(m_metalLevelsOut);

		m_currentOverride = m_layerTemplate->GetOverrideParameters( overrideSelection, res::ResourcePath() );

		m_currentParameters.m_colorScale.R = Uint8(m_currentOverride.colorScale[0]*255);
		m_currentParameters.m_colorScale.G = Uint8(m_currentOverride.colorScale[1]*255);
		m_currentParameters.m_colorScale.B = Uint8(m_currentOverride.colorScale[2]*255);
		m_currentParameters.m_colorScale.A = 255;

		m_currentParameters.m_normalScale = m_currentOverride.normalScale;

		m_currentParameters.m_roughScaleIn = m_currentOverride.roughScaleIn;
		m_currentParameters.m_roughBiasIn = m_currentOverride.roughBiasIn;
		m_currentParameters.m_metalScaleIn = m_currentOverride.metalScaleIn;
		m_currentParameters.m_metalBiasIn = m_currentOverride.metalBiasIn;

		m_currentParameters.m_roughScaleOut = m_currentOverride.roughScaleOut;
		m_currentParameters.m_roughBiasOut = m_currentOverride.roughBiasOut;
		m_currentParameters.m_metalScaleOut = m_currentOverride.metalScaleOut;
		m_currentParameters.m_metalBiasOut = m_currentOverride.metalBiasOut;

		m_currentParameters.m_colorMaskScaleIn = m_currentOverride.colorMaskScaleIn;
		m_currentParameters.m_colorMaskBiasIn = m_currentOverride.colorMaskBiasIn;
		m_currentParameters.m_colorMaskScaleOut = m_currentOverride.colorMaskScaleOut;
		m_currentParameters.m_colorMaskBiasOut = m_currentOverride.colorMaskBiasOut;

		m_currentParameters.m_diffuseTexture = m_layerTemplate->GetColorTextureRef();
		m_currentParameters.m_normalTexture = m_layerTemplate->GetNormalTextureRef();
		m_currentParameters.m_roughnessTexture = m_layerTemplate->GetRoughnessTextureRef();
		m_currentParameters.m_metalnessTexture = m_layerTemplate->GetMetalnessTextureRef();

		RefreshPreview();
	}

	void Multilayer_LayerTemplateController::RefreshPreview()
	{
		if ( m_previewPanel )
		{
			m_previewPanel->SetupRendering( m_previewMesh, m_layerTemplate, &m_currentOverride );
		}
	}


	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


	Multilayer_LayerTemplatePreviewPanel::Multilayer_LayerTemplatePreviewPanel( RenderingWindowPtr renderingWindow, const PreviewPanelSharedDataPtr& sharedState )
		: PreviewPanel( renderingWindow, sharedState )
		, m_renderProxy( nullptr )
	{
	}

	Multilayer_LayerTemplatePreviewPanel::~Multilayer_LayerTemplatePreviewPanel()
	{
		ReleaseRendering();
	}

	void Multilayer_LayerTemplatePreviewPanel::SetupRendering( const THandle< CMesh >& mesh, const THandle< Multilayer_LayerTemplate >& layerTemplate, const Multilayer_LayerOverride* layerOverride )
	{
		ReleaseRendering();

		if ( layerOverride != nullptr )
		{
			THandle<CMaterialInstance> materialInstance = CreateHandle<CMaterialInstance>();
			materialInstance->SetBaseMaterial( resTemplatePreviewMaterial.Get() );

			materialInstance->WriteParameter(RED_NAME_CONSTEXPR("Tiling"), layerTemplate->GetTilingMultiplier(), false);

			Vector4 colorScale;
			colorScale.X = layerOverride->colorScale[0]*2.0f;
			colorScale.Y = layerOverride->colorScale[1]*2.0f;
			colorScale.Z = layerOverride->colorScale[2]*2.0f;
			colorScale.W = 1.0f;
			materialInstance->WriteParameter(RED_NAME_CONSTEXPR("ColorScale"), colorScale, false);

			materialInstance->WriteParameter(RED_NAME_CONSTEXPR("NormalScale"), layerOverride->normalScale, false);

			materialInstance->WriteParameter(RED_NAME_CONSTEXPR("MetalnessScaleIn"), layerOverride->metalScaleIn, false);
			materialInstance->WriteParameter(RED_NAME_CONSTEXPR("MetalnessBiasIn"), layerOverride->metalBiasIn, false);
			materialInstance->WriteParameter(RED_NAME_CONSTEXPR("RoughnessScaleIn"), layerOverride->roughScaleIn, false);
			materialInstance->WriteParameter(RED_NAME_CONSTEXPR("RoughnessBiasIn"), layerOverride->roughBiasIn, false);

			materialInstance->WriteParameter(RED_NAME_CONSTEXPR("MetalnessScaleOut"), layerOverride->metalScaleOut, false);
			materialInstance->WriteParameter(RED_NAME_CONSTEXPR("MetalnessBiasOut"), layerOverride->metalBiasOut, false);
			materialInstance->WriteParameter(RED_NAME_CONSTEXPR("RoughnessScaleOut"), layerOverride->roughScaleOut, false);
			materialInstance->WriteParameter(RED_NAME_CONSTEXPR("RoughnessBiasOut"), layerOverride->roughBiasOut, false);

			materialInstance->WriteParameter(RED_NAME_CONSTEXPR("ColorMaskScaleIn"), layerOverride->colorMaskScaleIn, false);
			materialInstance->WriteParameter(RED_NAME_CONSTEXPR("ColorMaskBiasIn"), layerOverride->colorMaskBiasIn, false);
			materialInstance->WriteParameter(RED_NAME_CONSTEXPR("ColorMaskScaleOut"), layerOverride->colorMaskScaleOut, false);
			materialInstance->WriteParameter(RED_NAME_CONSTEXPR("ColorMaskBiasOut"), layerOverride->colorMaskBiasOut, false);

			materialInstance->WriteParameter(RED_NAME_CONSTEXPR("DiffuseTexture"), TResRef<ITexture>(layerTemplate->GetColorTexture()), false);
			materialInstance->WriteParameter(RED_NAME_CONSTEXPR("NormalTexture"), TResRef<ITexture>(layerTemplate->GetNormalTexture()), false);
			materialInstance->WriteParameter(RED_NAME_CONSTEXPR("RoughnessTexture"), TResRef<ITexture>(layerTemplate->GetRoughnessTexture()), false);
			materialInstance->WriteParameter(RED_NAME_CONSTEXPR("MetalnessTexture"), TResRef<ITexture>(layerTemplate->GetMetalnessTexture()), false);

			materialInstance->InvalidateRenderResource();

			m_renderProxy = CreateRenderProxy( GetSharedData()->GetRenderScene(), mesh, materialInstance );
		}

		auto camera = GetViewport()->GetCamera();
		GetViewport()->SetupOrbitCamera( mesh->GetBoundingBox().CalcCenter(), camera->GetRotation(), camera->GetOrbitDistance() );
	}

	void Multilayer_LayerTemplatePreviewPanel::ReleaseRendering()
	{
		if ( m_renderProxy )
		{
			GRender->GetCommands()->RemoveProxyFromScene( GetSharedData()->GetRenderScene(), m_renderProxy );
			m_renderProxy = nullptr;
		}
	}

	TRenderPtr<IRenderProxy> Multilayer_LayerTemplatePreviewPanel::CreateRenderProxy( const TRenderPtr<IRenderScene>& scene, const THandle< CMesh >& mesh, const THandle< IMaterial >& material )
	{
		RenderProxyMeshComponentInitData meshInfo;
		meshInfo.m_isCharacterShadowFallback = false;
		meshInfo.m_hasMeshResource = nullptr != mesh.Get();
		if( meshInfo.m_hasMeshResource )
		{
			meshInfo.m_meshCanMergeIntoGlobalShadowMesh = false;
			meshInfo.m_renderMesh = mesh->GetRenderResource();
			mesh::DataViewRO dataView( *mesh );
			CMaterialSetupParamsProvider materialSetupParamsProvider( dataView, material, true );
			meshInfo.m_materialSetup = GRender->UploadMaterialSetup( materialSetupParamsProvider.GetMaterialSetupEntries(), materialSetupParamsProvider.GetChunkMaterialNames() );
			meshInfo.m_setupsNum = 1;
		}
		else
		{
			meshInfo.m_meshCanMergeIntoGlobalShadowMesh = false;
			meshInfo.m_renderMesh = nullptr;
			meshInfo.m_setupsNum = 0;
		}

		meshInfo.m_componentFriendlyName = "<Mesh>";
		meshInfo.m_forcedLODLevel = 0;
		meshInfo.m_isCastingGlobalShadows = mesh ? mesh->GetCastGlobalShadows() : true;
		meshInfo.m_isCastingLocalShadows = mesh ? mesh->GetCastLocalShadows() : false;
		meshInfo.m_isTwoSided = false;
		meshInfo.m_isCastingShadowsEvenIfNotVisible = false;

		meshInfo.m_isVisible = true;
		meshInfo.m_isDynamicGeometryComponent = true;
		meshInfo.m_isForceNoAutohide = false;
		meshInfo.m_autoHideDistanceNear = 0.0f;
		meshInfo.m_autoHideDistanceFar = mesh ? mesh->GetAutoHideDistance() : 25.f;
		meshInfo.m_renderingPlane = RPl_Scene;
		meshInfo.m_objectTypeID = mesh ? mesh->GetRenderObjectType() : 0;

		meshInfo.m_type = RPT_Mesh;
		meshInfo.m_boundingBox = mesh ? mesh->GetBoundingBox() : Box( Vector4::ZERO_3D_POINT(), 1.f );

		RenderProxyInitInfo initInfo;
		initInfo.m_componentData = &meshInfo;

		auto proxy = GRender->CreateProxy( initInfo );

		if( proxy != nullptr )
		{
			GRender->GetCommands()->AddProxyToScene( scene, proxy );
		}
			
		return proxy;
	}

	void Multilayer_LayerTemplatePreviewPanel::SetupInterop( Multilayer_LayerTemplatePreviewPanel* panel, ::interop::utils::Object& op )
	{
	}


	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


	Multilayer_LayerTemplateResourceEditor::Multilayer_LayerTemplateResourceEditor()
		: m_controller( CreateHandle< Multilayer_LayerTemplateController >() )
		, m_controllerView( red::CreateUniquePtr< ObjectCollection >( GetAccessHistory() ) )
	{
	}

	Multilayer_LayerTemplateResourceEditor::~Multilayer_LayerTemplateResourceEditor()
	{
	}


	Bool Multilayer_LayerTemplateResourceEditor::Initialize( const THandle< CResource >& resource )
	{
		m_sharedData = red::CreateSharedPtr< GenericPreviewPanelSharedData >( *this );

		if ( !resource->IsA<Multilayer_LayerTemplate>() )
		{
			return false;
		}

		m_layerTemplate = Cast<Multilayer_LayerTemplate>(resource);

		m_controller->SetResource( m_layerTemplate );
		m_controllerView->SetObject( m_controller.Get() );

		AddGeometry( "sphere", resPreviewSphere.Get() );
		AddGeometry( "cube", resPreviewCube.Get() );
		AddGeometry( "plane", resPreviewPlane.Get());
		AddGeometry( "cylinder", resPreviewCylinder.Get() );
		AddGeometry( "ref sphere", resPreviewRefSphere.Get() );
		AddGeometry( "cube old", resPreviewCubeOld.Get());
		SelectGeometry( "sphere" );

		return true;
	}


	const Bool Multilayer_LayerTemplateResourceEditor::SupportsResourceClass( const rtti::ClassType* resourceClass ) const
	{
		return resourceClass->IsA( ClassID< Multilayer_LayerTemplate >() );
	}

	const THandle< CResource > Multilayer_LayerTemplateResourceEditor::GetResource() const
	{
		return m_layerTemplate;
	}


	void Multilayer_LayerTemplateResourceEditor::SetupInterop( Multilayer_LayerTemplateResourceEditor* editor, ::interop::utils::Object& op )
	{
		red::DynArray<String> geometryNames = editor->m_geometryTypes.GetKeys();
		op.RegisterRestrictedValue< String >( "geometry", geometryNames,
			[editor]( interop::IContext& ctx, String& outValue ) { outValue = editor->m_currentGeometryType; },
			[editor]( interop::IContext& ctx, const String& newValue ) { editor->SelectGeometry( newValue ); } );

		op.RegisterObject( "controller", editor->m_controllerView->GetRootInterop() );
	}

	void Multilayer_LayerTemplateResourceEditor::OnPreviewPanelRequest( const String& type, RenderingWindowPtr renderingWindowPtr, PreviewPanelPtr& outPanel )
	{
		m_previewPanel = red::CreateSharedPtr< Multilayer_LayerTemplatePreviewPanel >( renderingWindowPtr, m_sharedData );
		m_controller->SetPreviewPanel( m_previewPanel );
		outPanel = m_previewPanel;
	}


	void Multilayer_LayerTemplateResourceEditor::AddGeometry( const AnsiChar* name, const THandle<CMesh>& mesh )
	{
		if ( mesh )
		{
			m_geometryTypes.Insert( name, mesh );
		}
	}

	void Multilayer_LayerTemplateResourceEditor::SelectGeometry( const String& geometryName )
	{
		if ( m_currentGeometryType != geometryName )
		{
			m_currentGeometryType = geometryName;
			THandle<CMesh> mesh;
			m_geometryTypes.Find( m_currentGeometryType, mesh );

			m_controller->SetMesh( mesh );
		}
	}

} // namespace tools
