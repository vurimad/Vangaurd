#pragma once

//////////////////////////////////////////////////////////////////////////
// enums
namespace red
{
	enum ProfilerToolSlot
	{
		PTS_REDPROFILER,
		PTS_LOG,			// todo
		PTS_FILE,			// todo
		PTS_RAZOR,
		PTS_PIX,
		PTS_VTUNE,
		PTS_NVIDIA,
		PTS_TRACY,
		PTS_MAX
	};

	enum ProfilerExtensionSlot
	{
		PES_IO,
		PES_MEMORY,
		PES_MAX
	};

	enum ProfilerFrameType
	{
		PFT_UNKNOWN,
		PFT_ENGINE,
		PFT_EDITOR,
		PFT_GAMEPLAY,
		PFT_GRAPHICS,
		PFT_PHYSICS,
		PFT_NETWORK,
		PFT_MAX
	};
}