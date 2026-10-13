/**
* Copyright (c) 2007-2016 CD Projekt Red. All Rights Reserved.
*/
#pragma once

// Core lib stuff....
#include "../../core/include/corePublic.h"

// --------------------

class IRenderResource;
class IRenderResourceBlob;
struct MeshStats;
struct DebugVertexBase;
struct DebugVertex;
struct DebugVertexUV;
class IRenderCommandHandler;
class IRenderEngineHandler;
enum EEnvColorGroup : Uint8;
enum EEmitterGroup : Uint8;
class IRenderEntityGroup;
class IRenderSelectable;
class CRenderSelectionBuffer;
class IViewport;
class CRenderFrameInfo;
class CRenderCameraFrameInfo;
class IRenderFrame;
class IRenderScene;
class IRenderProxy;
class RenderProxyHandle;
typedef red::SharedPtr< RenderProxyHandle > RenderProxyPtr;
class CRenderProxyTypeFlag;
class IRenderThread;
struct SDynamicDecalInitInfo;
class RenderProxyInitInfo;
class IRenderSkinningData;
class IGpuDataProcessor;
class CRenderCamera;
class CEnvColorGroupsParametersAtPoint;
struct SFoliageRenderParams;
class IRender;
class IRenderObject;
enum EFadeType : Int32;
struct SScreenshotParameters;
enum ESaveFormat : Int32;
struct SSkyboxSetupParameters;
enum ETextureRawFormat : Int32;
enum ERenderMeshStreams : Int32;
struct DayPointEnvironmentParams;
struct MeshParticle;
enum ETextureCompression : Int32;
enum ERenderFragmentType : Uint32;
struct RenderEmitterDrawerData;
struct SMaterialSetupEntry;
struct SCameraRenderViewport;
enum EFeatureFlag : Uint8;
struct FeatureFlagsMask;
struct TechniqueFeatures;
struct FeatureFlagsTechniques;
struct FeatureFlagsCompiler;
class IRenderVideo;
struct IDeviceNotify;

namespace rend
{
	class SelectedObjectInfo;
	class SelectionResult;
	class DepthResult;	
	class ColorResult;
	class Color10Result;
	class HdrResult;
	class IDebugDrawer;
}

enum ERenderingMode : Int32;
enum EFramePurpose : Int32;
enum ERenderMeshStreams : Int32;
enum EFoliageVisualisationMode : Uint32;
enum EToneMappingType : Uint32;
enum EDisplayMode : Uint32;
enum EColorPrimary : Uint32;
enum EColorMappingFunction : Uint8;
enum ESSAOQualityLevel : Uint32;
enum ERenderVisibilityResult : Uint32;
enum EEnvManagerModifier : Uint32;
enum EEnvColorGroup : Uint8;
enum ERenderingPlane : Uint32;
enum EEnvAutoHideGroup : Int32;
enum EFogVolumeShape : Uint32;
enum ELightType : Uint32;
enum ELightUnit : Uint32;
enum EAreaLightShape : Uint32;
enum ERenderLightParameter : Uint32;
enum ERenderFragmentMaterialParameter : Uint32;
enum ELightShadowCastingMode : Uint32;
enum ELightShadowSoftnessMode : Uint32;
enum EDimmerType : Int32;
enum ERenderFeatureFlags : Uint32;
enum ECustomMaterialParam : Uint32;
enum ELightPass : Int32;
enum ECustomCameraTarget : Uint8;
enum EViewportWindowMode : Uint8;
enum class EAspectRatio : Uint8;

template< typename T >
class TRenderPtr;

namespace GpuWrapApi
{
	enum eTextureSaveFormat : Uint8;
	enum eTextureImportFormat : Uint8;
	enum eTextureType : Uint8;
	enum eShaderType : Uint8;
	enum eBufferChunkCategory : Uint8;
	enum eTextureFormat : Uint8;
	enum eTextureGroup : Uint8;
	enum EImageCompressionHint : Uint8;
	enum eBufferChunkType : Uint32;;
}

// --------------------

// Total (engine max) number of shadow cascades allowed
// I do not recomend to change this above 4 (some constants are packed into float4 variables)
// deeper refactor fo constant buffers is required if values larged than 4 are required.
#define MAX_CASCADES				4
#define MAX_DISTANT_SHADOWS_LEVELS	4

#define UPDATE_CONTEXT_BATCH_SIZE	64

// pczatr - uncomment this to enable dynamic decals code (WIP)
//#define NO_DYNAMIC_DECALS


// --------------------

#include "renderDataApi.h"

// --------------------

#include "../../imgui/include/imguiPublic.h"
#include "../../redImage/include/redImagePublic.h"
#include "../../resourceMaterial/include/resourceMaterialPublic.h"
#include "../../shaderCache/include/shaderCachePublic.h"

#include "renderingMemory.h"