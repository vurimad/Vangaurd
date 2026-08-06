/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

#include "../../../common/redMath/include/redMathPublic.h"

RED_DISABLE_WARNING_MSC( 4201 ) // nonstandard extension used : nameless struct/union

/************************************************************************/
/* Forward declarations                                                 */
/************************************************************************/
struct Vector4;
struct Vector2;
struct Vector3;
struct Quaternion;
struct Matrix;
struct EulerAngles;
struct Color;
struct Box;
struct Rect;
struct RectF;
struct ConvexHull;
struct OrientedBox;
struct Tetrahedron;
struct FixedCapsule;
struct CutCone;
struct Cylinder;
struct Transform;
struct AbsolutePathSerializable;

/********************************/
/* Canonical math wrappers     */
/********************************/
#include "mathVector4.h"
#include "mathVector3.h"
#include "mathVector2.h"
#include "mathPlane.h"
#include "mathBox.h"
#include "mathSegment.h"
#include "mathMatrix.h"
#include "mathEulerAngles.h"
#include "mathSphere.h"
#include "mathColor.h"
#include "mathQuaternion.h"
#include "mathQSTransform.h"
#include "mathTransform.h"
#include "mathWorldPosition.h"
#include "mathWorldTransform.h"

/********************************/
/* Other generic math wrappers  */
/********************************/
#include "mathRectF.h"
#include "mathPoint.h"
#include "mathPoint3D.h"
#include "mathRect.h"

/********************************/
/* Shape wrappers               */
/********************************/
#include "mathQuad.h"
#include "mathFixedCapsule.h"
#include "mathOrientedBox.h"
#include "mathConvexHull.h"
#include "mathConvexHullEx.h"
#include "mathTetrahedron.h"
#include "mathCylinder.h"
#include "mathCutCone.h"
#include "absoluteFilepath.h"

extern RED_REFLECTION_API void Compute2DConvexHull( red::DynArray<Vector4>& points );
extern RED_REFLECTION_API Bool IsPointInsideConvexShape( const Vector4 & point, const red::DynArray< Vector4 > & shape );

void RegisterMathTypeAliases();
