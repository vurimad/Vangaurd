/*
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red
{

enum class Platform : Uint8
{
	Orbis,
	Durango,
	Steam,
	GOG,
	Epic,
	Stadia,
	Null
};

enum class AppType : Uint8
{
	Tools,
	Game
};

}