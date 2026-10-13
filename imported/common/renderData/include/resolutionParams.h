/**
* Copyright (c)2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

//********************************************************************************************************************//
//********************************************************************************************************************//

namespace rend
{
	namespace dim
	{
		enum EPreset
		{
			eSAVE_THUMBNAIL_CONSOLE,
			eSAVE_THUMBNAIL,
			eHD_16,
			eVGA,
			eHD_4,
			eHD,
			eHD_PLUS,
			eFULL_HD,
			eUltrawide_HD,
			eQHD,
			eQHD_PLUS,
			eUWQHD,
			eWQHD_PLUS,
			eULTRA_HD,

			// Target resolutions below are grouped based on console power and available memory
			eCONSOLE_BASE = eFULL_HD,				// PlayStation4
			eCONSOLE_XBOX_BASE = eHD_PLUS,			// XboxOne, XboxOneS
			eCONSOLE_PRO_PROSPERO_LOCKHART = eQHD,	// PlayStation4 Pro, PlayStation5, XboxOneSeriesS (why PS5 here? We don't get API to detect running on it so it gets what PS4 Pro gets)
			// #TODO: when XSX gets a 30/40hz mode, that will want to be eULTRA_HD
			eCONSOLE_SCORPIO = eULTRA_HD,	// XboxOneX, (hopefully will one day include PS5 and XboxSeriesX :()
			eCONSOLE_ANACONDA = eQHD_PLUS,	// XboxSeriesX

#ifdef RED_PLATFORM_CONSOLE
			eSAVE_THUMBNAIL_CURRENT_PLATFORM = eSAVE_THUMBNAIL_CONSOLE,
#else
			eSAVE_THUMBNAIL_CURRENT_PLATFORM = eSAVE_THUMBNAIL,
#endif
		};

		// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
		// Known presets

		template <EPreset PRESET>
		struct Preset {};

		template <>
		struct Preset< eSAVE_THUMBNAIL_CONSOLE >
		{
			static constexpr Uint32		W = 228;
			static constexpr Uint32		H = 128;
			static const char*			NAME( ) { return "228 x 128"; }
			static const char*			LONG_NAME( ) { return "228 x 128 (SaveThumbnailConsole)"; }
		};

		template <>
		struct Preset< eSAVE_THUMBNAIL >
		{
			static constexpr Uint32		W = 456;
			static constexpr Uint32		H = 256;
			static const char*			NAME( ) { return "456 x 256"; }
			static const char*			LONG_NAME( ) { return "456 x 256 (SaveThumbnail)"; }
		};

		template <>
		struct Preset< eHD_16 >
		{
			static constexpr Uint32		W = 480;
			static constexpr Uint32		H = 270;
			static const char*			NAME() { return "480 x 270"; }
			static const char*			LONG_NAME() { return "480 x 270 (HD/16)"; }
		};

		template <>
		struct Preset< eVGA >
		{
			static constexpr Uint32		W = 640;
			static constexpr Uint32		H = 480;
			static const char*			NAME() { return "640 x 480"; }
			static const char*			LONG_NAME() { return "640 x 480 (VGA)"; }
		};

		template <>
		struct Preset< eHD_4 >
		{
			static constexpr Uint32		W = 960;
			static constexpr Uint32		H = 540;
			static const char*			NAME() { return "960 x 540"; }
			static const char*			LONG_NAME() { return "960 x 540 (HD/4)"; }
		};

		template <>
		struct Preset< eHD >
		{
			static constexpr Uint32		W = 1280;
			static constexpr Uint32		H = 720;
			static const char*			NAME() { return "1280 x 720"; }
			static const char*			LONG_NAME() { return "1280 x 720 (HD)"; }
		};

		template <>
		struct Preset< eHD_PLUS >
		{
			static constexpr Uint32		W = 1600;
			static constexpr Uint32		H = 900;
			static const char*			NAME() { return "1600 x 900"; }
			static const char*			LONG_NAME() { return "1600 x 900 (HD+)"; }
		};

		template <>
		struct Preset< eFULL_HD >
		{
			static constexpr Uint32		W = 1920;
			static constexpr Uint32		H = 1080;
			static const char*			NAME() { return "1920 x 1080"; }
			static const char*			LONG_NAME() { return "1920 x 1080 (FULLHD)"; }
		};

		template <>
		struct Preset< eUltrawide_HD >
		{
			static constexpr Uint32		W = 2560;
			static constexpr Uint32		H = 1080;
			static const char*			NAME() { return "2560 x 1080"; }
			static const char*			LONG_NAME() { return "2560 x 1080 (ultrawide HD)"; }
		};


		template <>
		struct Preset< eQHD >
		{
			static constexpr Uint32		W = 2560;
			static constexpr Uint32		H = 1440;
			static const char*			NAME() { return "2560 x 1440"; }
			static const char*			LONG_NAME() { return "2560 x 1440 (QHD)"; }
		};

		template <>
		struct Preset< eQHD_PLUS >
		{
			static constexpr Uint32		W = 3200;
			static constexpr Uint32		H = 1800;
			static const char*			NAME() { return "3200 x 1800"; }
			static const char*			LONG_NAME() { return "3200 x 1800 (QHD+)"; }
		};

		template <>
		struct Preset< eUWQHD >
		{
			static constexpr Uint32		W = 3440;
			static constexpr Uint32		H = 1440;
			static const char*			NAME() { return "3440 x 1440"; }
			static const char*			LONG_NAME() { return "3440 x 1440 (UWQHD)"; }
		};

		template <>
		struct Preset< eWQHD_PLUS >
		{
			static constexpr Uint32		W = 3840;
			static constexpr Uint32		H = 1600;
			static const char*			NAME() { return "3840 x 1600"; }
			static const char*			LONG_NAME() { return "3840 x 1600 (WQHD+)"; }
		};

		template <>
		struct Preset< eULTRA_HD >
		{
			static constexpr Uint32		W = 3840;
			static constexpr Uint32		H = 2160;
			static const char*			NAME() { return "3840 x 2160"; }
			static const char*			LONG_NAME() { return "3840 x 2160 (ULTRAHD)";  }
		};

		RENDER_DATA_API void GetResolution( const EPreset preset, Uint32& outWidth, Uint32& outHeight );
		RENDER_DATA_API void GetPreset( const Uint32 width, const Uint32 height, EPreset& outPreset );

		// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
		// Compute view ratio

		template <EPreset PRESET>
		struct Ratio
		{
			static constexpr Float		value = (Float)Preset<PRESET>::W / (Float)Preset<PRESET>::H;
			static constexpr Float		invValue = (Float)Preset<PRESET>::H / (Float)Preset<PRESET>::W;
		};

		// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
		// Clamp resolution

		template <EPreset PRESET>
		struct Clamp
		{
			static Uint32 W( Uint32 w )
			{
				return ::Clamp<Uint32>( w, 0, Preset<PRESET>::W );
			}
			static Uint32 H( Uint32 h )
			{
				return ::Clamp<Uint32>( h, 0, Preset<PRESET>::H );
			}

			static void WH( Uint32& w, Uint32& h )
			{
				w = W( w );
				h = H( h );
			}

			static void FitInner( Uint32& w, Uint32& h )
			{
				if( w > Preset<PRESET>::W || h > Preset<PRESET>::H )
				{
					RED_FATAL_ASSERT( w && h, "Invalid size: %d  %d", w, h );
					const Float ratioW = (Float)Preset<PRESET>::W / (Float)w;
					const Float ratioH = (Float)Preset<PRESET>::H / (Float)h;
					const Float minRatio = Min( ratioW, ratioH );

					w = (Uint32)(w * minRatio);
					h = (Uint32)(h * minRatio);
				}
			}

			static void FitOuter( Uint32& w, Uint32& h )
			{
				if( w > Preset<PRESET>::W || h > Preset<PRESET>::H )
				{
					RED_FATAL_ASSERT( w && h, "Invalid size: %d  %d", w, h );
					const Float ratioW = (Float)Preset<PRESET>::W / (Float)w;
					const Float ratioH = (Float)Preset<PRESET>::H / (Float)h;
					const Float minRatio = Max( ratioW, ratioH );

					w = (Uint32)(w * minRatio);
					h = (Uint32)(h * minRatio);
				}
			}

		};

		// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -


	}

	namespace camera
	{
		RENDER_DATA_API extern const Float NEAR_PLANE_DEFAULT;
		RENDER_DATA_API extern const Float FAR_PLANE_DEFAULT;
		RENDER_DATA_API extern const Float WEAPON_PLANE_NEAR_DEFAULT;
		RENDER_DATA_API extern const Float WEAPON_PLANE_FAR_DEFAULT;
	}

}

RTTI_DECLARE_ENUM_IN_NAMESPACE( EPreset, rend, dim );

//********************************************************************************************************************//
//********************************************************************************************************************//
