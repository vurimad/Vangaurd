/*
 * Copyright (c)2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#include "../../../backend/backendEngine/include/exporter.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/resource/include/basicTextBuilder.h"
#include "../../../common/resource/include/xmlNewWriter.h"

#include "../../../common/resourceSVG/include/svgResource.h"

class SVGExporter : public IExporter
{
	RTTI_DECLARE_TYPE( SVGExporter );
	RED_BASE_CLASS( IExporter );

public:
	SVGExporter();
	virtual Bool DoExport( const ExportOptions& options ) override;

private:

	Bool ExportDimensions( xml::Writer& writer, SvgResource& resource );
	Bool WriteXmlnsAttr( xml::Writer& writer );
	Bool ExportShapes( xml::Writer& writer, SvgResource& resource );

	Bool ExportFilledPath( xml::Writer& writer, red::ArraySpan< const Vector2 > points, vg::VectorGraphicStyle& style );
	Bool ExportPath( xml::Writer& writer, red::ArraySpan< const Vector2 > points, vg::VectorGraphicStyle& style, Float stroke );

	red::HashSet< THandle< vg::VectorGraphicStyle > > m_styles;
};

RTTI_BEGIN_TYPE( SVGExporter );
RTTI_PARENT_TYPE( IExporter );
RTTI_END_TYPE();


SVGExporter::SVGExporter()
{
	m_resourceClass = ClassID< SvgResource >();
	m_formats.PushBack(CFileFormat("svg", "SVG Resource definition file"));
}


Bool SVGExporter::DoExport( const ExportOptions& options )
{
	THandle< SvgResource > svgRes = Cast< SvgResource >( options.m_resource );

	RED_FATAL_ASSERT( svgRes );

	CBasicTextBuilder buffer;
	buffer.Write( "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n" );

	red::UniquePtr< xml::Writer > xmlWriter = xml::Writer::Create( buffer );

	xmlWriter->Open( "svg" );

	WriteXmlnsAttr( *xmlWriter );
	ExportDimensions( *xmlWriter, *svgRes );
	ExportShapes( *xmlWriter, *svgRes );

	xmlWriter->Close();

	auto fileWriter = GFileManager->CreateFileWriter( red::AbsolutePath::ParseFilePath( options.m_saveFilePath ), FOF_AbsolutePath );

	if ( fileWriter )
	{
		buffer.SaveToFile( fileWriter.Get() );
		return true;
	}
	else
		return false;
}

Bool SVGExporter::ExportDimensions( xml::Writer& writer, SvgResource& resource )
{
	Vector2 dimensions = resource.GetVectorGraphicDefinition()->GetDimensions();

	const Uint32 viewBox[4] = {
		0u, 0u,
		(Uint32)dimensions.X, (Uint32)dimensions.Y
	};
	writer.Attrf( "viewBox", "%u %u %u %u", viewBox[0], viewBox[1], viewBox[2], viewBox[3] );
	return true;
}

Bool SVGExporter::WriteXmlnsAttr( xml::Writer& writer )
{
	writer.Attr( "xmlns", "http://www.w3.org/2000/svg" );
	return true;
}

static String PrintColor( Color color )
{
	return String::Printf( "#%.2x%.2x%.2x", color.R, color.G, color.B );
}

Bool SVGExporter::ExportShapes( xml::Writer& writer, SvgResource& resource )
{
	THandle< vg::VectorGraphicDefinition > vgDef = resource.GetVectorGraphicDefinition();
	THandle< vg::VectorGraphicShape_Group > rootGroup = vgDef->GetRootShapeGroup();


	auto childShapes = rootGroup->GetChildShapes();

	for ( const THandle< vg::BaseVectorGraphicShape >& shape : childShapes )
	{
		if ( auto polygon = Cast< vg::VectorGraphicShape_Polygon >( shape ) )
		{
			red::ArraySpan< const Vector2 > pointsSpan{ polygon->Points };
			if ( !ExportFilledPath( writer, pointsSpan, *polygon->Style ) )
				return false;
		}
		else if ( auto polyline = Cast< vg::VectorGraphicShape_PolyLine >( shape ) )
		{
			red::ArraySpan< const Vector2 > pointsSpan{ polyline->Points };
			if ( !ExportPath( writer, pointsSpan, *polyline->Style, polyline->Stroke ) )
				return false;
		}
	}

	return true;
}

Bool SVGExporter::ExportPath( xml::Writer& writer, red::ArraySpan< const Vector2 > points, vg::VectorGraphicStyle& style, Float stroke )
{
	if ( points.Empty() )
		return true;

	red::StringBuilder< String > builder;

	for ( Uint32 i = 0; i < points.Size(); ++i )
	{
		const Vector2& point = points[i];

		if ( i == 0 )
			builder.Appendf( "M %.3f %.3f ", point.X, point.Y );
		else
			builder.Appendf( "L %.3f %.3f ", point.X, point.Y );
	}
	builder.Append( "Z" );
	Color color = style.GetAttributeDefault< Color >( vg::EStyleAttributeType::FillColor, Color::RED() );

	writer.Open( "path" );
	writer.Attr( "d", builder.AsChar() );
	writer.Attr( "fill", "transparent" );
	writer.Attr( "stroke", PrintColor( color ).AsChar() );
	writer.Attrf( "stroke-width", "%f", stroke );
	writer.Close();

	return true;
}

Bool SVGExporter::ExportFilledPath( xml::Writer& writer, red::ArraySpan< const Vector2 > points, vg::VectorGraphicStyle& style )
{
	if ( points.Empty() )
		return true;

	red::StringBuilder< String > builder;

	for ( Uint32 i = 0; i < points.Size(); ++i )
	{
		const Vector2& point = points[i];

		if ( i == 0 )
			builder.Appendf( "M %.3f %.3f ", point.X, point.Y );
		else
			builder.Appendf( "L %.3f %.3f ", point.X, point.Y );
	}
	builder.Append( "Z" );
	Color color = style.GetAttributeDefault< Color >( vg::EStyleAttributeType::FillColor, Color::RED() );

	writer.Open( "path" );
	writer.Attr( "d", builder.AsChar() );
	writer.Attr( "fill", PrintColor( color ).AsChar() );
	writer.Close();

	return true;
}
