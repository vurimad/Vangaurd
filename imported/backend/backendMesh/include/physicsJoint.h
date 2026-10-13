/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "physicsBase.h"

namespace physics
{
	enum EPhysicsJointAxis : Uint8
	{
		EPJA_X			= 0,
		EPJA_Y			= 1,
		EPJA_Z			= 2,
		EPJA_Twist		= 3,
		EPJA_Swing1		= 4,
		EPJA_Swing2		= 5,
		EPJA_Count		
	};

	enum EPhysicsJointMotion : Uint8
	{
		EPJM_Locked,
		EPJM_Limited,
		EPJM_Free,
		EPJM_Max
	};

	enum EPhysicsJointFlag : Uint32
	{
		EPJF_ScheduleLocalFrameUpdate		= EPBF_Reserved_1,
		EPJF_ScheduleUpdateFromParent		= EPBF_Reserved_2,
		EPJF_ScheduleRegisteringAsListener	= EPBF_Reserved_3,
	};

	enum EPhysicsJointDrive : Uint8
	{
		EPJD_X			= 0,
		EPJD_Y			= 1,
		EPJD_Z			= 2,
		EPJD_Swing		= 3,
		EPJD_Twist		= 4,
		EPJD_SLERP		= 5,
		EPJD_Count		
	};

	enum EPhysicsJointActorIndex : Uint8
	{
		EPJAI_Parent		= 0,
		EPJAI_Child			= 1,
		EPJAI_Count			
	};


	// Joint drive

	struct BACKEND_MESH_API SPhysicsJointDrive
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_TYPE( SPhysicsJointDrive );

		Float						m_forceLimit;			//	Force limit of a drive 
		Float						m_stiffness;			//	Force proportional to the position error
		Float						m_damping;				//	Force proportional to the velocity error
		Bool						m_isAcceleration;		//	drive spring is for the acceleration at the joint (rather than the force) 

		SPhysicsJointDrive();
		SPhysicsJointDrive( Float driveStiffness, Float driveDamping, Float driveForceLimit, Bool isAcceleration = false );


		Bool						IsValid() const;

		Bool						IsActive() const;
	};

	struct BACKEND_MESH_API SPhysicsJointDriveVelocity
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_TYPE( SPhysicsJointDriveVelocity );

		Vector4						m_linearVelocity;		// The goal velocity for linear drive
		Vector4						m_angularVelocity;		// The goal velocity for angular drive

		SPhysicsJointDriveVelocity();

		SPhysicsJointDriveVelocity( Vector4 linearVelocity, Vector4 angularVelocity );
	};

	//

	// Joint limit base description
	struct BACKEND_MESH_API SPhysicsJointLimitBase
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_POLYMORPHIC_TYPE( SPhysicsJointLimitBase );

		Float					m_restitution;		// Determines how much will a joint bounce when hitting the limit [0, 1] 
		Float					m_bounceThreshold;	// Determines minimum impact velocity required for the joint to bounce 
		Float					m_stiffness;		// Higher than zero means a soft limit [0, RED_FLT_MAX)
		Float					m_damping;			// If spring is greater than 0, this determines its damping [0, RED_FLT_MAX) 
		Float					m_contactDistance;	// Determines when a limit is considered active by solver. Too small might cause jitter. 

		SPhysicsJointLimitBase();

		Bool					IsValid() const;

		Bool					IsSoft() const;
	};

	// One sided linear limit description
	struct BACKEND_MESH_API SPhysicsJointLinearLimit : public SPhysicsJointLimitBase
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_TYPE( SPhysicsJointLinearLimit );

		Float					m_value; // Extent of the limit (0, RED_FLT_MAX) 
		EPhysicsJointMotion		m_x;
		EPhysicsJointMotion		m_y;
		EPhysicsJointMotion		m_z;

		SPhysicsJointLinearLimit();

		SPhysicsJointLinearLimit( SPhysicsTolerancesScale scale, Float extent, Float contactDist = -1.f );

		SPhysicsJointLinearLimit( Float extent, Float stiffness, Float damping );

		Bool					IsValid() const;
		EPhysicsJointMotion		GetMotion( EPhysicsJointAxis axis ) const;
	};

	// The twist limit controls the range of motion around the twist axis.
	struct BACKEND_MESH_API SPhysicsJointAngularLimitPair : public SPhysicsJointLimitBase
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_TYPE( SPhysicsJointAngularLimitPair );

		Float					m_upper; // Use Getter if you want angle with regard to motion type. Upper limit. Must be bigger than lower limit. (-180, 180)
		Float					m_lower; // Use Getter if you want angle with regard to motion type. Lower limit. Must be lower than upper limit. (-180, 180)
		EPhysicsJointMotion		m_twist;

		SPhysicsJointAngularLimitPair();

		SPhysicsJointAngularLimitPair( Float lower, Float upper, Float contactDist = -1.f );

		SPhysicsJointAngularLimitPair( Float lower, Float upper, Float stiffness, Float damping );

		Float					GetAngleUpper() const;

		Float					GetAngleLower() const;

		Float					GetAngleUpperRadians() const;

		Float					GetAngleLowerRadians() const;

		EPhysicsJointMotion		GetMotion( EPhysicsJointAxis axis ) const;

		Bool					IsValid() const;
	};

	// Swing limit
	// The cone limit is used if either or both swing axes are limited. The extents are 
	// symmetrical and measured in the frame of the parent.If only one swing degree of freedom
	// is limited, the corresponding value from the cone limit defines the limit range.
	struct BACKEND_MESH_API SPhysicsJointLimitConePair : public SPhysicsJointLimitBase
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_TYPE( SPhysicsJointLimitConePair );

		Float					m_yAngle; // Use Getter if you want angle with regard to motion type. The maximum angle from the Y axis of the constraint frame. (0, 180)
		Float					m_zAngle; // Use Getter if you want angle with regard to motion type. The maximum angle from the Z axis of the constraint frame. (0, 180)
		EPhysicsJointMotion		m_swingY;
		EPhysicsJointMotion		m_swingZ;

		SPhysicsJointLimitConePair();

		SPhysicsJointLimitConePair( Float yAngle, Float zAngle, Float contactDist = -1.f );

		SPhysicsJointLimitConePair( Float yAngle, Float zAngle, Float stiffness, Float damping );

		Float					GetAngleY() const;

		Float					GetAngleZ() const;

		Float					GetAngleYRadians() const;

		Float					GetAngleZRadians() const;

		EPhysicsJointMotion		GetMotion( EPhysicsJointAxis axis ) const;

		Bool					IsValid() const;

	private:
		Float					GetAngle( EPhysicsJointAxis ax ) const;
		Float					GetAngleRadians( Float angle ) const;
	};

	class BACKEND_MESH_API PhysicsJoint : public PhysicsBase
	{
		RTTI_DECLARE_TYPE( PhysicsJoint );
		DECLARE_INTEROP_DERIVED( PhysicsJoint, PhysicsBase );

	public:
		PhysicsJoint();
		PhysicsJoint( PhysicsBaseID id, PhysicsEditorScene* editorParent = nullptr );

		virtual ~PhysicsJoint();

		virtual void										OnUpdate() override;
		virtual void										OnRender( rend::IDebugDrawer& debugDrawer, PhysicsRendererOptionsPtr options ) override;
		virtual void OnPostLoad( const PostLoadContext& context ) override;
		virtual void										OnRestore( PhysicsEditorScene* scene ) override;

		virtual void										PrepareForRendering( PhysicsRendererOptionsPtr options ) override;

		virtual void										SetLocalToWorld( const Matrix& localToWorld ) override;

		EPhysicsJointMotion									GetMotion( EPhysicsJointAxis axis ) const;
		void												SetMotion( EPhysicsJointAxis axis, EPhysicsJointMotion motion );

		const Matrix&										GetLocalFrame( EPhysicsJointActorIndex actorInd = EPJAI_Parent ) const;
		void												SetLocalFrame( Matrix mat, EPhysicsJointActorIndex actorInd = EPJAI_Parent );

		PhysicsBaseID										GetActorId( EPhysicsJointActorIndex actorInd = EPJAI_Parent ) const;
		void												SetActorId( PhysicsBaseID id, EPhysicsJointActorIndex actorInd = EPJAI_Parent );

		virtual void										OnNotifyTransformChanged( PhysicsBaseID id ) override;


		RED_INLINE const SPhysicsJointLinearLimit&			GetLinearLimit() const
		{
			return m_linearLimit;
		}

		RED_INLINE const SPhysicsJointAngularLimitPair&		GetTwistLimit() const
		{
			return m_twistLimit;
		}

		RED_INLINE const SPhysicsJointLimitConePair&		GetSwingLimit() const
		{
			return m_swingLimit;
		}

		const SPhysicsJointDrive&							GetDrive( EPhysicsJointDrive drive ) const;

		RED_INLINE const Matrix&							GetDrivePosition() const
		{
			return m_drivePosition;
		}

		RED_INLINE const Vector4&							GetDriveLinearVelocity() const
		{
			return m_driveVelocity.m_linearVelocity;
		}

		RED_INLINE const Vector4&							GetDriveAngularVelocity() const
		{
			return m_driveVelocity.m_angularVelocity;
		}

		RED_INLINE Bool										IsProjectionToleranceEnabled() const
		{
			return m_projectionEnabled;
		}

		RED_INLINE Float									GetProjectionLinearTolerance() const
		{
			return m_linearTolerance;
		}

		RED_INLINE Float									GetProjectionAngularTolerance() const
		{
			return m_angularTolerance;
		}

		RED_INLINE Bool										IsBreakable() const
		{
			return m_isBreakable;
		}

		RED_INLINE Float									GetBreakingForce() const
		{
			return m_breakingForce;
		}

		RED_INLINE Float									GetBreakingTorque() const
		{
			return m_breakingForce;
		}

		RED_INLINE void										SetLinearLimit( SPhysicsJointLinearLimit limit )
		{
			m_linearLimit = limit;
		}

		RED_INLINE void										SetTwistLimit( SPhysicsJointAngularLimitPair limit )
		{
			m_twistLimit = limit;
		}

		RED_INLINE void										SetSwingLimit( SPhysicsJointLimitConePair limit )
		{
			m_swingLimit = limit;
		}

		void												SetDrive( EPhysicsJointDrive d, SPhysicsJointDrive drive );

		RED_INLINE void										SetDrivePosition( Matrix positon )
		{
			m_drivePosition = positon;
		}

		RED_INLINE void										SetDriveLinearVelocity( Vector4 velocity )
		{
			m_driveVelocity.m_linearVelocity = velocity;
		}

		RED_INLINE void										SetDriveAngularVelocity( Vector4 velocity )
		{
			m_driveVelocity.m_angularVelocity = velocity;
		}

		RED_INLINE void										SetProjectionToleranceEnabled( Bool value )
		{
			m_projectionEnabled = value;
		}

		RED_INLINE void										SetProjectionLinearTolerance( Float value )
		{
			m_linearTolerance = value;
		}

		RED_INLINE void										SetProjectionAngularTolerance( Float value )
		{
			m_angularTolerance = value;
		}

		RED_INLINE void										SetIsBreakable( Bool value )
		{
			m_isBreakable = value;
		}

		RED_INLINE void										SetBreakingForce( Float value )
		{
			m_breakingForce = value;
		}

		RED_INLINE void										SetBreakingTorque( Float value )
		{
			m_breakingTorque = value;
		}

		virtual String										GetName() const override final;
		Bool												IsValid() const;

		void												OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value ) override;

	private:
		struct SDiscLimitRenderParams
		{
			const Matrix&	m_space;
			const Matrix&	m_transform;
			Color			m_color;
			Float			m_angleMin;
			Float			m_angleMax;
			Float			m_radius;
			Bool			m_enableOverlay;
		};

		Matrix												CalculateLocalFrame( Matrix worldPos, Matrix parentLocalToWorld );
		void												UpdateLocalFrameFromAnchor( EPhysicsJointActorIndex actorInd );

		Bool												PrepareLinearLimitsVisualization();

		void												RenderDiscLimit( rend::FragmentBuilder &builder, SDiscLimitRenderParams params ) const;
		void												RenderAngularLimits( rend::IDebugDrawer& debugDrawer ) const;
	private:
		SPhysicsJointLinearLimit							m_linearLimit;
		SPhysicsJointAngularLimitPair						m_twistLimit;
		SPhysicsJointLimitConePair							m_swingLimit;

		SPhysicsJointDrive									m_driveX;
		SPhysicsJointDrive									m_driveY;
		SPhysicsJointDrive									m_driveZ;
		SPhysicsJointDrive									m_driveSwing;
		SPhysicsJointDrive									m_driveTwist;
		SPhysicsJointDrive									m_driveSLERP;
		SPhysicsJointDriveVelocity							m_driveVelocity;
		Matrix												m_drivePosition;			// The goal drive pose if positional drive is in use		

		Matrix												m_localFrames[ EPJAI_Count ];
		PhysicsBaseID										m_actors[ EPJAI_Count ];

		Float												m_breakingForce;
		Float												m_breakingTorque;
		Float												m_linearTolerance;
		Float												m_angularTolerance;
		Float												m_radius;

		Bool												m_projectionEnabled;
		Bool												m_isBreakable;
	};

	using PhysicsJointPtr = THandle< PhysicsJoint >;
}

RTTI_DECLARE_ENUM_IN_NAMESPACE( EPhysicsJointAxis, physics );
RTTI_DECLARE_ENUM_IN_NAMESPACE( EPhysicsJointMotion, physics );
RTTI_DECLARE_ENUM_IN_NAMESPACE( EPhysicsJointDrive, physics );

