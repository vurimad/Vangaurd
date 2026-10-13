/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../../common/redReflection/include/rttiClassBuilder.h"
#include "../../../common/redReflection/include/handle.h"
#include "../../../common/interop/include/interopUtilsManual.h"

namespace physics
{
	class PhysicsBase;
	class PhysicsEditorScene;
}

namespace tools
{
	struct ObjectCreationInfo;

	/// Helper class that helps to construct physics object (like actor or shape)
	class IPhysicsObjectConstructor
	{
		RTTI_DECLARE_TYPE( IPhysicsObjectConstructor );

	public:
		virtual ~IPhysicsObjectConstructor() {};

		/// Can we create object from the source information
		virtual const Bool CanCreateObject( const ObjectCreationInfo& info ) const = 0;

		/// Create the object
		virtual const Bool CreateObject( const ObjectCreationInfo& info, red::DynArray< THandle< physics::PhysicsBase > >& physicsObjects, physics::PhysicsEditorScene* editorScene, const red::DynArray< tools::EditorObjectID >& selectedIds ) const = 0;
	};

} // tools
