/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "screenshotUtilities.h"

#include "../../common/renderData/include/renderer.h"
#include "../../common/renderData/include/renderSelectionResult.h"
#include "../../common/renderData/include/renderCommandHandler.h"
#include "../../common/redFileSystem/include/rawFileWriter.h"
#include "../../common/engine/include/renderFrame.h"

namespace tools
{
	namespace utilities
	{
		void Screenshot::RenderFrame( CRenderFrameInfo& frameInfo, const CRenderCamera& renderCamera, const SCameraRenderViewport& cameraRenderViewport, const Uint64 cameraName )
		{
			frameInfo.IssueCamera( cameraName );

			CRenderCameraFrameInfo cameraInfo;

			auto scene = frameInfo.GetScenePtr();

			GRender->GetCommands()->RegisterCamera( scene, renderCamera, cameraRenderViewport, cameraName );
			GRender->GetCommands()->UpdateCameraData( scene, cameraName, cameraInfo );

			auto frame = TRenderPtr<CRenderFrame>( RED_NEW( CRenderFrame )( std::move( frameInfo ) ) );

			GRender->GetCommands()->RenderScene( frame );
			GRender->GetCommands()->UnregisterCamera( scene, cameraName );

			GRender->Flush();
		}

		img::ImagePtr Screenshot::ToImagePtr( const rend::ColorResult& source, Uint32 width, Uint32 height )
		{
			img::ImagePtr image;

			const img::Image::SizeInfo imageSizeInfo( 4, width, height );
			image = img::Image::CreateEmpty( img::PixelFormat::Format_Uint8, imageSizeInfo );
			red::Memcpy( image->GetPixelBuffer(), source.GetColors(), image->GetPixelBufferSize() );

			return image;
		}

		Bool Screenshot::Save( const red::AbsolutePath& path, img::ImagePtr image )
		{
			img::GenericWriter writer;
			writer.Reserve( image->GetPixelBufferSize() );
			if ( img::BaseImageSaver::Save( writer, image, "png" ) )
			{
				RED_FATAL_ASSERT( writer.GetSize() == writer.GetOffset() );
				if ( writer.GetSize() > 0 )
				{
					auto fileWriter = fs::RawFileWriter::Create( path, false );
					if ( fileWriter )
					{
						fileWriter->Serialize( writer.GetData(), writer.GetSize() );

						RED_LOG( "Thumbnail saved to: %hs", path.AsChar() );
						return true;
					}
				}
			}

			return false;
		}
	}
}
