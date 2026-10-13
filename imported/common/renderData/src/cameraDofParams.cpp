#include "build.h"
#include "cameraDofParams.h"

RTTI_BEGIN_TYPE( SBokehDofParams )
	RTTI_PROPERTY( m_enabled ).editable();
	RTTI_PROPERTY( m_hexToCircleScale ).editable().range( 0.0f, 1.0f );
	RTTI_PROPERTY( m_usePhysicalSetup ).editable();
	RTTI_PROPERTY( m_planeInFocus ).editable().range( 0.0f, 8000.0f );
	RTTI_PROPERTY( m_fStops ).editable();
	RTTI_PROPERTY( m_bokehSizeMuliplier ).editable().range( 0.0f, 2.0f );
RTTI_END_TYPE();


RTTI_BEGIN_ENUM( EApertureValue )
	RTTI_ENUM_OPTION_ALIAS( "f/1.0", APERTURE_1_0 );
	RTTI_ENUM_OPTION_ALIAS( "f/1.4", APERTURE_1_4 );
	RTTI_ENUM_OPTION_ALIAS( "f/2.0", APERTURE_2_0 );
	RTTI_ENUM_OPTION_ALIAS( "f/2.8", APERTURE_2_8 );
	RTTI_ENUM_OPTION_ALIAS( "f/4.0", APERTURE_4_0 );
	RTTI_ENUM_OPTION_ALIAS( "f/5.6", APERTURE_5_6 );
	RTTI_ENUM_OPTION_ALIAS( "f/8.0", APERTURE_8_0 );
	RTTI_ENUM_OPTION_ALIAS( "f/11.0", APERTURE_11_0 );
	RTTI_ENUM_OPTION_ALIAS( "f/16.0", APERTURE_16_0 );
	RTTI_ENUM_OPTION_ALIAS( "f/22.0", APERTURE_22_0 );
	RTTI_ENUM_OPTION_ALIAS( "f/32.0", APERTURE_32_0 );
RTTI_END_ENUM();

