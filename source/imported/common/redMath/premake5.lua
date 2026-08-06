---
-- Copyright (C)2017 CD Projekt Red. All Rights Reserved.
---

-- RED Vanguard integration for the original RED redMath module.

local redMath_files = {
	["canonical/vector4"] = {
		"include/vector4.h",
		"include/vector4.hpp",
		"src/vector4.cpp" },
	["canonical/vector3"] = {
		"include/vector3.h",
		"src/vector3.cpp",
		"include/vector3.hpp" },
	["canonical/vector2"] = {
		"include/vector2.h",
		"include/vector2.hpp" },
	["canonical/plane"] = {
		"include/plane.h",
		"include/plane.hpp" },
	["canonical/box"] = {
		"include/box.h",
		"include/box.hpp",
		"src/box.cpp" },
	["canonical/segment"] = {
		"include/segment.h",
		"include/segment.hpp",
		"src/segment.cpp" },
	["canonical/matrix"] = {
		"include/matrix.h",
		"include/matrix.hpp",
		"src/matrix.cpp" },
	["canonical/eulerAngles"] = {
		"include/eulerAngles.h",
		"include/eulerAngles.hpp",
		"src/eulerAngles.cpp" },
	["canonical/sphere"] = {
		"include/sphere.h",
		"include/sphere.hpp",
		"src/sphere.cpp" },
	["canonical/xform"] = {
		"include/transform.h",
		"include/transform.hpp"
	},
	["generic/color"] = {
		"include/color.h",
		"include/color.hpp",
		"include/colorUtils.h",
		"src/color.cpp",
		"src/colorUtils.cpp"
	},
	["generic/rectf"] = {
		"include/rectf.h",
		"include/rectf.hpp" },
	["generic/rect"] = {
		"include/rect.h",
		"include/rect.hpp",
		"include/point.h",
		"include/point.hpp",
		"include/point3D.h",
		"include/point3D.hpp"},
	["shapes/quad"] = {
		"include/quad.h",
		"include/quad.hpp",
		"src/quad.cpp" },
	["shapes/fixedCapsule"] = {
		"include/fixedCapsule.h",
		"include/fixedCapsule.hpp",
		"src/fixedCapsule.cpp" },
	["shapes/orientedBox"] = {
		"include/orientedBox.h",
		"include/orientedBox.hpp",
		"src/orientedBox.cpp" },
	["shapes/tetrahedron"] = {
		"include/tetrahedron.h",
		"include/tetrahedron.hpp",
		"src/tetrahedron.cpp" },
	["shapes/cylinder"] = {
		"include/cylinder.h",
		"include/cylinder.hpp",
		"src/cylinder.cpp" },
	["shapes/cutCone"] = {
		"include/cutCone.h",
		"include/cutCone.hpp",
		"src/cutCone.cpp" },
	["highPrecision"] = {
		"include/matrixDouble.h",
		"include/fixedPoint.h",
		"include/fixedPoint.inl",
		"include/worldPosition.h",
		"include/worldPosition.inl",
		"include/worldTransform.h",
		"include/worldTransform.inl",
		"src/matrixDouble.cpp" },
	["utils"] = {
		"include/mathUtils.h",
		"include/half.h",
		"include/float16compressor.h",
		"include/numericalUtils.h",
		"include/fpuFunctions.h",
		"src/mathUtils.cpp" },
	[""] = {
		"include/redMathApi.h",
		"include/redMathPublic.h",
		"src/build.h",
		"src/build.cpp",
		"src/redMathInternal.h",
		"src/red_math.natvis" },
	["simd/transform"] = {
		"include/simdQSTransform.h",
		"include/simdQSTransform.hpp"},
	["simd/vector"] = {
		"include/simdVector4.h",
		"include/simdVectorArithmetic.h",
		"include/simdVectorFunctions.h",
		"include/simdVector4.hpp",
		"include/simdVectorArithmetic.hpp",
		"include/simdVectorFunctions.hpp",
		"src/simdVector4.cpp" },
	["simd/scalar"] = {
		"include/simdScalar.h",
		"include/simdScalar.hpp" },
	["simd"] = {
		"include/simdQuad.h",
		"include/simdSoAHelper.h"},
	["random"] = {
		"include/random.h",
		"include/perlinNoise.h",
		"src/random.cpp",
		"src/perlinNoise.cpp",
	},
	["simd/comparison"] = {
		"include/simdComparisonResult.h",
		"include/simdComparisonResult.hpp"
	},
	["simd/matrix"] = {
		"include/simdMatrix.h"
	},
	["simd/box"] = {
		"include/simdBox.h"
	},
	["canonical/quaternion"] = {
		"include/quaternion.h",
		"include/quaternion.hpp" },
	["interpolation"] = {
		"include/interpolation.h",
		"src/interpolation.cpp"},
	}

local function flatten_file_groups(groups)
	local result = {}
	for _, group in pairs(groups) do
		for _, file in ipairs(group) do
			table.insert(result, file)
		end
	end
	return result
end

project "redMath"
	kind "StaticLib"
	language "C++"
	cppdialect "C++17"
	exceptionhandling "Off"
	location "../../../../build/%{_ACTION}/projects/redMath"

	pchheader "build.h"
	pchsource "src/build.cpp"

	includedirs
	{
		"./",
		"src",
		"include",
		"../redSystem/include"
	}

	defines
	{
		"RED_MODULE_redMath",
		"RED_EXPORT_redMath"
	}

	links
	{
		"redSystem"
	}

	dependson
	{
		"redSystem"
	}

	vpaths(redMath_files)

	files(flatten_file_groups(redMath_files))
