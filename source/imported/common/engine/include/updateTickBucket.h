/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/


#pragma once

namespace UpdateBucket
{
	enum Enum : Uint8
	{
		Vehicle = 0,
		Character,
		AttachedObject,
		Count,
	};

	constexpr RED_INLINE Uint8 NoneMask() { return 0; }
	constexpr RED_INLINE Uint8 VehicleMask() { return RED_FLAG( Vehicle ); }
	constexpr RED_INLINE Uint8 CharacterMask() { return RED_FLAG( Character ); }
	constexpr RED_INLINE Uint8 AttachedObjectMask() { return RED_FLAG( AttachedObject ); }
	constexpr RED_INLINE Uint8 AllMask() { return VehicleMask() | CharacterMask() | AttachedObjectMask(); }

	static constexpr AnsiChar const* name[Count] =
	{
		"VehicleBucket",
		"CharacterBucket",
		"AttachedObjectBucket",
	};
}//

namespace EntityPhase
{
	enum Enum : Uint8
	{
		Pre = 0,
		Post,
		Count,
	};

	constexpr RED_INLINE Uint8 PreMask()	{ return RED_FLAG( Pre ); }
	constexpr RED_INLINE Uint8 PostMask()	{ return RED_FLAG( Post ); }

	static constexpr AnsiChar const* name[Count] =
	{
		"PreTick",
		"PostTick",
	};
}//
