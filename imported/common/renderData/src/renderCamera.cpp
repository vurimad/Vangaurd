/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "renderCamera.h"
#include "renderPublicFunctions.h"
#include "../../renderData/include/resolutionParams.h"

const SRenderCameraLastFrameData SRenderCameraLastFrameData::INVALID;

static const Float INVALID_POSITION_THRESHOLD	= 0.5f;
static const Float INVALID_ROTATION_THRESHOLD	= 5.0f;
static const Float INVALID_FOV_THRESHOLD		= 2.0f;


const Matrix CRenderCamera::s_axesConversion( Vector4( 1, 0, 0, 0 ), Vector4( 0, 0, 1, 0 ), Vector4( 0, 1, 0, 0 ), Vector4( 0, 0, 0, 1 ) );


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static Matrix BuildJitterMatrix( Float subpixelOffsetX, Float subpixelOffsetY, Uint32 width, Uint32 height )
{
	Matrix jitterMatrix;
	jitterMatrix.SetIdentity();	
	jitterMatrix.SetTranslation( (subpixelOffsetX / Max<Uint32>( 1, width ) * 2.f), (subpixelOffsetY / Max<Uint32>( 1, height ) * 2.f), 0.f );
	return jitterMatrix;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

Vector2 CalculateCameraShift( const Vector2 &startPos, const Vector2 &endPos, Float targetZoom, Float zoomFactor, Float *outCurrentZoom )
{
	const Float customZoom = 1.f * (1 - zoomFactor) + targetZoom * zoomFactor;
	if ( nullptr != outCurrentZoom )
		*outCurrentZoom = customZoom;
	return -startPos * customZoom + startPos * (1 - zoomFactor) + endPos * zoomFactor;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

SRenderCameraLastFrameData::SRenderCameraLastFrameData()
{
	Invalidate();
}

SRenderCameraLastFrameData& SRenderCameraLastFrameData::Set( Bool isValid, const WorldPosition &position, const Quaternion& rotation, const Matrix &worldToView, const Matrix &viewToScreenRevProjAware, const Matrix &viewToScreenRevProjAwareNoJitter, const Matrix &screenToWorldRevProjAware, Float fov, Float subpixelOffsetX, Float subpixelOffsetY, Uint32 subpixelOffsetWidth, Uint32 subpixelOffsetHeight )
{
	m_isValid = isValid;
	m_position = position;
	m_rotation = rotation;
	m_worldToView = worldToView;
	m_viewToScreenRevProjAware = viewToScreenRevProjAware;
	m_viewToScreenRevProjAware_NoJitter = viewToScreenRevProjAwareNoJitter;
	m_screenToWorldRevProjAware = screenToWorldRevProjAware;
	m_fov = fov;
	m_subpixelOffsetX = subpixelOffsetX;
	m_subpixelOffsetY = subpixelOffsetY;
	m_subpixelOffsetWidth = subpixelOffsetWidth;
	m_subpixelOffsetHeight = subpixelOffsetHeight;

	RED_ASSERT( Validate() );

	return *this;
}

SRenderCameraLastFrameData& SRenderCameraLastFrameData::Init( const WorldPosition &position, const Quaternion& rotation, Float fov, Float aspect, Float nearPlane, Float farPlane, Float zoom, Vector2 shift, const Vector4& clippingPlane, Float subpixelOffsetX, Float subpixelOffsetY, Uint32 subpixelOffsetWidth, Uint32 subpixelOffsetHeight, const Matrix &screenToWorldRevProjAware )
{
	if( fov == 0.0f )
	{
		RED_FATAL_ASSERT( farPlane != nearPlane, "Insane near plane" );
		RED_FATAL_ASSERT( zoom > 0.0f, "Invalid orthogonal zoom" );
	}
	else
	{
		RED_FATAL_ASSERT( fov > 0.0f && m_fov < 180.0f, "Insane FOV ???" );
		RED_FATAL_ASSERT( nearPlane >= CRenderCamera::MIN_NEAR_PLANE, "Insane near plane" );
		RED_FATAL_ASSERT( farPlane <= CRenderCamera::MAX_FAR_PLANE, "Insane far plane" );
	}

	// Calculate view matrix
	Matrix rotMatrix = rotation.ToMatrix().Transposed();
	Matrix transMatrix = Matrix::IDENTITY();
	transMatrix.SetTranslation( -position.AsVector3() );
	Matrix worldToView = ( transMatrix * rotMatrix ) * CRenderCamera::s_axesConversion;

	// Build projection matrix
	Matrix viewToScreen;
	if ( fov == 0.0f )
	{
		viewToScreen.BuildOrthoLH( zoom * aspect, zoom, nearPlane, farPlane );
	}
	else
	{
		viewToScreen.BuildPerspectiveLH(DEG2RAD(fov), aspect, nearPlane, farPlane);
		viewToScreen[0][0] *= zoom;
		viewToScreen[1][1] *= zoom;
	}

	// Apply shift
	if ( math::Vector2::ZEROS() != shift )
	{
		Matrix shiftMatrix;
		shiftMatrix.SetIdentity();
		shiftMatrix.SetTranslation( Vector4( shift.X, shift.Y, 0 ) );
		viewToScreen = viewToScreen * shiftMatrix;
	}

	Matrix viewToScreenNoJitter = viewToScreen;

	// Apply subpixel offset
	if( subpixelOffsetX != 0 || subpixelOffsetY != 0 )
	{
		viewToScreen = viewToScreen * BuildJitterMatrix( subpixelOffsetX, subpixelOffsetY, subpixelOffsetWidth, subpixelOffsetHeight );
	}

	// Apply reversed projection
	Matrix reverseMatrix;
	reverseMatrix.SetIdentity();
	reverseMatrix.SetScale33( Vector4( 1.f, 1.f, -1.f ) );
	reverseMatrix.SetTranslation( 0.f, 0.f, 1.f );
	Matrix viewToScreenRevProjAware = viewToScreen * reverseMatrix;
	Matrix viewToScreenRevProjAwareNoJitter = viewToScreenNoJitter * reverseMatrix;

	//
	return Set( true, position, rotation, worldToView, viewToScreenRevProjAware, viewToScreenRevProjAwareNoJitter, screenToWorldRevProjAware, fov, subpixelOffsetX, subpixelOffsetY, subpixelOffsetWidth, subpixelOffsetHeight );
}

SRenderCameraLastFrameData& SRenderCameraLastFrameData::Init( const CRenderCamera &camera )
{
	return Init( camera.GetPosition(), camera.GetRotation(), camera.GetFOV(), camera.GetAspect(), camera.GetNearPlane(), camera.GetFarPlane(), camera.GetZoom(), camera.GetShift(), camera.GetClippingPlane(), camera.GetSubpixelOffsetX(), camera.GetSubpixelOffsetY(), camera.GetSubpixelOffsetWidth(), camera.GetSubpixelOffsetHeight(), camera.GetScreenToWorldRevProjAware() );
}

SRenderCameraLastFrameData& SRenderCameraLastFrameData::Invalidate()
{
	// Dont use any Matrix static variables 
	// in case of instances defined globally.

	m_isValid = false;
	m_position = WorldPosition(0, 0, 0);
	m_rotation = Quaternion::IDENTITY();
	m_worldToView.SetIdentity();
	m_viewToScreenRevProjAware.SetIdentity();
	m_screenToWorldRevProjAware.SetIdentity();
	m_fov = 0.0f;
	m_subpixelOffsetX = 0;
	m_subpixelOffsetY = 0;

	return *this;
}

Bool SRenderCameraLastFrameData::Validate() const
{ 
	if( m_fov == 0.0f )
	{
		// Nothing to check
	}
	else
	{
		RED_FATAL_ASSERT( m_fov > 0.0f && m_fov < 180.0f, "Insane FOV ???" );
	}

	RED_FATAL_ASSERT( !std::isnan( m_rotation.i ) && std::isfinite( m_rotation.i ) );
	RED_FATAL_ASSERT( !std::isnan( m_rotation.j ) && std::isfinite( m_rotation.j ) );
	RED_FATAL_ASSERT( !std::isnan( m_rotation.k ) && std::isfinite( m_rotation.k ) );
	RED_FATAL_ASSERT( !std::isnan( m_rotation.r ) && std::isfinite( m_rotation.r ) );

	RED_FATAL_ASSERT( std::abs( m_rotation.MagnitudeSq() - 1.0f ) < 1e-2f, "Unnormalized rotation" );

	RED_FATAL_ASSERT( Abs( m_subpixelOffsetX ) < 10.0f );
	RED_FATAL_ASSERT( Abs( m_subpixelOffsetY ) < 10.0f );

	return true;
}

WorldPosition SRenderCameraLastFrameData::GetPosition() const
{
	return m_position;
}

Vector4 SRenderCameraLastFrameData::GetCameraUp() const
{
	return m_worldToView.Transposed().GetAxisY();
}

Vector4 SRenderCameraLastFrameData::GetCameraForward() const
{
	return m_worldToView.Transposed().GetAxisZ();

}

Vector4 SRenderCameraLastFrameData::GetCameraRight() const
{
	return m_worldToView.Transposed().GetAxisX();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

SCameraRenderViewport::SCameraRenderViewport ()
{
	Reset();
}

SCameraRenderViewport::SCameraRenderViewport ( Uint32 sortPriority, const Rect &renderArea, const Rect &referenceArea )
{
	m_sortPriority = sortPriority;
	m_renderArea = renderArea;
	m_referenceArea = referenceArea;
}

void SCameraRenderViewport::Reset()
{	
	m_sortPriority = 0;
	m_renderArea = Rect ( 0, 1, 0, 1 );
	m_referenceArea = Rect ( 0, 1, 0, 1 );
}

Bool SCameraRenderViewport::IsOnscreenViewport() const
{
	return !m_referenceArea.IsEmpty();
}

Rect SCameraRenderViewport::GetRenderViewport( Int32 fullWidth, Int32 fullHeight ) const
{
	return GetRenderViewport( Rect ( 0, fullWidth, 0, fullHeight ) );
}

Rect SCameraRenderViewport::GetRenderViewport( const Rect &fullViewport ) const
{
	Rect result;

	if ( IsOnscreenViewport() )
	{
		const Rect onscreenViewport = GetOnscreenViewport( fullViewport );

		result.m_left = 0;
		result.m_top = 0;
		result.m_right = onscreenViewport.Width();
		result.m_bottom = onscreenViewport.Height();
	}
	else
	{
		RED_FATAL_ASSERT( 0 == m_renderArea.m_left && 0 == m_renderArea.m_top, "No reference area, so expecting only size, without positioning" );
		result = m_renderArea;
	}

	RED_FATAL_ASSERT( !result.IsEmpty(), "" );
	return result;
}	

Rect SCameraRenderViewport::GetOnscreenViewport( Int32 fullWidth, Int32 fullHeight ) const
{
	return GetOnscreenViewport( Rect ( 0, fullWidth, 0, fullHeight ) );
}

Rect SCameraRenderViewport::GetOnscreenViewport( const Rect &fullViewport ) const
{
	if ( !IsOnscreenViewport() )
	{
		return Rect ( 0, 0, 0, 0 );
	}
		
	RED_FATAL_ASSERT( !fullViewport.IsEmpty(), "" );
	RED_FATAL_ASSERT( !m_referenceArea.IsEmpty(), "" );
	const Float ratioLeft = (m_renderArea.m_left - (Float)m_referenceArea.m_left) / m_referenceArea.Width();
	const Float ratioRight = (m_renderArea.m_right - (Float)m_referenceArea.m_left) / m_referenceArea.Width();
	const Float ratioTop = (m_renderArea.m_top - (Float)m_referenceArea.m_top) / m_referenceArea.Height();
	const Float ratioBottom = (m_renderArea.m_bottom - (Float)m_referenceArea.m_top) / m_referenceArea.Height();

	// TODO: make this pixel perfect	
	Rect result; 
	result.m_left	= static_cast< Int32 >( ( fullViewport.m_left + ratioLeft * fullViewport.Width() ) + 0.5f );
	result.m_right	= static_cast< Int32 >( ( fullViewport.m_left + ratioRight * fullViewport.Width() ) + 0.5f );
	result.m_top	= static_cast< Int32 >( ( fullViewport.m_top + ratioTop * fullViewport.Height() ) + 0.5f );
	result.m_bottom = static_cast< Int32 >( ( fullViewport.m_top + ratioBottom * fullViewport.Height() ) + 0.5f );

	RED_FATAL_ASSERT( !result.IsEmpty(), "" );
	return result;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

SRenderingPlaneDef::SRenderingPlaneDef ()
	: m_fov ( 60.f )
	, m_zoom ( 1.f )
	, m_shift ( 0, 0 )
	, m_nearPlane ( rend::camera::NEAR_PLANE_DEFAULT )
	, m_farPlane( rend::camera::FAR_PLANE_DEFAULT )
{}

SRenderingPlaneDef::SRenderingPlaneDef( const CRenderCamera& camera )
{
	InitFromCamera( camera );
}

SRenderingPlaneDef::SRenderingPlaneDef( const SRenderingPlaneDef& renderingPlane )
{
	*this = renderingPlane;
}

SRenderingPlaneDef& SRenderingPlaneDef::InitFromCamera( const CRenderCamera &camera )
{
	m_fov = camera.GetFOV();
	m_zoom = camera.GetZoom();
	m_depthMod = camera.GetDepthEncodingMod();
	m_shift = camera.GetShift();
	m_nearPlane = camera.GetNearPlane();
	m_farPlane = camera.GetFarPlane();
	if ( camera.GetWeaponPlaneDefinition() != nullptr )
	{
		m_weaponPlaneParams = camera.GetWeaponPlaneDefinition()->m_weaponPlaneParams;
	}
	return *this;
}

Bool SRenderingPlaneDef::IsEquivalent( const CRenderCamera &camera ) const
{
	Bool identic =  m_fov == camera.GetFOV() &&
		m_zoom == camera.GetZoom() &&
		m_depthMod == camera.GetDepthEncodingMod() &&
		m_shift == camera.GetShift() &&
		m_nearPlane == camera.GetNearPlane() &&
		m_farPlane == camera.GetFarPlane();
	if ( identic && camera.GetWeaponPlaneDefinition() != nullptr )
	{
		const SWeaponPlaneParams& p = camera.GetWeaponPlaneDefinition()->m_weaponPlaneParams;
		identic = identic && m_weaponPlaneParams == p;
	}
	return identic;
}

void SRenderingPlaneDef::Apply( CRenderCamera &camera ) const
{
	if ( IsEquivalent( camera ) )
	{
		return;
	}

	CRenderCamera::CreationContext context;
	{
		context.rotation = camera.GetRotation();
		context.position = camera.GetPosition();
		context.fov = m_fov;
		context.aspect = camera.GetAspect();
		context.nearPlane = camera.GetNearPlane();
		context.farPlane = camera.GetFarPlane();
		context.zoom = m_zoom;
		context.shift = m_shift;
		context.isReversedProjection = camera.IsReversedProjection();
		context.nearPlane = m_nearPlane;
		context.farPlane = m_farPlane;
		context.weaponPlane = camera.GetWeaponPlaneDefinitionShrPtr();
	}
	camera.Set( context );
	camera.SetDepthEncodingMod( m_depthMod );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

CRenderCamera::CreationContext::CreationContext()
	: rotation( Quaternion::IDENTITY() )
	, position( Vector4::ZERO_3D_POINT() )
	, clippingPlane( Vector4::ZEROS() )
	, shift( Vector2::ZEROS() )
	, fov( 75.0f )
	, aspect( 1.0f )
	, nearPlane( rend::camera::NEAR_PLANE_DEFAULT )
	, farPlane( rend::camera::FAR_PLANE_DEFAULT )
	, zoom( 1.0f )
	, isReversedProjection( false )
	, isCameraCut( false )
{
}

CRenderCamera::CRenderCamera( const CreationContext& context )
	: m_fov( context.fov )
	, m_aspect( context.aspect )
	, m_position( context.position )
	, m_rotation( context.rotation )
	, m_nearPlane( context.nearPlane )
	, m_farPlane( context.farPlane )
	, m_zoom( context.zoom )
	, m_shift( context.shift )
	, m_subPixelOffsetX( 0 )
	, m_subPixelOffsetY( 0 )
	, m_subPixelOffsetWidth( 0 )
	, m_subPixelOffsetHeight( 0 )
	, m_jitterIndex( 0 )
	, m_nonDefaultFarPlane( false )
	, m_nonDefaultNearPlane( false )
	, m_isCameraCut( context.isCameraCut )
	, m_isReversedProjection( context.isReversedProjection )
	, m_cascadeRefPoint( context.position.AsVector3() )
	, m_clippingPlane( context.clippingPlane )
	, m_projectionOffset( Vector2::ZEROS() )
	, m_weaponPlane( context.weaponPlane )
{
	CalculateMatrices();
	CalculateFOVMultiplier();
}

CRenderCamera::CRenderCamera( const CreationContext& context, const Vector4& cascadeRefPoint )
	: m_fov( context.fov )
	, m_aspect( context.aspect )
	, m_position( context.position )
	, m_rotation( context.rotation )
	, m_nearPlane( context.nearPlane )
	, m_farPlane( context.farPlane )
	, m_zoom( context.zoom )
	, m_shift( context.shift )
	, m_subPixelOffsetX( 0 )
	, m_subPixelOffsetY( 0 )
	, m_subPixelOffsetWidth( 0 )
	, m_subPixelOffsetHeight( 0 )
	, m_jitterIndex( 0 )
	, m_nonDefaultFarPlane( false )
	, m_nonDefaultNearPlane( false )
	, m_isCameraCut( context.isCameraCut )
	, m_isReversedProjection( context.isReversedProjection )
	, m_cascadeRefPoint( cascadeRefPoint )
	, m_clippingPlane( context.clippingPlane )
	, m_projectionOffset( Vector2::ZEROS() )
	, m_weaponPlane( context.weaponPlane )
{
	CalculateMatrices();
	CalculateFOVMultiplier();
}

CRenderCamera::CRenderCamera( const CRenderCamera& camera ) = default;

void CRenderCamera::CalcCameraVectors( const Quaternion &rotation, Vector4 &outCameraForward, Vector4 &outCameraRight, Vector4 &outCameraUp )
{
	const Matrix rotMatrix = rotation.ToMatrix().Transposed();
	
	const Matrix &worldToCamera = rotMatrix;
	const Matrix worldToView = worldToCamera * s_axesConversion;
	const Matrix viewToWorld = worldToView.OrthonormInverted();

	// output result
	outCameraRight = viewToWorld.GetAxisX();
	outCameraForward = viewToWorld.GetAxisZ();
	outCameraUp = viewToWorld.GetAxisY();
}

void CRenderCamera::CalculateMatrices()
{
	Sanitize();

	// Calculate view matrix
	Matrix rotMatrix = m_rotation.ToMatrix().Transposed();
	Matrix transMatrix = Matrix::IDENTITY();
	transMatrix.SetTranslation( -m_position.AsVector3() );
	m_worldToCamera = transMatrix * rotMatrix;
	m_worldToView = m_worldToCamera * s_axesConversion;

	m_viewToWorld = m_worldToView.OrthonormInverted();
	m_cameraToWorld = m_worldToCamera.OrthonormInverted();

	// Build projection matrix
	if(m_fov == 0.0f)
	{
		m_viewToScreen.BuildOrthoLH( m_zoom * m_aspect, m_zoom, m_nearPlane, m_farPlane );
	}
	else
	{
		m_viewToScreen.BuildPerspectiveLH( DEG2RAD( m_fov ), m_aspect, m_nearPlane, m_farPlane );
		m_viewToScreen[0][0] *= m_zoom;
		m_viewToScreen[1][1] *= m_zoom;
	}

	// Apply shift
	if(Vector2::ZEROS() != m_shift)
	{
		Matrix shiftMatrix;
		shiftMatrix.SetIdentity();
		shiftMatrix.SetTranslation( Vector4( m_shift.X, m_shift.Y, 0 ) );
		m_viewToScreen = m_viewToScreen * shiftMatrix;
	}

	if(m_subPixelOffsetX != 0 || m_subPixelOffsetY != 0)
	{
		m_viewToScreen = m_viewToScreen * BuildJitterMatrix( m_subPixelOffsetX, m_subPixelOffsetY, m_subPixelOffsetWidth, m_subPixelOffsetHeight );
	}

	// Calculate view-projection matrix
	m_screenToView = m_viewToScreen.FullInverted();
	m_worldToScreen = m_worldToView * m_viewToScreen;
	m_screenToWorld = m_screenToView * m_viewToWorld;

	// Camera vectors - axes are flipped because "axesConversion"
	m_cameraVectors[CV_Right] = m_viewToWorld.GetAxisX();
	m_cameraVectors[CV_Forward] = m_viewToWorld.GetAxisZ();
	m_cameraVectors[CV_Up] = m_viewToWorld.GetAxisY();

	// Calculate reversed projection aware matrices
	CalculateRevProjAwareMatrices();
}

void CRenderCamera::CalculateRevProjAwareMatrices()
{
	if ( !m_isReversedProjection )
	{
		m_viewToScreenRevProjAware = m_viewToScreen;
		m_worldToScreenRevProjAware	= m_worldToScreen;
		m_screenToWorldRevProjAware = m_screenToWorld;
		return;
	}

	Matrix reverseMatrix;
	reverseMatrix.SetIdentity();
	reverseMatrix.SetScale33( Vector4( 1.f, 1.f, -1.f ) );
	reverseMatrix.SetTranslation( 0.f, 0.f, 1.f );

	m_viewToScreenRevProjAware = GetViewToScreen() * reverseMatrix;
	m_worldToScreenRevProjAware = GetWorldToView() * m_viewToScreenRevProjAware;
	m_screenToWorldRevProjAware = m_worldToScreenRevProjAware.FullInverted();
}

void CRenderCamera::Set( const CreationContext& context )
{
	m_fov = context.fov;
	m_aspect = context.aspect;
	m_position = context.position;
	m_rotation = context.rotation;
	m_nearPlane = context.nearPlane;
	m_farPlane = context.farPlane;
	m_zoom = context.zoom;
	m_shift = context.shift;
	m_isReversedProjection = context.isReversedProjection;
	m_isCameraCut = context.isCameraCut;
	m_cascadeRefPoint = context.position.AsVector3();
	m_clippingPlane = context.clippingPlane;
	m_weaponPlane = context.weaponPlane;
	m_nearPlane = context.nearPlane;
	m_farPlane = context.farPlane;

	CalculateMatrices();
	CalculateFOVMultiplier();
}

void CRenderCamera::Set( const WorldPosition& position, const Quaternion& rotation )
{
	if ( m_position != position || m_rotation != rotation )
	{
		m_position = position;
		m_rotation = rotation;
		m_cascadeRefPoint = position.AsVector3();

		CalculateMatrices();
	}
}

void CRenderCamera::SetRotation( const Quaternion& rotation )
{
	m_rotation = rotation;

	CalculateMatrices();
}

void CRenderCamera::SetFOV( Float fov )
{
	m_fov = fov;

	CalculateMatrices();
	CalculateFOVMultiplier();
}

void CRenderCamera::SetAspect( Float aspect )
{
	if ( aspect != m_aspect )
	{
		m_aspect = aspect;

		CalculateMatrices();
	}
}

void CRenderCamera::SetShift( const Vector2& shift )
{
	if ( shift != m_shift )
	{
		m_shift = shift;

		CalculateMatrices();
	}
}

void CRenderCamera::Sanitize()
{
	// We don't want to assert here.
	// Getting an assert here leaves us helpless, because we don't know what issued the bad settings.
	// So instead, sanitize the camera settings! And log. Once.
	if( m_fov == 0.0f )
	{
		RED_ERROR_ONCE( m_farPlane > m_nearPlane + 0.1f, "A camera with wrong near/far plane setup is used. Near: %f, Far: %f", m_nearPlane, m_farPlane );
		m_farPlane = math::Max( m_nearPlane + 0.1f, m_farPlane );

		RED_ERROR_ONCE( m_zoom >= FLT_EPSILON, "Invalid orthogonal zoom: %f", m_zoom );
		m_zoom = math::Max( FLT_EPSILON, m_zoom );
	}
	else
	{
		RED_ERROR_ONCE( m_fov >= 5.0f && m_fov <= 170.0f, "Insane FOV: %f ???", m_fov );
		math::Clamp( m_fov, 5.0f, 170.0f );

		RED_ERROR_ONCE( m_nearPlane >= MIN_NEAR_PLANE, "Near plane too near (%f, limit: %f)", m_nearPlane, MIN_NEAR_PLANE );
		m_nearPlane = math::Max( m_nearPlane, Float(MIN_NEAR_PLANE) );

		RED_ERROR_ONCE( m_farPlane <= MAX_FAR_PLANE, "Far plane too far (%f, limit: %f)", m_farPlane, MAX_FAR_PLANE );
		m_farPlane = math::Min( m_farPlane, Float(MAX_FAR_PLANE) );
	}

	RED_FATAL_ASSERT( !std::isnan( m_aspect ) && std::isfinite( m_aspect ) && m_aspect > 0.0f, "Invalid aspect: %f", m_aspect );

	RED_FATAL_ASSERT( !std::isnan( m_rotation.i ) && std::isfinite( m_rotation.i ), "Invalid rotation: [%f, %f, %f; %f]", m_rotation.i, m_rotation.j, m_rotation.k, m_rotation.r );
	RED_FATAL_ASSERT( !std::isnan( m_rotation.j ) && std::isfinite( m_rotation.j ), "Invalid rotation: [%f, %f, %f; %f]", m_rotation.i, m_rotation.j, m_rotation.k, m_rotation.r );
	RED_FATAL_ASSERT( !std::isnan( m_rotation.k ) && std::isfinite( m_rotation.k ), "Invalid rotation: [%f, %f, %f; %f]", m_rotation.i, m_rotation.j, m_rotation.k, m_rotation.r );
	RED_FATAL_ASSERT( !std::isnan( m_rotation.r ) && std::isfinite( m_rotation.r ), "Invalid rotation: [%f, %f, %f; %f]", m_rotation.i, m_rotation.j, m_rotation.k, m_rotation.r );

	RED_FATAL_ASSERT( std::abs( m_rotation.MagnitudeSq() - 1.0f ) < 1e-2f, "Unnormalized rotation [%f, %f, %f; %f] (MagSq == %f)", m_rotation.i, m_rotation.j, m_rotation.k, m_rotation.r, m_rotation.MagnitudeSq() );
}

void CRenderCamera::SetReversedProjection( Bool isReversed )
{
	if ( isReversed == m_isReversedProjection )
	{
		return;
	}

	m_isReversedProjection = isReversed;
	CalculateRevProjAwareMatrices();
}

const Matrix CRenderCamera::GetScreenToWorld_NoTranslate( Bool useJitter /*= true*/ ) const
{
	Matrix mat;
	{
		// Calculate view matrix
		Matrix rotMatrix = m_rotation.ToMatrix().Transposed();
		if( m_clippingPlane != Vector4::ZEROS() )
		{
			rotMatrix.SetIdentity();
			rotMatrix = rotMatrix.Matrix::BuildFromDirectionVector( m_clippingPlane.AsVector3() ).Transposed();
		}
		Matrix worldToCamera = rotMatrix;
		Matrix worldToView = worldToCamera * s_axesConversion;
		Matrix viewToWorld = worldToView.OrthonormInverted();

		// Build projection matrix
		Matrix viewToScreen;
		if( m_fov == 0.0f )
		{
			viewToScreen.BuildOrthoLH( m_zoom * m_aspect, m_zoom, m_nearPlane, m_farPlane );
		}
		else
		{
			viewToScreen.BuildPerspectiveLH( DEG2RAD( m_fov ), m_aspect, m_nearPlane, m_farPlane );

			viewToScreen[0][0] *= m_zoom;
			viewToScreen[1][1] *= m_zoom;
		}

		// Apply shift
		if( Vector2::ZEROS() != m_shift )
		{
			Matrix shiftMatrix;
			shiftMatrix.SetIdentity();
			shiftMatrix.SetTranslation( Vector4( m_shift.X, m_shift.Y, 0 ) );
			viewToScreen = viewToScreen * shiftMatrix;
		}

		// Apply depth scale
		if( !m_depthMod.IsNeutral() )
		{
			Matrix zScaleMatrix;
			zScaleMatrix.SetIdentity();
			zScaleMatrix.SetScale( Vector4( 1.f, 1.f, m_depthMod.m_depthScale ) );
			zScaleMatrix.SetTranslation( Vector4( 0.f, 0.f, m_depthMod.m_depthBias ) );
			viewToScreen = viewToScreen * zScaleMatrix;
		}

		if( useJitter )
		{
			if( m_subPixelOffsetX != 0 || m_subPixelOffsetY != 0 )
			{
				viewToScreen = viewToScreen * BuildJitterMatrix( m_subPixelOffsetX, m_subPixelOffsetY, m_subPixelOffsetWidth, m_subPixelOffsetHeight );
			}
		}

		Matrix screenToView = viewToScreen.FullInverted();
		mat = screenToView * viewToWorld;
	}

	return mat;
}


Vector4 CRenderCamera::ProjectWorldToScreen( const Vector3& worldPosition ) const
{
	//const Matrix rotMatrix = m_rotation.ToMatrix().Transposed();
	//const Matrix worldToViewCompensated = (rotMatrix * s_axesConversion);
	Matrix worldToViewCompensated = m_worldToView;
	worldToViewCompensated.SetTranslation( 0.0f, 0.0f, 0.0f );
	const Matrix worldToScreenCompensated = worldToViewCompensated * m_viewToScreen;
	return worldToScreenCompensated.TransformVectorWithW( Vector4( WorldPosition(worldPosition) - m_position, 1.0f ) );
}

Vector4 CRenderCamera::ProjectWorldToScreen( const WorldPosition& worldPosition ) const
{
	//const Matrix rotMatrix = m_rotation.ToMatrix().Transposed();
	//const Matrix worldToViewCompensated = (rotMatrix * s_axesConversion);
	Matrix worldToViewCompensated = m_worldToView;
	worldToViewCompensated.SetTranslation( 0.0f, 0.0f, 0.0f );
	const Matrix worldToScreenCompensated = worldToViewCompensated * m_viewToScreen;
	return worldToScreenCompensated.TransformVectorWithW( Vector4( worldPosition - m_position, 1.0f ) );
}


void CRenderCamera::GetFrustumPlaneCorners( const Float plane, Vector4* corners, Bool localCorners /*= false*/, Bool useJitter /*= false*/ ) const
{
	const Matrix mat = localCorners ? GetScreenToWorld_NoTranslate( useJitter ) : GetScreenToWorld();

	corners[0] = mat.TransformVectorWithW(Vector4(+1, -1, plane));
	corners[1] = mat.TransformVectorWithW(Vector4(+1, +1, plane));
	corners[2] = mat.TransformVectorWithW(Vector4(-1, +1, plane));
	corners[3] = mat.TransformVectorWithW(Vector4(-1, -1, plane));

	corners[0] /= corners[0].W;
	corners[1] /= corners[1].W;
	corners[2] /= corners[2].W;
	corners[3] /= corners[3].W;
}

void CRenderCamera::SetDepthEncodingMod( const SDepthBufferEncodingModParams &depthMod )
{
	if ( depthMod == m_depthMod )
	{
		return;
	}

	m_depthMod = depthMod;

	CalculateMatrices();
}

void CRenderCamera::SetSubpixelOffset( Float x, Float y, Uint32 screenW, Uint32 screenH, Uint32 jitterIndex /*= 0*/ )
{
	RED_ASSERT( screenW > 0 );
	RED_ASSERT( screenH > 0 );

	m_subPixelOffsetX = x;
	m_subPixelOffsetY = y;
	m_subPixelOffsetWidth = screenW;
	m_subPixelOffsetHeight = screenH;
	m_jitterIndex = jitterIndex;

	CalculateMatrices();
}

void CRenderCamera::CalculateFOVMultiplier()
{
	m_fovMultiplier = ::CalcFovDistanceMultiplier( GetFOV() );
}

void CRenderCamera::CalculateFrustumPlanes( Vector4* outFrustumPlanes ) const
{
	Matrix m = m_worldToScreen;

	// Left plane
	outFrustumPlanes[0].X = m.GetRow(0).W + m.GetRow(0).X;
	outFrustumPlanes[0].Y = m.GetRow(1).W + m.GetRow(1).X;
	outFrustumPlanes[0].Z = m.GetRow(2).W + m.GetRow(2).X;
	outFrustumPlanes[0].W = m.GetRow(3).W + m.GetRow(3).X;

	// Right plane
	outFrustumPlanes[1].X = m.GetRow(0).W - m.GetRow(0).X;
	outFrustumPlanes[1].Y = m.GetRow(1).W - m.GetRow(1).X;
	outFrustumPlanes[1].Z = m.GetRow(2).W - m.GetRow(2).X;
	outFrustumPlanes[1].W = m.GetRow(3).W - m.GetRow(3).X;

	// Top plane
	outFrustumPlanes[2].X = m.GetRow(0).W - m.GetRow(0).Y;
	outFrustumPlanes[2].Y = m.GetRow(1).W - m.GetRow(1).Y;
	outFrustumPlanes[2].Z = m.GetRow(2).W - m.GetRow(2).Y;
	outFrustumPlanes[2].W = m.GetRow(3).W - m.GetRow(3).Y;

	// Bottom plane
	outFrustumPlanes[3].X = m.GetRow(0).W + m.GetRow(0).Y;
	outFrustumPlanes[3].Y = m.GetRow(1).W + m.GetRow(1).Y;
	outFrustumPlanes[3].Z = m.GetRow(2).W + m.GetRow(2).Y;
	outFrustumPlanes[3].W = m.GetRow(3).W + m.GetRow(3).Y;

	// Near plane
	outFrustumPlanes[4].X = m.GetRow(0).Z;
	outFrustumPlanes[4].Y = m.GetRow(1).Z;
	outFrustumPlanes[4].Z = m.GetRow(2).Z;
	outFrustumPlanes[4].W = m.GetRow(3).Z;

	// Far plane
	outFrustumPlanes[5].X = m.GetRow(0).W - m.GetRow(0).Z;
	outFrustumPlanes[5].Y = m.GetRow(1).W - m.GetRow(1).Z;
	outFrustumPlanes[5].Z = m.GetRow(2).W - m.GetRow(2).Z;
	outFrustumPlanes[5].W = m.GetRow(3).W - m.GetRow(3).Z;

	// Normalize plane
	for( Uint8 i = 0; i < 6; ++i )
	{
		outFrustumPlanes[i] /= outFrustumPlanes[i].Mag3();
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
