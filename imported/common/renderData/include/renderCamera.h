/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../redReflection/include/mathCommon.h"
#include "../../../common/renderData/include/renderWeaponPlaneParams.h"

//for some weird reason linker was failing for Debug Orbis when this constant was inside CRenderCamera below
static constexpr Uint32		TEMPORAL_AA_NUM_SAMPLES = 16;

/// Camera vectors
enum ECameraVector
{
	CV_Right = 0,
	CV_Forward = 1,
	CV_Up = 2,
};

/// Camera last frame info
struct RENDER_DATA_API SRenderCameraLastFrameData
{
	static const SRenderCameraLastFrameData INVALID;

public:
	Bool				m_isValid;
	Float				m_fov;
	WorldPosition		m_position;
	Quaternion			m_rotation;
	Matrix				m_worldToView;
	Matrix				m_viewToScreenRevProjAware;
	Matrix				m_viewToScreenRevProjAware_NoJitter;
	Matrix				m_screenToWorldRevProjAware;
	Float				m_subpixelOffsetX;
	Float				m_subpixelOffsetY;
	Uint32				m_subpixelOffsetWidth;
	Uint32				m_subpixelOffsetHeight;	
	Float				m_accumulatedVelocity;
public:
	SRenderCameraLastFrameData();
	
	SRenderCameraLastFrameData& Set( Bool isValid, const WorldPosition &position, const Quaternion &rotation, const Matrix &worldToView, const Matrix &viewToScreenRevProjAware, const Matrix &viewToScreenRevProjAwareNoJitter, const Matrix &screenToWorldRevProjAware, Float fov, Float subpixelOffsetX, Float subpixelOffsetY, Uint32 subpixelOffsetWidth, Uint32 subpixelOffsetHeight );
	SRenderCameraLastFrameData& Init( const WorldPosition &position, const Quaternion &rotation, Float fov, Float aspect, Float nearPlane, Float farPlane, Float zoom, Vector2 shift, const Vector4& clippingPlane, Float subpixelOffsetX, Float subpixelOffsetY, Uint32 subpixelOffsetWidth, Uint32 subpixelOffsetHeight, const Matrix &screenToWorldRevProjAware );
	SRenderCameraLastFrameData& Init( const CRenderCamera &camera );
	SRenderCameraLastFrameData& Invalidate();

	Bool						Validate() const;

public:
	WorldPosition GetPosition() const;
	Vector4 GetCameraUp() const;
	Vector4 GetCameraForward() const;
	Vector4 GetCameraRight() const;	
};

// Render camera viewport
struct RENDER_DATA_API SCameraRenderViewport
{
	// - - - - - - - - - - - - - - - - - - -
	RED_FORCE_INLINE static const SCameraRenderViewport Fullscreen( Uint32 sortPriority = 0 ) { return SCameraRenderViewport ( sortPriority, Rect(0,1,0,1), Rect(0,1,0,1) ); }
	// - - - - - - - - - - - - - - - - - - -

	SCameraRenderViewport ();
	SCameraRenderViewport ( Uint32 sortPriority, const Rect &renderArea, const Rect &referenceArea );

	void Reset();
	Bool IsOnscreenViewport() const;
	Rect GetRenderViewport( Int32 fullWidth, Int32 fullHeight ) const;
	Rect GetRenderViewport( const Rect &fullViewport ) const;
	Rect GetOnscreenViewport( Int32 fullWidth, Int32 fullHeight ) const;
	Rect GetOnscreenViewport( const Rect &fullViewport ) const;

	Uint32	m_sortPriority;		//< higher priority will be rendered on top of lower priorities
	Rect	m_renderArea;
	Rect	m_referenceArea;
};

/// Parameters for depth buffer encoding modification
/* Note that this doesn't take clipping into account, but seems perfect for simple renderingPlanes assiming some restrictions */
struct RENDER_DATA_API SDepthBufferEncodingModParams
{
	SDepthBufferEncodingModParams ()
		: m_depthScale ( 1.f )
		, m_depthBias ( 0.f )
	{
		RED_ASSERT( IsNeutral() );
	}

	RED_INLINE Bool IsNeutral() const
	{
		return 1 == m_depthScale && 0 == m_depthBias;
	}

	RED_INLINE Bool operator==( const SDepthBufferEncodingModParams &other ) const
	{
		return m_depthScale == other.m_depthScale && m_depthBias == other.m_depthBias;
	}

	RED_INLINE Bool operator!=( const SDepthBufferEncodingModParams &other ) const
	{
		return !operator==( other );
	}
		
	Float			m_depthScale;
	Float			m_depthBias;
};

/// Rendering plane definition
struct RENDER_DATA_API SRenderingPlaneDef
{
	RED_USE_MEMORY_POOL( rend::PoolCameras );

	SRenderingPlaneDef ();
	SRenderingPlaneDef( const SRenderingPlaneDef& renderingPlane );
	SRenderingPlaneDef( const CRenderCamera& camera) ;

	SRenderingPlaneDef& InitFromCamera( const CRenderCamera &camera );
	Bool IsEquivalent( const CRenderCamera &camera ) const;
	void Apply( CRenderCamera &camera ) const;

	Float							m_fov;			//!< FOV ( in degrees )
	Float							m_zoom;			//!< Linear zoom
	Vector2							m_shift;		//!< Projection shift
	SDepthBufferEncodingModParams	m_depthMod;		//!< Depth modification
	Float							m_nearPlane;	//!< Near clipping plane		
	Float							m_farPlane;		//!< Far clipping plane	

	SWeaponPlaneParams				m_weaponPlaneParams; //weapon plane blur parameters
};

/// Low level camera
class RENDER_DATA_API CRenderCamera
{
	RED_USE_MEMORY_POOL( rend::PoolCameras );

public:
	static const Matrix s_axesConversion;

private:
	WorldPosition	m_position;						//!< Camera position
	Quaternion		m_rotation;						//!< Rotation angles
	Float			m_fov;							//!< FOV ( in degrees )
	Float			m_fovMultiplier;				//!< FOV multiplier for the LOD calculation
	Float			m_aspect;						//!< Aspect ratio
	Float			m_zoom;							//!< Linear zoom
	Vector2			m_shift;						//!< Projection shift
	SDepthBufferEncodingModParams	m_depthMod;		//< Depth encoding modification params
	Float			m_nearPlane;					//!< Near clipping plane
	Float			m_farPlane;						//!< Far clipping plane
	Matrix			m_worldToView;					//!< View matrix
	Matrix			m_worldToCamera;				//!< World to camera matrix
	Matrix			m_cameraToWorld;				//!< Camera to world matrix
	Matrix			m_viewToWorld;					//!< Inverse of view matrix
	Matrix			m_screenToView;					//!< Inverse of projection matrix
	Matrix			m_viewToScreen;					//!< Projection matrix
	Matrix			m_worldToScreen;				//!< ViewProjection matrix
	Matrix			m_screenToWorld;				//!< Inverse of view projection matrix
	Vector4			m_cameraVectors[3];				//!< Camera vectors
	Vector4			m_cascadeRefPoint;				//!< Cascade reference point (in case of mirrors it's different than camera position)
	Vector4			m_clippingPlane;				//!< Oblique clipping plane
	Vector2			m_projectionOffset;				//!< Projection offset
	Matrix			m_worldToScreenRevProjAware;	//!< ViewProjection matrix, reversed projection aware
	Matrix			m_viewToScreenRevProjAware;		//!< Projection matrix, reversed projection aware
	Matrix			m_screenToWorldRevProjAware;	//!< Inverse of view projection matrix, reversed projection aware

	Float 			m_subPixelOffsetX;				//!<Subpixel offset X
	Float			m_subPixelOffsetY;				//!<Subpixel offset Y
	Uint32			m_subPixelOffsetWidth;			//!<Subpixel offset width
	Uint32			m_subPixelOffsetHeight;			//!<Subpixel offset height

	Uint32			m_jitterIndex;

	Bool			m_nonDefaultNearPlane	: 1;	//!< Non default near plane
	Bool			m_nonDefaultFarPlane	: 1;	//!< Non default far plane
	Bool			m_isReversedProjection	: 1;	//!< Is this camera using reversed projection (rendering only)
	Bool			m_isCameraCut : 1;				//!< Camera changed, invalidate any temporal effects

	red::SharedPtr< SRenderingPlaneDef > m_weaponPlane;
public:

	static constexpr Float		MIN_NEAR_PLANE = 0.001f;	// 1mm
	static constexpr Float		MAX_FAR_PLANE = 16000.0f;	// 16km 

	struct RENDER_DATA_API CreationContext
	{
		Quaternion		rotation;					//!< Rotation angles
		WorldPosition	position;					//!< Camera position
		Vector4			clippingPlane;				//!< Oblique clipping plane
		Vector2			shift;						//!< Projection shift
		Float			fov;						//!< FOV ( in degrees )
		Float			aspect;						//!< Aspect ratio
		Float			nearPlane;					//!< Near clipping plane
		Float			farPlane;					//!< Far clipping plane
		Float			zoom;						//!< Linear zoom
		Bool			isReversedProjection : 1;	//!< Is this camera using reversed projection (rendering only)
		Bool			isCameraCut : 1;			//!< Camera changed, invalidate any temporal effects
		red::SharedPtr< SRenderingPlaneDef > weaponPlane;	//!< Weapon plane definition

		CreationContext();
	};

	CRenderCamera( const CreationContext& context = CreationContext() );
	CRenderCamera( const CreationContext& context, const Vector4& cascadeRefPoint );
	CRenderCamera( const CRenderCamera& camera );

	// Get attributes
	RED_INLINE Float GetFOV() const { return m_fov; }
	RED_INLINE Float GetFOVMultiplier() const { return m_fovMultiplier; }
	RED_INLINE Float GetAspect() const { return m_aspect; }
	RED_INLINE Float GetNearPlane() const { return m_nearPlane; }
	RED_INLINE Float GetFarPlane() const { return m_farPlane; }
	RED_INLINE void SetNearPlane( Float val ) { RED_FATAL_ASSERT( m_fov == 0.0f || val >= MIN_NEAR_PLANE, "Insane near plane" ); m_nearPlane = val; }
	RED_INLINE void SetFarPlane( Float val ) { RED_FATAL_ASSERT( m_fov == 0.0f || val <= MAX_FAR_PLANE, "Insane far plane" );  m_farPlane = val; }
	RED_INLINE void SetZoom( Float val ) { m_zoom = val; }; // Please note that this does not cause matrices to re-calculate. 
	RED_INLINE Float GetZoom() const { return m_zoom; }
	RED_INLINE const Vector2& GetShift() const { return m_shift; }
	RED_INLINE const Vector4& GetClippingPlane() const { return m_clippingPlane; }
	RED_INLINE const Vector2& GetProjectionOffset() const { return m_projectionOffset; }
	RED_INLINE const WorldPosition& GetPosition() const { return m_position; }
	RED_INLINE const Vector4& GetCascadeReferencePoint() const { return m_cascadeRefPoint; }
	RED_INLINE const Quaternion& GetRotation() const { return m_rotation; }
	RED_INLINE const Matrix& GetWorldToView() const { return m_worldToView; }
	RED_INLINE const Matrix& GetWorldToCamera() const { return m_worldToCamera; }
	RED_INLINE const Matrix& GetCameraToWorld() const { return m_cameraToWorld; }
	RED_INLINE const Matrix& GetViewToWorld() const { return m_viewToWorld; }
	RED_INLINE const Matrix& GetScreenToView() const { return m_screenToView; }
	RED_INLINE const Matrix& GetViewToScreen() const { return m_viewToScreen; }
	RED_INLINE const Matrix& GetWorldToScreen() const { return m_worldToScreen; }
	RED_INLINE const Matrix& GetScreenToWorld() const { return m_screenToWorld; }
			   const Matrix  GetScreenToWorld_NoTranslate( Bool useJitter = true ) const;
	RED_INLINE const Vector4& GetCameraVector( Int32 index ) const { return m_cameraVectors[ index ]; }
	RED_INLINE const Vector4& GetCameraRight() const { return m_cameraVectors[ CV_Right ]; }
	RED_INLINE const Vector4& GetCameraForward() const { return m_cameraVectors[ CV_Forward ]; }
	RED_INLINE const Vector4& GetCameraUp() const { return m_cameraVectors[ CV_Up ]; }
	RED_INLINE Bool IsOrtho() const { return m_fov == 0.0f; }
	RED_INLINE Bool IsPerspective() const { return m_fov != 0.0f; }
	RED_INLINE Bool IsReversedProjection() const { return m_isReversedProjection; }
	RED_INLINE Bool IsCameraCut() const { return m_isCameraCut; }
	RED_INLINE Float GetDepthBufferClearValue() const { return m_isReversedProjection ? 0.f : 1.f; }
	RED_INLINE const Matrix& GetWorldToScreenRevProjAware() const { return m_worldToScreenRevProjAware; }
	RED_INLINE const Matrix& GetViewToScreenRevProjAware() const { return m_viewToScreenRevProjAware; }
	RED_INLINE const Matrix& GetScreenToWorldRevProjAware() const { return m_screenToWorldRevProjAware; }
	RED_INLINE const SRenderingPlaneDef* GetWeaponPlaneDefinition() const { return m_weaponPlane.Get(); };
	RED_INLINE const red::SharedPtr< SRenderingPlaneDef > GetWeaponPlaneDefinitionShrPtr() const { return m_weaponPlane; };

	// Project a world position to screen space. Does not to divide-by-w.
	Vector4 ProjectWorldToScreen( const Vector3& worldPosition ) const;
	Vector4 ProjectWorldToScreen( const WorldPosition& worldPosition ) const;

	// Not so eficient as we want to know only the postprojected depth value
	RED_INLINE Float ProjectDepth( float distanceFromNearPlane ) const
	{
		const Vector4 pos = GetCameraForward() * distanceFromNearPlane + GetPosition().AsVector3();
		const Vector4 vsp = GetWorldToScreen().TransformVectorWithW( pos );
		return vsp.Z / vsp.W;
	}
	
	// Get corners of the frustum for given plane
	void GetFrustumPlaneCorners( const Float plane, Vector4* corners, Bool localCorners = false, Bool useJitter = false ) const;

	// Set all attributes
	void Set( const CreationContext& context );
	void Set( const WorldPosition& position, const Quaternion& rotation );
	void SetRotation( const Quaternion& rotation );
	void SetReversedProjection( Bool isReversed );
	void SetFOV( Float fov );
	void SetAspect( Float aspect );
	void SetShift( const Vector2& shift );

	void Sanitize();

	void SetSubpixelOffset( Float x, Float y, Uint32 screenW, Uint32 screenH, Uint32 jitterIndex = 0 );
	Float GetSubpixelOffsetX() const { return m_subPixelOffsetX; }
	Float GetSubpixelOffsetY() const { return m_subPixelOffsetY; }
	Uint32 GetSubpixelOffsetWidth() const { return m_subPixelOffsetWidth; }
	Uint32 GetSubpixelOffsetHeight() const { return m_subPixelOffsetHeight; }
	Uint32 GetJitterIndex() const { return m_jitterIndex; }

	void SetDepthEncodingMod( const SDepthBufferEncodingModParams &depthMod );
	RED_INLINE const SDepthBufferEncodingModParams& GetDepthEncodingMod() const { return m_depthMod; }	

public:
	RED_INLINE void SetNonDefaultFarRenderingPlane(){ m_nonDefaultFarPlane = true; }
	RED_INLINE void SetNonDefaultNearRenderingPlane(){ m_nonDefaultNearPlane = true; }
	RED_INLINE Bool GetNonDefaultFarRenderingPlane() const { return m_nonDefaultFarPlane; }
	RED_INLINE Bool GetNonDefaultNearRenderingPlane() const { return m_nonDefaultNearPlane; }
	void CalculateMatrices();

	// Calculate camera vectors for given rotation
	static void CalcCameraVectors( const Quaternion&rotation, Vector4 &outCameraForward, Vector4 &outCameraRight, Vector4 &outCameraUp );

	// Calculate and return frustum planes of current camera
	void CalculateFrustumPlanes( Vector4* outFrustumPlanes ) const;

protected:
	void CalculateRevProjAwareMatrices();
	void CalculateFOVMultiplier();
};

static_assert( sizeof( CRenderCamera ) < 2*1024 , "This starts to be too big. Consider creating CRenderCameraEx" );

/*
	Calculates shift for camera zooming. Appropriate matrix can be calculated as a translation matrix with given shift, and applied after the actual zoom gets applied.	
	TargetZoom : what is the zoom value we're trying to reach
	ZoomFactor : how much are we zoomed (effective zoom value is assumed to be lerp(1, TargetZoom, ZoomFactor)
	startPos   : indicates position on the screen in coordSystem below
	endPos     : at what position screen content that with zoomFactor==0 is going to be on the screen with zoomFactor==1

	Coord system (-1,-1)x(1,1):

	   ^
	   |
	---x--->	
	   |

	Zoom is interpreted here as magnification.
	Effective zoom should be applied to the original camera zoom through multiplication. 
	Note that in case of orthogonal camera zoom is a minification, so should be applied through division.
*/
RENDER_DATA_API Vector2 CalculateCameraShift( const Vector2 &startPos, const Vector2 &endPos, Float targetZoom, Float zoomFactor, Float *outCurrentZoom = nullptr );
