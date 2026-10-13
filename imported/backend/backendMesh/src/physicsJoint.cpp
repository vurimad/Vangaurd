/* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.*/

#include "build.h"
#include "physicsJoint.h"
#include "physicsEditorScene.h"
#include "physicsActor.h"
#include "math.h"

#include "../../../common/renderDraw/include/fragmentBuilder.h"
#include "../../../common/redReflection/include/rttiPathParser.h"
#include "../../../common/renderData/include/drawer.h"

// EPhysicsJointAxis

RTTI_BEGIN_ENUM_IN_NAMESPACE( EPhysicsJointAxis, physics )
	RTTI_ENUM_OPTION_ALIAS( "X", EPJA_X );
	RTTI_ENUM_OPTION_ALIAS( "Y", EPJA_Y );
	RTTI_ENUM_OPTION_ALIAS( "Z", EPJA_Z );
	RTTI_ENUM_OPTION_ALIAS( "Twist", EPJA_Twist );
	RTTI_ENUM_OPTION_ALIAS( "SwingY", EPJA_Swing1 );
	RTTI_ENUM_OPTION_ALIAS( "SwingZ", EPJA_Swing2 );
RTTI_END_ENUM();	  	


// EPhysicsJointMotion

RTTI_BEGIN_ENUM_IN_NAMESPACE( EPhysicsJointMotion, physics )
	RTTI_ENUM_OPTION_ALIAS( "Locked", EPJM_Locked );
	RTTI_ENUM_OPTION_ALIAS( "Limited", EPJM_Limited );
	RTTI_ENUM_OPTION_ALIAS( "Free", EPJM_Free);
RTTI_END_ENUM();

// EPhysicsJointDrive

RTTI_BEGIN_ENUM_IN_NAMESPACE( EPhysicsJointDrive, physics )
RTTI_ENUM_OPTION_ALIAS( "X", EPJD_X );
RTTI_ENUM_OPTION_ALIAS( "Y", EPJD_Y );
RTTI_ENUM_OPTION_ALIAS( "Z", EPJD_Z );
RTTI_ENUM_OPTION_ALIAS( "Swing", EPJD_Swing );
RTTI_ENUM_OPTION_ALIAS( "Twist", EPJD_Twist );
RTTI_ENUM_OPTION_ALIAS( "SLERP", EPJD_SLERP );
RTTI_END_ENUM();

// SPhysicsJointDrive

RTTI_BEGIN_TYPE_IN_NAMESPACE( SPhysicsJointDrive, physics );
RTTI_PROPERTY( m_forceLimit ).editable().tooltip( "Force limit of a drive." );
RTTI_PROPERTY( m_isAcceleration ).editable().tooltip( "When set, the drive's spring will be use as acceleration not the force." );
RTTI_PROPERTY( m_stiffness ).editable().tooltip( "The spring strength of the drive: that is, the force proportional to the position error." );
RTTI_PROPERTY( m_damping ).editable().tooltip( "The damping strength of the drive: that is, the force proportional to the velocity error." );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( SPhysicsJointDriveVelocity, physics );
RTTI_PROPERTY( m_linearVelocity ).editable().tooltip( "The linear goal velocity for drive." );
RTTI_PROPERTY( m_angularVelocity ).editable().tooltip( "The angular goal velocity for drive." );
RTTI_END_TYPE();

// PhysicsJointLimitBase

RTTI_BEGIN_TYPE_IN_NAMESPACE( SPhysicsJointLimitBase, physics );
RTTI_PROPERTY( m_restitution ).editable().range(0.f, 1.f).tooltip( "Determines how much will a joint bounce when hitting the limit [0, 1]." );
RTTI_PROPERTY( m_bounceThreshold ).editable().tooltip( "Determines minimum impact velocity required for the joint to bounce." );
RTTI_PROPERTY( m_stiffness ).editable().tooltip( "Higher than zero means a soft limit [0, RED_FLT_MAX)." );
RTTI_PROPERTY( m_damping ).editable().tooltip( "If spring is greater than 0, this determines its damping [0, RED_FLT_MAX)." );
RTTI_PROPERTY( m_contactDistance ).editable().tooltip( "Determines when a limit is considered active by solver. Too small might cause jitter." );
RTTI_END_TYPE();


// PhysicsJointLinearLimit

RTTI_BEGIN_TYPE_IN_NAMESPACE( SPhysicsJointLinearLimit, physics );
RTTI_PARENT_TYPE( SPhysicsJointLimitBase )
	RTTI_PROPERTY( m_x ).editable().tooltip( "Motion type along the X axis." );
	RTTI_PROPERTY( m_y ).editable().tooltip( "Motion type along the Y axis." );
	RTTI_PROPERTY( m_z ).editable().tooltip( "Motion type along the Z axis." );
	RTTI_PROPERTY( m_value ).editable().tooltip( "Extent of the limit (0, RED_FLT_MAX)." );
RTTI_END_TYPE();


// PhysicsJointAngularLimitPair

RTTI_BEGIN_TYPE_IN_NAMESPACE( SPhysicsJointAngularLimitPair, physics );
RTTI_PARENT_TYPE( SPhysicsJointLimitBase )
RTTI_PROPERTY( m_twist ).editable().tooltip( "Motion type around the X axis" );
RTTI_PROPERTY( m_upper ).editable().range( -180.f, 180.f ).tooltip( "Upper limit. Must be bigger than lower limit. (-180, 180)." );
RTTI_PROPERTY( m_lower ).editable().range( -180.f, 180.f ).tooltip( "Lower limit. Must be lower than upper limit. (-180, 180)." );
RTTI_END_TYPE();


// PhysicsJointLimitConePair

RTTI_BEGIN_TYPE_IN_NAMESPACE( SPhysicsJointLimitConePair, physics );
RTTI_PARENT_TYPE( SPhysicsJointLimitBase )
	RTTI_PROPERTY( m_swingY ).editable().tooltip( "Motion type around the Y axis" );
	RTTI_PROPERTY( m_swingZ ).editable().tooltip( "Motion type around the Z axis" );
	RTTI_PROPERTY( m_yAngle ).editable().range(0.f, 180.f).tooltip( "The maximum angle from the Y axis of the constraint frame. (0, 180)" );
	RTTI_PROPERTY( m_zAngle ).editable().range(0.f, 180.f).tooltip( "The maximum angle from the Z axis of the constraint frame. (0, 180)" );
RTTI_END_TYPE();


// PhysicsJoint

RTTI_BEGIN_TYPE_IN_NAMESPACE( PhysicsJoint, physics );
	RTTI_PARENT_TYPE( physics::PhysicsBase );
	RTTI_PROPERTY_CATEGORY("2. Joint properties");
	RTTI_PROPERTY( m_projectionEnabled ).editable().tooltip( 
		"Whether the actors should get projected for this constraint. This will try to bring violated constraints back into alignment when solver fails. Very small tolerance values for projection may result in jittering around the joint. Avoid using, when possible." );
	RTTI_PROPERTY( m_linearTolerance ).editable().tooltip( "With projection enabled, this will determine the linear value beyond which the joint will be projected. If the joint separates by more than this distance along its locked degrees of freedom, the solver will move the bodies to close the distance." );
	RTTI_PROPERTY(m_angularTolerance).editable().range( 0.0f, RED_PI ).tooltip( "With projection enabled, this will determine the angular value beyond which the joint will be projected. Angular projection is implemented only for the case of two or three locked angular degrees of freedom." );
	RTTI_PROPERTY( m_isBreakable ).editable().tooltip( "If the constraint force or torque on the joint exceeds the specified values and this flag is set, the joint will break." );
	RTTI_PROPERTY( m_breakingForce ).editable().tooltip( "If the constraint force on the joint exceeds this value and isBreakable flag is set, the joint will break." );
	RTTI_PROPERTY( m_breakingTorque ).editable().tooltip( "If the constraint torque on the joint exceeds this value and isBreakable flag is set, the joint will break." );
	RTTI_PROPERTY(m_actors).editable().tooltip( "The actors connected by the joint. The first one is a parent (for editor purposes)." );
	RTTI_PROPERTY(m_localFrames).editable().tooltip( "Transforms of the joint relative to the respective actor." );
	RTTI_PROPERTY_CATEGORY("3. Joint Limits");
	RTTI_PROPERTY( m_linearLimit ).editable().tooltip( 
		"A single limit constraints all linear limited degrees of freedom, forming a linear, circular or spherical constraint on motion depending on the number of limited degrees." );
	RTTI_PROPERTY( m_twistLimit ).editable().tooltip( "The twist limit controls the range of motion around the twist axis." );
	RTTI_PROPERTY( m_swingLimit ).editable().tooltip( "The cone limit is used if either or both swing axes are limited." );
	RTTI_PROPERTY_CATEGORY("4. Joint Drives");
	RTTI_PROPERTY( m_driveY ).editable().tooltip( "Drive along the X-axis." );
	RTTI_PROPERTY( m_driveX ).editable().tooltip( "Drive along the Y-axis." );
	RTTI_PROPERTY( m_driveZ ).editable().tooltip( "Drive along the Z-axis." );
	RTTI_PROPERTY( m_driveTwist ).editable().tooltip( "Drive of displacement from the X-axis." );
	RTTI_PROPERTY( m_driveSwing ).editable().tooltip( "Drive of the displacement around the X-axis." );
	RTTI_PROPERTY( m_driveSLERP ).editable().tooltip( "Drive of all three angular degrees along a SLERP-path." );
	RTTI_PROPERTY( m_driveVelocity ).editable().tooltip( "Target goal velocity for drive. The velocity is measured in the constraint frame of actor 0." );
	RTTI_PROPERTY( m_drivePosition ).editable().tooltip( "Drive goal transform. The goal is relative to the constraint frame of actor 0." );
RTTI_END_TYPE();

namespace physics
{
	// helpers

	namespace helpers
	{
		Float Coslerp( Float t )
		{
			return 1.f - MCos(t * RED_PI_HALF );
		}

		Float Sinlerp( Float t )
		{
			return MSin( t * RED_PI_HALF );
		}
	}

	// Physics Joint Drives

	SPhysicsJointDrive::SPhysicsJointDrive()
		: m_stiffness( 0.f )
		, m_damping( 0.f )
		, m_forceLimit( 0.f )
		, m_isAcceleration( false )
	{}


	SPhysicsJointDrive::SPhysicsJointDrive( Float driveStiffness, Float driveDamping, Float driveForceLimit, Bool isAcceleration /*= false */ )
		: m_stiffness( driveStiffness )
		, m_damping( driveDamping )
		, m_forceLimit( driveForceLimit )
		, m_isAcceleration( isAcceleration )
	{}

	Bool SPhysicsJointDrive::IsValid() const
	{
		return std::isfinite( m_stiffness ) && m_stiffness >= 0.f &&
			std::isfinite( m_damping ) && m_damping >= 0.f &&
			std::isfinite( m_forceLimit ) && m_forceLimit >= 0.f;
	}

	Bool SPhysicsJointDrive::IsActive() const
	{
		return ( m_damping != 0 || m_stiffness != 0 );
	}

	SPhysicsJointDriveVelocity::SPhysicsJointDriveVelocity()
		: m_linearVelocity( Vector4::ZERO_3D_POINT() )
		, m_angularVelocity( Vector4::ZERO_3D_POINT() )
	{}

	SPhysicsJointDriveVelocity::SPhysicsJointDriveVelocity( Vector4 linearVelocity, Vector4 angularVelocity )
		: m_linearVelocity( linearVelocity )
		, m_angularVelocity( angularVelocity )
	{}


	// -- Physics Joint Drives

	// Physics Joint limits

	// SPhysicsJointLimitBase

	SPhysicsJointLimitBase::SPhysicsJointLimitBase()
		: m_restitution( 0.f )
		, m_bounceThreshold( 0.f )
		, m_stiffness( 0.f )
		, m_damping( 0.f )
		, m_contactDistance( 0.f )
	{}
	
	Bool SPhysicsJointLimitBase::IsValid() const
	{
		return	std::isfinite( m_restitution ) && m_restitution >= 0.f		&& m_restitution <= 1.f &&
			std::isfinite( m_stiffness ) && m_stiffness >= 0.f		&&
			std::isfinite( m_damping ) && m_damping >= 0.f			&&
			std::isfinite( m_bounceThreshold ) && m_bounceThreshold >= 0.f	&&
			std::isfinite( m_contactDistance ) && m_contactDistance >= 0.f;
	}

	Bool SPhysicsJointLimitBase::IsSoft() const
	{
		return m_damping > 0.f || m_stiffness > 0.f;
	}

	// -- SPhysicsJointLimitBase

	// SPhysicsJointLinearLimit

	SPhysicsJointLinearLimit::SPhysicsJointLinearLimit()
		: SPhysicsJointLimitBase()
		, m_value( 0.0f )
		, m_x( EPJM_Locked )
		, m_y( EPJM_Locked )
		, m_z( EPJM_Locked )
	{}

	SPhysicsJointLinearLimit::SPhysicsJointLinearLimit( SPhysicsTolerancesScale scale, Float extent, Float contactDist /*= -1.f*/ )
		: SPhysicsJointLimitBase()
		, m_value( extent )
		, m_x( EPJM_Locked )
		, m_y( EPJM_Locked )
		, m_z( EPJM_Locked )
	{
		m_contactDistance = contactDist == -1 ? 0.01f * scale.m_length : contactDist;
	}

	SPhysicsJointLinearLimit::SPhysicsJointLinearLimit( Float extent, Float stiffness, Float damping )
		: SPhysicsJointLimitBase()
		, m_value( extent )
		, m_x( EPJM_Locked )
		, m_y( EPJM_Locked )
		, m_z( EPJM_Locked )
	{
		m_stiffness = stiffness;
		m_damping = damping;
	}

	Bool SPhysicsJointLinearLimit::IsValid() const
	{
		return SPhysicsJointLimitBase::IsValid() &&
			std::isfinite( m_value ) && m_value > 0.f;
	}

	physics::EPhysicsJointMotion SPhysicsJointLinearLimit::GetMotion( EPhysicsJointAxis axis ) const
	{
		switch( axis )
		{
			case physics::EPJA_X:
				return m_x;
			case physics::EPJA_Y:
				return m_y;
			case physics::EPJA_Z:
				return m_z;
			default:
				return EPJM_Locked;
		}
	}

	// -- SPhysicsJointLinearLimit

	// SPhysicsJointAngularLimitPair

	SPhysicsJointAngularLimitPair::SPhysicsJointAngularLimitPair()
		: SPhysicsJointLimitBase()
		, m_upper( 0.0f )
		, m_lower( 0.0f )
		, m_twist( EPJM_Locked )
	{}

	SPhysicsJointAngularLimitPair::SPhysicsJointAngularLimitPair( Float lower, Float upper, Float contactDist /*= -1.f*/ )
		: SPhysicsJointLimitBase()
		, m_upper( upper )
		, m_lower( lower )
		, m_twist( EPJM_Locked )
	{
		m_upper = Clamp<Float>( m_upper, m_lower, 180.f );
		m_lower = Clamp<Float>( m_lower, -180.f, m_upper );

		m_contactDistance = contactDist == -1 ? math::Min( 0.1f, 0.49f*( upper - lower ) ) : contactDist;
		m_bounceThreshold = 0.5f;
	}

	SPhysicsJointAngularLimitPair::SPhysicsJointAngularLimitPair( Float lower, Float upper, Float stiffness, Float damping )
		: SPhysicsJointLimitBase()
		, m_upper( upper )
		, m_lower( lower )
		, m_twist( EPJM_Locked )
	{
		m_upper = Clamp<Float>( m_upper, m_lower, 180.f );
		m_lower = Clamp<Float>( m_lower, -180.f, m_upper );

		m_stiffness = stiffness;
		m_damping = damping;
	}

	Float SPhysicsJointAngularLimitPair::GetAngleUpper() const
	{
		if( m_twist == EPJM_Free )
		{
			return 180.f;
		}
		else if( m_twist == EPJM_Locked )
		{
			return 0.f;
		}
		else
		{
			return m_upper;
		}
	}

	Float SPhysicsJointAngularLimitPair::GetAngleLower() const
	{
		if( m_twist == EPJM_Free )
		{
			return -180.f;
		}
		else if( m_twist == EPJM_Locked )
		{
			return 0.f;
		}
		else
		{
			return m_lower;
		}
	}

	Float SPhysicsJointAngularLimitPair::GetAngleUpperRadians() const
	{
		if( m_twist != EPJM_Limited )
		{
			return 0.0f;
		}
		else
		{
			return ( m_upper / 180.f ) * RED_PI;
		}
	}

	Float SPhysicsJointAngularLimitPair::GetAngleLowerRadians() const
	{
		if( m_twist != EPJM_Limited )
		{
			return 0.0f;
		}
		else
		{
			return ( m_lower / 180.f ) * RED_PI;
		}
	}

	physics::EPhysicsJointMotion SPhysicsJointAngularLimitPair::GetMotion( EPhysicsJointAxis axis ) const
	{
		if( axis == physics::EPJA_Twist )
		{
			if( m_twist == EPJM_Limited )
			{
				Float angle = ( m_upper - m_lower );
				if( angle <= 0 )
				{
					return EPJM_Locked;
				}
				else if( angle >= 180.0f )
				{
					return EPJM_Limited;
				}
			}
			return m_twist;
		}
		else
		{
			return EPJM_Locked;
		}
	}

	Bool SPhysicsJointAngularLimitPair::IsValid() const
	{
		Float upperRad = GetAngleUpperRadians();
		Float lowerRad = GetAngleLowerRadians();
		return SPhysicsJointLimitBase::IsValid() &&
			std::isfinite( upperRad ) && std::isfinite( lowerRad ) && ( upperRad >= lowerRad ) &&
			std::isfinite( m_contactDistance ) && ( m_contactDistance <= ( upperRad - lowerRad ) );
	}

	// -- SPhysicsJointAngularLimitPair

	// SPhysicsJointLimitConePair

	SPhysicsJointLimitConePair::SPhysicsJointLimitConePair()
		: SPhysicsJointLimitBase()
		, m_yAngle( 0.0f )
		, m_zAngle( 0.0f )
		, m_swingY( EPJM_Locked )
		, m_swingZ( EPJM_Locked )
	{}

	SPhysicsJointLimitConePair::SPhysicsJointLimitConePair( Float yAngle, Float zAngle, Float contactDist /*= -1.f*/ )
		: SPhysicsJointLimitBase()
	{
		m_yAngle = Clamp<Float>( yAngle, 0.f, 180.f );
		m_zAngle = Clamp<Float>( zAngle, 0.f, 180.f );
		m_swingY = yAngle > 0 ? ( yAngle < 180.0f ? EPJM_Limited : EPJM_Free ) : EPJM_Locked;
		m_swingZ = zAngle > 0 ? ( yAngle < 180.0f ? EPJM_Limited : EPJM_Free ) : EPJM_Locked;

		m_contactDistance = contactDist == -1 ? math::Min( 0.1f, 0.49f*( math::Min( zAngle, yAngle ) ) ) : contactDist;
		m_bounceThreshold = 0.5f;
	}

	SPhysicsJointLimitConePair::SPhysicsJointLimitConePair( Float yAngle, Float zAngle, Float stiffness, Float damping )
		: SPhysicsJointLimitBase()
	{
		m_yAngle = Clamp<Float>( yAngle, 0.f, 180.f );
		m_zAngle = Clamp<Float>( zAngle, 0.f, 180.f );
		m_swingY = yAngle > 0 ? ( yAngle < 180.0f ? EPJM_Limited : EPJM_Free ) : EPJM_Locked;
		m_swingZ = zAngle > 0 ? ( yAngle < 180.0f ? EPJM_Limited : EPJM_Free ) : EPJM_Locked;
		m_stiffness = stiffness;
		m_damping = damping;
	}

	Float SPhysicsJointLimitConePair::GetAngleY() const
	{
		return GetAngle( EPJA_Swing1 );
	}

	Float SPhysicsJointLimitConePair::GetAngleZ() const
	{
		return GetAngle( EPJA_Swing2 );
	}

	Float SPhysicsJointLimitConePair::GetAngleYRadians() const
	{
		return GetAngleRadians( GetAngle( EPJA_Swing1 ) );
	}

	Float SPhysicsJointLimitConePair::GetAngleZRadians() const
	{
		return GetAngleRadians( GetAngle( EPJA_Swing2 ) );
	}

	Float SPhysicsJointLimitConePair::GetAngle( EPhysicsJointAxis ax ) const
	{
		if( GetMotion( ax ) == EPJM_Free )
		{
			return 180.f;
		}
		else if( GetMotion( ax ) == EPJM_Locked )
		{
			return FLT_EPSILON;
		}
		else
		{
			return ax == EPJA_Swing1 ? m_yAngle : m_zAngle;
		}
	}

	Float SPhysicsJointLimitConePair::GetAngleRadians( Float angle ) const
	{
		return ( angle / 180.f ) * RED_PI;
	}

	physics::EPhysicsJointMotion SPhysicsJointLimitConePair::GetMotion( EPhysicsJointAxis axis ) const
	{
		if( axis == physics::EPJA_Swing1 )
		{
			if( m_swingY == EPJM_Limited )
			{
				if( m_yAngle >= 180.0f )
				{
					return EPJM_Free;
				}
				else if( m_yAngle <= 0.f )
				{
					return EPJM_Locked;
				}
			}
			return m_swingY;
		}
		else if( axis == physics::EPJA_Swing2 )
		{
			if( m_swingZ == EPJM_Limited )
			{
				if( m_zAngle >= 180.0f )
				{
					return EPJM_Free;
				}
				else if( m_zAngle <= 0.f )
				{
					return EPJM_Locked;
				}
			}
			return m_swingZ;
		}
		else
		{
			return EPJM_Locked;
		}
	}

	Bool SPhysicsJointLimitConePair::IsValid() const
	{
		Float yRad = GetAngleYRadians();
		Float zRad = GetAngleZRadians();
		return SPhysicsJointLimitBase::IsValid() &&
			std::isfinite( yRad ) && ( GetAngleYRadians() > 0 ) && ( GetAngleYRadians() <= RED_PI ) &&
			std::isfinite( zRad ) && ( GetAngleZRadians() > 0 ) && ( GetAngleZRadians() <= RED_PI );
	}

	// -- SPhysicsJointLimitConePair
	
	// -- Joint limits

	// PhysicsJoint

	PhysicsJoint::PhysicsJoint()
		: PhysicsBase( 0, EPBT_Joint, nullptr )
		, m_linearLimit( SPhysicsJointLinearLimit() )
		, m_twistLimit( SPhysicsJointAngularLimitPair() )
		, m_swingLimit( SPhysicsJointLimitConePair() )
		, m_projectionEnabled( false )
		, m_linearTolerance( 1e10f ) // default in PhysX
		, m_angularTolerance( RED_PI )
		, m_isBreakable( false )
		, m_breakingForce( 0.f )
		, m_breakingTorque( 0.f )
		, m_driveX( SPhysicsJointDrive() )
		, m_driveY( SPhysicsJointDrive() )
		, m_driveZ( SPhysicsJointDrive() )
		, m_driveSwing( SPhysicsJointDrive() )
		, m_driveTwist( SPhysicsJointDrive() )
		, m_driveSLERP( SPhysicsJointDrive() )
		, m_driveVelocity( SPhysicsJointDriveVelocity() )
		, m_drivePosition( Matrix::IDENTITY() )
	{
		m_actors[ 0 ] = 0;
		m_actors[ 1 ] = 0;
		m_localFrames[ 0 ] = Matrix::IDENTITY();
		m_localFrames[ 1 ] = Matrix::IDENTITY();
	}

	PhysicsJoint::PhysicsJoint( PhysicsBaseID id,PhysicsEditorScene* editorParent /*= nullptr */ )
		: PhysicsBase( id, EPBT_Joint, editorParent )
		, m_linearLimit( SPhysicsJointLinearLimit() )
		, m_twistLimit( SPhysicsJointAngularLimitPair() )
		, m_swingLimit( SPhysicsJointLimitConePair() )
		, m_projectionEnabled( false )
		, m_linearTolerance( 1e10f ) // default in PhysX
		, m_angularTolerance( RED_PI )
		, m_isBreakable( false )
		, m_breakingForce( 0.f )
		, m_breakingTorque( 0.f )
		, m_driveX( SPhysicsJointDrive() )
		, m_driveY( SPhysicsJointDrive() )
		, m_driveZ( SPhysicsJointDrive() )
		, m_driveSwing( SPhysicsJointDrive() )
		, m_driveTwist( SPhysicsJointDrive() )
		, m_driveSLERP( SPhysicsJointDrive() )
		, m_driveVelocity( SPhysicsJointDriveVelocity() )
		, m_drivePosition( Matrix::IDENTITY() )
	{
		m_actors[ 0 ] = 0;
		m_actors[ 1 ] = 0;
		m_localFrames[ 0 ] = Matrix::IDENTITY();
		m_localFrames[ 1 ] = Matrix::IDENTITY();
	}

	PhysicsJoint::~PhysicsJoint()
	{}

	void PhysicsJoint::OnUpdate()
	{
		PhysicsBase::OnUpdate();

		// check actors validity
		PhysicsBasePtr actorPtr = nullptr;
		PhysicsBasePtr childPtr = nullptr;

		if( auto editorScene = GetEditorScene() )
		{
			if( m_actors[ 0 ] )
			{
				actorPtr = editorScene->FindByPhysicsId( m_actors[ 0 ] );
				if( !actorPtr )
				{
					m_localFrames[ 0 ] = Matrix::IDENTITY();
					m_actors[ 0 ] = 0;
				}
			}
			if( m_actors[ 1 ] )
			{
				childPtr = editorScene->FindByPhysicsId( m_actors[ 1 ] );
				if( !childPtr )
				{
					m_localFrames[ 1 ] = Matrix::IDENTITY();
					m_actors[ 1 ] = 0;
				}
			}

			if( IsFlagSet( EPBF_SimulationRunning ) )
			{
				if( IsFlagSet( EPJF_ScheduleUpdateFromParent ) )
				{
					if( actorPtr )
					{
						Matrix l2w = actorPtr->GetLocalToWorld();
						SetLocalToWorld( m_localFrames[ 0 ] * l2w );
					}

					if( childPtr && !childPtr->IsFlagSet( EPBF_IsSelected ) )
					{
						childPtr->SetFlag( EPBF_ScheduleWakeUp, true );
					}
				}
				SetFlag( EPJF_ScheduleRegisteringAsListener, false );
				SetFlag( EPJF_ScheduleUpdateFromParent, false );
				SetFlag( EPJF_ScheduleLocalFrameUpdate, false );
				return;
			}

			if( IsFlagSet( EPJF_ScheduleRegisteringAsListener ) )
			{
				auto thisHandle = editorScene->FindByPhysicsId( GetPhysicsObjectID() );
				if( editorScene && thisHandle )
				{
					for( auto id : m_actors )
					{
						auto basePtr = editorScene->FindByPhysicsId( id );
						if( basePtr )
						{
							basePtr->RegisterListener( thisHandle );
						}
					}
				}
				SetFlag( EPJF_ScheduleRegisteringAsListener, false );
			}

			if( IsFlagSet( EPJF_ScheduleLocalFrameUpdate ) )
			{
				UpdateLocalFrameFromAnchor( EPJAI_Parent );
				UpdateLocalFrameFromAnchor( EPJAI_Child );
			}

			if( IsFlagSet( EPJF_ScheduleUpdateFromParent ) )
			{
				if( actorPtr )
				{
					Matrix l2w = actorPtr->GetLocalToWorld();
					PhysicsBase::SetLocalToWorld( m_localFrames[ 0 ] * l2w );
					SetFlag( EPJF_ScheduleLocalFrameUpdate, true );
				}
				SetFlag( EPJF_ScheduleUpdateFromParent, false );
			}

			SetFlag( EPJF_ScheduleLocalFrameUpdate, false );
		}
	}

	void PhysicsJoint::RenderDiscLimit( rend::FragmentBuilder &builder, SDiscLimitRenderParams params ) const
	{
		builder.EnableOverlay( params.m_enableOverlay );
		builder.EnableTransparency( true );
		builder.SetColor( params.m_color );
		builder.SetLocalTransform( params.m_space );
		builder.BindSelectable( GetSelectableObject() );
		builder.AddDiscLimitVisualization( params.m_angleMin, params.m_angleMax, params.m_radius, params.m_transform, m_alpha );
	}

	void PhysicsJoint::RenderAngularLimits( rend::IDebugDrawer& debugDrawer ) const
	{
		Matrix centerPoint = GetLocalToWorld();
		if( auto editorScene = GetEditorScene() )
		{
			auto childPtr = editorScene->FindByPhysicsId( m_actors[ EPJAI_Child ] );
			const auto space = centerPoint;

			rend::FragmentBuilder builder( debugDrawer );

			auto motion1 = GetMotion( EPJA_Twist );
			Bool twistLocked = motion1 == EPJM_Locked;
			if( !twistLocked )
			{
				Float angleMin = m_twistLimit.GetAngleLower();
				Float angleMax = m_twistLimit.GetAngleUpper();

				SDiscLimitRenderParams discLimitParams = { space, Matrix::IDENTITY(), Color::RED( m_alpha ), angleMin, angleMax, m_radius, true };
				RenderDiscLimit( builder, discLimitParams );
			}

			motion1 = GetMotion( EPJA_Swing1 );
			auto motion2 = GetMotion( EPJA_Swing2 );
			Float yAngle = m_swingLimit.GetAngleY();
			Float zAngle = m_swingLimit.GetAngleZ();
			if( motion1 != EPJM_Locked && motion2 != EPJM_Locked )
			{
				builder.EnableTransparency( true );
				builder.EnableOverlay( true );
				builder.SetColor( Color::CYAN( m_alpha ) );
				builder.SetLocalTransform( space );
				builder.BindSelectable( GetSelectableObject() );
				builder.AddConeLimitVisualization( yAngle, zAngle, 0.25f, Matrix::IDENTITY(), m_alpha );
			}
			else if( motion1 == EPJM_Locked )
			{
				Quaternion rot = Quaternion( Vector3( 0.f, 1.0f, 0.f ), RED_PI_HALF  );
				Matrix transform = Matrix::IDENTITY();

				SDiscLimitRenderParams discLimitParams = { space, transform.BuildFromQuaternion( rot ), Color::BLUE( m_alpha ), -zAngle, zAngle, m_radius, true };
				RenderDiscLimit( builder, discLimitParams );
			}
			else if( motion2 == EPJM_Locked )
			{
				Quaternion rot = Quaternion( Vector3( 0.f, 0.f, 1.0f ), -RED_PI_HALF );

				Matrix transform = Matrix::IDENTITY();

				SDiscLimitRenderParams discLimitParams = { space, transform.BuildFromQuaternion( rot ), Color::GREEN( m_alpha ), -yAngle, yAngle, m_radius, true };
				RenderDiscLimit( builder, discLimitParams );
			}
		}		
	}

	void PhysicsJoint::OnRender( rend::IDebugDrawer& debugDrawer, PhysicsRendererOptionsPtr options )
	{
		if( IsFlagSet( EPBF_IsVisible ) && options->m_joints )
		{
			PhysicsBase::OnRender( debugDrawer, options );
			RenderAngularLimits( debugDrawer );

			if( m_physicsRenderHelper )
			{
				if( auto editorScene = GetEditorScene() )
				{
					const auto actorPtr = editorScene->FindByPhysicsId( m_actors[ EPJAI_Parent ] );

					auto centerPoint = GetLocalToWorld().GetTranslation();

					if( actorPtr )
					{
						Matrix l2w = actorPtr->GetLocalToWorld();
						m_physicsRenderHelper->RenderLine( debugDrawer, GetSelectableObject(), Matrix::IDENTITY(), centerPoint, l2w.GetTranslation(), Color::BLACK( m_alpha ) );
					}
					auto childPtr = editorScene->FindByPhysicsId( m_actors[ EPJAI_Child ] );
					if( childPtr )
					{
						Matrix l2w = childPtr->GetLocalToWorld();
						m_physicsRenderHelper->RenderLine( debugDrawer, GetSelectableObject(), Matrix::IDENTITY(), centerPoint, l2w.GetTranslation(), Color::BLACK( m_alpha ) );
					}

					rend::FragmentBuilder builder( debugDrawer );

					builder.EnableTransparency( true );
					builder.EnableOverlay( true );
					builder.SetColor( Color::BLACK( m_alpha ) );
					builder.BindTransform( GetLocalToWorld() );
					builder.BindSelectable( GetSelectableObject() );
					builder.AddSolidBox( Vector3::ZEROS(), options->m_actorSize * 1.75f );

				}				
			}
		}
	}

	void PhysicsJoint::OnPostLoad( const PostLoadContext& context )
	{
		PhysicsBase::OnPostLoad( context );
	}

	void PhysicsJoint::OnRestore(PhysicsEditorScene* scene )
	{
		PhysicsBase::OnRestore( scene );
		SetFlag( EPJF_ScheduleRegisteringAsListener, true );

		// Safeguard for broken setups
		if( m_actors[0] == m_actors[1] && m_actors[0] )
		{
			m_actors[0] = 0;
		}
	}

	void PhysicsJoint::PrepareForRendering( PhysicsRendererOptionsPtr options )
	{
		PhysicsBase::PrepareForRendering( options );
		m_alpha = (Uint8)( options->m_jointAlpha * 255 );
		m_radius = options->m_jointRadius;
		if( IsFlagSet( EPBF_IsSelected ) )
		{
			m_alpha = math::Max( m_alpha, (Uint8)192 );
		}

		if( IsFlagSet( EPBF_RequestRenderingRebuild ) )
		{
			if( !m_physicsRenderHelper )
			{
				m_physicsRenderHelper = red::CreateUniquePtr< PhysicsShapeRenderer >();
			}
			m_physicsRenderHelper->Reset();

			PrepareLinearLimitsVisualization();

			m_physicsRenderHelper->SetLocalToWorld( GetLocalToWorld() );
			m_physicsRenderHelper->PrepareForRendering();
		}

		if( m_physicsRenderHelper )
		{
			m_physicsRenderHelper->SetColor( Color::DARK_MAGENTA( m_alpha ) );
		}
		SetFlag( EPBF_RequestRenderingRebuild, false );
	}

	Bool PhysicsJoint::PrepareLinearLimitsVisualization()
	{
		Bool result = false;
		Vector4 axes[ 3 ] = { Vector4::EX(), Vector4::EY(), Vector4::EZ() };
		auto linearLimit = m_linearLimit;
		for( Uint32 i = 0; i < 3; ++i )
		{
			auto motion = GetMotion( (EPhysicsJointAxis)i );

			if( motion == EPJM_Limited )
			{
				Vector4 end = axes[ i ] * linearLimit.m_value;
				Vector4 start = -end;
				m_physicsRenderHelper->AddLine( start, end );

				Float boxSize = 0.1f;
				OrientedBox box = OrientedBox( end - ( Vector4( boxSize, boxSize, boxSize ) / 2.0f ), Vector4::EX() * boxSize, Vector4::EY() * boxSize, Vector4::EZ() * boxSize );
				box.RenderWireframe( *m_physicsRenderHelper );

				box = OrientedBox( start - ( Vector4( boxSize, boxSize, boxSize ) / 2.0f ), Vector4::EX() * boxSize, Vector4::EY() * boxSize, Vector4::EZ() * boxSize );
				box.RenderWireframe( *m_physicsRenderHelper );

				result = true;
			}
			else if( motion == EPJM_Free )
			{
				Vector4 start = -axes[ i ];
				Vector4 end = axes[ i ];

				m_physicsRenderHelper->AllocateLines( 5 );

				m_physicsRenderHelper->AddLine( start, end );

				Vector4 perp = axes[ ( i + 1 ) % 3 ] * 0.1f;
				m_physicsRenderHelper->AddLine( ( end * 0.9f ) + perp, end );
				m_physicsRenderHelper->AddLine( ( end * 0.9f ) - perp, end );
				m_physicsRenderHelper->AddLine( ( start * 0.9f ) + perp, start );
				m_physicsRenderHelper->AddLine( ( start * 0.9f ) - perp, start );

				result = true;
			}
		}
		return result;
	}

	void PhysicsJoint::SetLocalToWorld( const Matrix& localToWorld )
	{
		PhysicsBase::SetLocalToWorld( localToWorld );
		SetFlag( EPJF_ScheduleLocalFrameUpdate, true );
		SetFlag( EPJF_ScheduleUpdateFromParent, true );
	}

	physics::EPhysicsJointMotion PhysicsJoint::GetMotion( EPhysicsJointAxis axis ) const
	{
		switch( axis )
		{
			case physics::EPJA_X:
			case physics::EPJA_Y:
			case physics::EPJA_Z:
				return m_linearLimit.GetMotion( axis );
			case physics::EPJA_Twist:
				return m_twistLimit.GetMotion( axis );
			case physics::EPJA_Swing1:
			case physics::EPJA_Swing2:
				return m_swingLimit.GetMotion( axis );
			case physics::EPJA_Count:
			default:
				return EPJM_Locked;
		}
	}

	void PhysicsJoint::SetMotion( EPhysicsJointAxis axis, EPhysicsJointMotion motion )
	{
		switch( axis )
		{
			case physics::EPJA_X:
				m_linearLimit.m_x = motion;
				break;
			case physics::EPJA_Y:
				m_linearLimit.m_y = motion;
				break;
			case physics::EPJA_Z:
				m_linearLimit.m_z = motion;
				break;
			case physics::EPJA_Twist:
				m_twistLimit.m_twist = motion;
				break;
			case physics::EPJA_Swing1:
				m_swingLimit.m_swingY = motion;
				break;
			case physics::EPJA_Swing2:
				m_swingLimit.m_swingZ = motion;
				break;
			case physics::EPJA_Count:
			default:
				break;
		}
	}

	const Matrix& PhysicsJoint::GetLocalFrame( EPhysicsJointActorIndex actorInd ) const
	{
		if( actorInd != EPJAI_Child && actorInd != EPJAI_Parent )
			return Matrix::IDENTITY_CONSTANT;
		return m_localFrames[ actorInd ];
	}

	void PhysicsJoint::SetLocalFrame( Matrix mat, EPhysicsJointActorIndex actorInd )
	{
		if( actorInd != EPJAI_Child && actorInd != EPJAI_Parent )
			return;
		if( actorInd )
		{
			m_localFrames[ actorInd ].SetTranslation( mat.GetTranslation() );
		}
		else
		{
			m_localFrames[ actorInd ] = mat;
		}
	}

	PhysicsBaseID PhysicsJoint::GetActorId( EPhysicsJointActorIndex actorInd ) const
	{
		return m_actors[ actorInd ];
	}

	void PhysicsJoint::SetActorId( PhysicsBaseID id, EPhysicsJointActorIndex actorInd )
	{
		if( auto editorScene = GetEditorScene() )
		{
			auto thisHandle = editorScene->FindByPhysicsId( GetPhysicsObjectID() );
			if( thisHandle )
			{
				if( m_actors[ actorInd ] > 0 )
				{
					auto basePtr = editorScene->FindByPhysicsId( m_actors[ actorInd ] );
					if( basePtr )
					{
						basePtr->UnregisterListener( thisHandle );
					}
				}
				m_actors[ actorInd ] = id;
				
				auto basePtr = editorScene->FindByPhysicsId( m_actors[ actorInd ] );
				if( basePtr )
				{
					basePtr->RegisterListener( thisHandle );
				}
			}
			else
			{
				m_actors[ actorInd ] = id;
				SetFlag( EPJF_ScheduleRegisteringAsListener, true );
			}
		}
		else
		{
			SetFlag( EPJF_ScheduleRegisteringAsListener, true );
			m_actors[ actorInd ] = id;
			return;
		}		
	}

	void PhysicsJoint::OnNotifyTransformChanged( PhysicsBaseID id )
	{
		if( id == m_actors[ 0 ] )
		{
			SetFlag( EPJF_ScheduleUpdateFromParent, true );
		}
		else
		{
			SetFlag( EPJF_ScheduleLocalFrameUpdate, true );
		}
	}

	const physics::SPhysicsJointDrive& PhysicsJoint::GetDrive( EPhysicsJointDrive drive ) const
	{
		switch( drive )
		{
			case physics::EPJD_X:
				return m_driveX;
			case physics::EPJD_Y:
				return m_driveY;
			case physics::EPJD_Z:
				return m_driveZ;
			case physics::EPJD_Swing:
				return m_driveSwing;
			case physics::EPJD_Twist:
				return m_driveTwist;
			case physics::EPJD_SLERP:
				return m_driveSLERP;
			case physics::EPJD_Count:
				return m_driveX;
			default:
				return m_driveX;
		}
	}

	void PhysicsJoint::SetDrive( EPhysicsJointDrive d, SPhysicsJointDrive drive )
	{
		switch( d )
		{
			case physics::EPJD_X:
				m_driveX = drive;
			case physics::EPJD_Y:
				m_driveY = drive;
			case physics::EPJD_Z:
				m_driveZ = drive;
			case physics::EPJD_Swing:
				m_driveSwing = drive;
			case physics::EPJD_Twist:
				m_driveTwist = drive;
			case physics::EPJD_SLERP:
				m_driveSLERP = drive;
			case physics::EPJD_Count:
				m_driveX = drive;
			default:
				break;
		}
	}

	Matrix PhysicsJoint::CalculateLocalFrame( Matrix worldPos, Matrix parentLocalToWorld )
	{
		return worldPos * parentLocalToWorld.OrthonormInverted();
	}

	void PhysicsJoint::UpdateLocalFrameFromAnchor( EPhysicsJointActorIndex actorInd )
	{
		if( auto editorScene = GetEditorScene() )
		{
			auto basePtr = editorScene->FindByPhysicsId( m_actors[ actorInd ] );
			if( basePtr )
			{
				m_localFrames[ actorInd ] = CalculateLocalFrame( GetLocalToWorld(), basePtr->GetLocalToWorld() );
			}
		}	
	}

	String PhysicsJoint::GetName() const
	{
		return String::Printf( "Joint (ID: %d )", GetPhysicsObjectID() );
	}

	Bool PhysicsJoint::IsValid() const
	{
		// if we aren't attached to scene, the joint is invalid
		auto editorScene = GetEditorScene();
		if( editorScene == nullptr )
			return false;

		const auto& actor0 = red::StaticCast< PhysicsActor >( editorScene->FindByPhysicsId( m_actors[ 0 ] ) );
		const auto& actor1 = red::StaticCast< PhysicsActor >( editorScene->FindByPhysicsId( m_actors[ 1 ] ) );

		// joint without actors is invalid
		if( !actor0 && !actor1 )
			return false;

		// at least one actor needs to be dynamic
		if( actor0->GetSimulationType() != EPS_Dynamic && actor1->GetSimulationType() != EPS_Dynamic )
			return false;

		return true;
	}

	void PhysicsJoint::OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value )
	{
		PhysicsBase::OnPropertyPostChange( path, oldValue, value );

		// Find the property name
		CName propName;
		rtti::PathParser parser( path );
		parser.EatName( propName );
		SetFlag( EPBF_WasModified, true );
		red::String	 pathToNotify = propName.AsChar();
		parser.EatName( propName );

		if( propName == RED_NAME_CONSTEXPR_NOREG( "twistLimit" ) )
		{
			pathToNotify += ".";
			pathToNotify += propName.AsChar();
			parser.EatName( propName );

			if( propName == RED_NAME_CONSTEXPR_NOREG( "upper" ) )
			{
				pathToNotify += ".lower";

				if( m_twistLimit.m_upper < m_twistLimit.m_lower )
				{
					m_twistLimit.m_lower = m_twistLimit.m_upper;
					ISerializable::NotifyPropertyChanged( rtti::AccessPath( pathToNotify.AsChar() ) );
				}
			}
			else if( propName == RED_NAME_CONSTEXPR_NOREG( "lower" ) )
			{
				pathToNotify += ".upper";

				if( m_twistLimit.m_upper < m_twistLimit.m_lower )
				{
					m_twistLimit.m_upper = m_twistLimit.m_lower;
					ISerializable::NotifyPropertyChanged( rtti::AccessPath( pathToNotify.AsChar() ) );
				}
			}
		}
		else if( propName == RED_NAME_CONSTEXPR_NOREG( "actors" ) )
		{
			if( m_actors[0] == m_actors[1] && m_actors[0] )
			{
				m_actors[0] = 0;
			}
		}
	}

	void PhysicsJoint::SetupInterop( PhysicsJoint* ptr, ::interop::utils::Object& op )
	{}

	// --PhysicsJoint
}
