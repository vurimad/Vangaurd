/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once


enum
{
#ifndef RED_PLATFORM_CONSOLE
	ENVPROBE_AMBIENT_CUBE_RESOLUTION		= 16,
	ENVPROBE_REFLECTION_CUBE_RESOLUTION		= 128,
	ENVPROBE_AMBIENT_CUBE_NUM_MIPS			= 1,
	ENVPROBE_REFLECTION_CUBE_NUM_MIPS		= 8,
#else
	ENVPROBE_AMBIENT_CUBE_RESOLUTION		= 16,
	ENVPROBE_REFLECTION_CUBE_RESOLUTION		= 16,
	ENVPROBE_AMBIENT_CUBE_NUM_MIPS			= 1,
	ENVPROBE_REFLECTION_CUBE_NUM_MIPS		= 5,
#endif
	ENVPROBE_MAX_CUBE_RESOLUTION			= ENVPROBE_AMBIENT_CUBE_RESOLUTION > ENVPROBE_REFLECTION_CUBE_RESOLUTION ? ENVPROBE_AMBIENT_CUBE_RESOLUTION : ENVPROBE_REFLECTION_CUBE_RESOLUTION,

};

/// Env probe buffer type
enum eEnvProbeBufferTexture
{
	ENVPROBEBUFFERTEX_Albedo,
	ENVPROBEBUFFERTEX_Normals,
	ENVPROBEBUFFERTEX_GlobalShadow,
	ENVPROBEBUFFERTEX_Depth,

	ENVPROBEBUFFERTEX_MAX
};

RED_INLINE Uint32 GetEnvProbeDataSourceResolution()
{
	return Max( ENVPROBE_AMBIENT_CUBE_RESOLUTION, ENVPROBE_REFLECTION_CUBE_RESOLUTION );
}

RED_INLINE Uint32 GetEnvProbeDataSourceSize()
{
	const Uint32 resolution = GetEnvProbeDataSourceResolution();
	return 6 * (2 + 3 + 4 + 2) * resolution * resolution;
}

RED_INLINE Uint32 GetEnvProbeDataSourceFaceSize()
{
	const Uint32 dataSourceSize = GetEnvProbeDataSourceSize();
	RED_ASSERT( 0 == dataSourceSize % 6 );
	return dataSourceSize / 6;
}

RED_INLINE Uint32 GetEnvProbeDataSourceBufferOffset( eEnvProbeBufferTexture buffType )
{
	const Uint32 resolution = GetEnvProbeDataSourceResolution();
	const Uint32 fullBuffSize = 4 * resolution * resolution;

	Uint32 off = 0;
	if ( buffType > ENVPROBEBUFFERTEX_Albedo )
		off += fullBuffSize / 2;
	if ( buffType > ENVPROBEBUFFERTEX_Normals )
		off += fullBuffSize * 3 / 4;
	if ( buffType > ENVPROBEBUFFERTEX_GlobalShadow )
		off += fullBuffSize;
	if ( buffType > ENVPROBEBUFFERTEX_Depth )
		off += fullBuffSize / 2;

	return off;
}

RED_INLINE Uint32 GetEnvProbeDataSourceBufferOffset( Uint32 faceIndex, eEnvProbeBufferTexture buffType )
{
	return faceIndex * GetEnvProbeDataSourceFaceSize() + GetEnvProbeDataSourceBufferOffset( buffType );
}

