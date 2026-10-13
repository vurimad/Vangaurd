/*
 * Copyright (c)2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#include "../../../backend/backendEngine/include/importer.h"
#include "../../backendEngine/include/factory.h"
#include "../../../common/resource/include/xmlDocument.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redContainers/include/fundamentalStringParser.h"

#include "../../../common/resourceSVG/include/svgResource.h"

class SvgFactory : public IFactory
{
	RTTI_DECLARE_TYPE(SvgFactory);

public:
	SvgFactory()
	{
		m_resourceClass = ClassID< SvgResource >();
	}

	virtual Bool DoCreate(THandle< CResource > res, const THandle< FactoryOptions >& options) override
	{
		return true;
	}
};

RTTI_BEGIN_TYPE(SvgFactory);
RTTI_PARENT_TYPE(IFactory);
RTTI_END_TYPE();

class SVGResourceImporter : public IImporter
{
	RTTI_DECLARE_TYPE(SVGResourceImporter);

public:
	SVGResourceImporter();
	virtual ImportResult DoImport( THandle< CResource > inputResource, const THandle<ResourceImportOptions>& options ) override;

private:
	Bool ConstructShapeFromNode(xml::NodeIterator& node, THandle<vg::VectorGraphicShape_Group>& shapeGroup);

	template<class T>
	THandle<T> ConstructShape(xml::NodeIterator& node, THandle<vg::VectorGraphicShape_Group>& shapeGroup);

	// SVG Attributes
	Bool ParseSVGDimensions(const red::UniquePtr< xml::Document >& document);
	void ParseSVGStyles(xml::NodeIterator& node);

	// Shape Attributes
	void ParseCommonShapeAttributes(xml::NodeIterator& node, vg::BaseVectorGraphicShape::Handle shape);
	void ParseShapeAttributes(xml::NodeIterator& node, THandle<vg::VectorGraphicShape_Group>& shape);
	void ParseShapeAttributes(xml::NodeIterator& node, THandle<vg::VectorGraphicShape_Rect>& shape);
	void ParseShapeAttributes(xml::NodeIterator& node, THandle<vg::VectorGraphicShape_Circle>& shape);
	void ParseShapeAttributes(xml::NodeIterator& node, THandle<vg::VectorGraphicShape_PolyLine>& shape);
	void ParseShapeAttributes(xml::NodeIterator& node, THandle<vg::VectorGraphicShape_Polygon>& shape);
	void ParseShapeAttributes(xml::NodeIterator& node, THandle<vg::VectorGraphicShape_Text>& shape);
	
	// Attribute Helpers
	Bool ParseAttribute_Styles(xml::NodeIterator& node, vg::VectorGraphicStyle::Handle& outStyle);
	Bool ParseAttribute_Transform(xml::NodeIterator& node, Matrix& outTransform);
	Bool ParseAttribute_Position(xml::NodeIterator& node, Vector2& outPosition);
	Bool ParseAttribute_Dimensions(xml::NodeIterator& node, Vector2& outDimensions);
	Bool ParseAttribute_Radius(xml::NodeIterator& node, Float& outRadius);
	Bool ParseAttribute_Points(xml::NodeIterator& node, red::DynArray<Vector2>& outPoints);

	Bool ConvertHexValueToColor(const String& hexValue, Color& outColor);

	vg::VectorGraphicDefinition::Handle m_vectorGraphicDef;
	red::Map<String, vg::VectorGraphicStyle> m_styles;
};

RTTI_BEGIN_TYPE(SVGResourceImporter)
RTTI_PARENT_TYPE(IImporter)
RTTI_END_TYPE()


SVGResourceImporter::SVGResourceImporter()
{
	m_resourceClass = ClassID< SvgResource >();
	m_formats.PushBack(CFileFormat("svg", "SVG Resource definition file"));
}

ImportResult SVGResourceImporter::DoImport( THandle< CResource > inputResource, const THandle<ResourceImportOptions>& options )
{
	THandle< SvgResource > svg = Cast< SvgResource >(inputResource);
	if (!svg)
	{
		return ImportResult::Error( "Core: Unable to import svg resource into something that is not an svg resource" );
	}

	const red::AbsolutePath sourceFilePath = options->GetSourcePath();
	auto xmlFile = GFileManager->CreateFileReader(sourceFilePath, FOF_Buffered | FOF_AbsolutePath);
	red::UniquePtr< xml::Document > doc(xml::Document::Create(xmlFile.Get()));
	if (!doc)
	{
		return ImportResult::Error( "Core: Failed to load XML '%hs'", sourceFilePath.AsChar());
	}

	doc->SetErrorFunc([](const Uint32 line, const String& txt) { RED_LOG_ERROR("SVGResourceImporter: Parsing error at line %d: %hs", line, txt.AsChar()); });

	auto* rootNode = doc->GetRootNode();
	if (!rootNode)
	{
		return ImportResult::Error("Core: Missing 'svg' root node");
	}

	m_vectorGraphicDef = svg->GetVectorGraphicDefinition();
	if (!m_vectorGraphicDef)
	{
		return ImportResult::Error("SVGResourceImporter: SvgResource does not have valid VectorGraphicDefinition");
	}

	m_vectorGraphicDef->Initialize();

	if (!ParseSVGDimensions(doc))
	{
		return ImportResult::Error( "SVGResourceImporter: Error parsing svg dimensions" );
	}

	THandle<vg::VectorGraphicShape_Group> rootShapeGroup = m_vectorGraphicDef->GetRootShapeGroup();

	for (xml::NodeIterator currentNode(*doc, rootNode); currentNode; ++currentNode)
	{
		if (currentNode.GetName() == "style")
		{
			ParseSVGStyles(currentNode);
		}
		else
		{
			if (!ConstructShapeFromNode(currentNode, rootShapeGroup))
			{
				return ImportResult::Error( "SVGResourceImporter: Error constructing shape from node" );
			}
		}
	}
	return ImportResult::Success( svg );
}

Bool SVGResourceImporter::ParseSVGDimensions(const red::UniquePtr< xml::Document >& document)
{
	// Get dimensions of the SVG from the root node
	for (xml::AttributeIterator it(*document, document->GetRootNode(), "style"); it; ++it)
	{
		// Dimensions are the last 2 numbers specified in the document style
		String valueStr = it.GetValue().AsChar();
		red::DynArray<String> styleStrList = valueStr.Split(" ");
		Uint32 length = styleStrList.Size();
		if (length >= 2)
		{
			float width = 0, height = 0;
			FromString(styleStrList.PopBack(), height);
			FromString(styleStrList.PopBack(), width);
			m_vectorGraphicDef->SetDimensions(Vector2(width, height));
		}
		else
		{
			document->Error("Missing SVG style dimensions");
			return false;
		}
		return true;
	}
	return false;
}

void SVGResourceImporter::ParseSVGStyles(xml::NodeIterator& node)
{
	// Store all styles locally. Afterwards, when parsing each shape, we will search these styles and copy the data over.

	String allStylesStr = node.GetValue().AsChar();
	allStylesStr.Trim();

	red::DynArray<String> stylesStrList = allStylesStr.Split("\n");
	for (String styleStr : stylesStrList)
	{
		// Styles are defined as follows:
		// .styleName{prop1:value1;prop2:value2;...;}

		// Extract Style Name
		styleStr = styleStr.StringAfter(".");
		String styleName = styleStr.StringBefore("{");
		RED_ASSERT(m_styles.Find(styleName) == m_styles.End(), "CSVGResourceImporter: Duplicate style definition with name %hs", styleName.AsChar());
		
		// Create Style
		vg::VectorGraphicStyle newStyle;

		// Extract {} around attributes
		styleStr = styleStr.StringAfter("{").StringBeforeFromRight("}");

		red::DynArray<String> styleAttributesStr = styleStr.Split(";");
		for (const String& attrStr : styleAttributesStr)
		{
			const String& attrName = attrStr.StringBefore(":");
			const String& attrValue = attrStr.StringAfter(":");

			if (attrName == "fill")
			{
				Color fillColor;
				if (ConvertHexValueToColor(attrValue, fillColor))
				{
					newStyle.AddAttribute(vg::EStyleAttributeType::FillColor, fillColor);
				}
			}
			else if (attrName == "stroke")
			{
				Color strokeColor;
				if (ConvertHexValueToColor(attrValue, strokeColor))
				{
					newStyle.AddAttribute(vg::EStyleAttributeType::StrokeColor, strokeColor);
				}
			}
			else if (attrName == "stroke-width")
			{
				Int16 strokeSize;
				if (FromString(attrValue, strokeSize))
				{
					newStyle.AddAttribute(vg::EStyleAttributeType::StrokeSize, strokeSize);
				}
			}
			else if (attrName == "stroke-miterlimit")
			{
				Int16 strokeMiterLimit;
				if (FromString(attrValue, strokeMiterLimit))
				{
					newStyle.AddAttribute(vg::EStyleAttributeType::StrokeMiterLimit, strokeMiterLimit);
				}
			}
			else if (attrName == "font-size")
			{
				Float fontSize;
				if (FromString(attrValue, fontSize))
				{
					newStyle.AddAttribute(vg::EStyleAttributeType::FontSize, fontSize);
				}
			}
			else if (attrName == "font-family")
			{
				// TODO: Font family name to Font type
			}
		}

		m_styles[styleName] = newStyle;
	}
}

Bool SVGResourceImporter::ConstructShapeFromNode(xml::NodeIterator& node, THandle<vg::VectorGraphicShape_Group>& shapeGroup)
{
	const xml::StaticString nodeName = node.GetName();
	if (nodeName == "rect")
	{
		return static_cast< Bool >( ConstructShape<vg::VectorGraphicShape_Rect>(node, shapeGroup) );
	}
	else if (nodeName == "circle")
	{
		return static_cast< Bool >( ConstructShape<vg::VectorGraphicShape_Circle>(node, shapeGroup) );
	}
	else if (nodeName == "polyline")
	{
		return static_cast< Bool >( ConstructShape<vg::VectorGraphicShape_PolyLine>(node, shapeGroup) );
	}
	else if (nodeName == "polygon")
	{
		return static_cast< Bool >( ConstructShape<vg::VectorGraphicShape_Polygon>(node, shapeGroup) );
	}
	else if (nodeName == "text")
	{
		return static_cast< Bool >( ConstructShape<vg::VectorGraphicShape_Text>(node, shapeGroup) );
	}
	else if (nodeName == "g")
	{
		THandle<vg::VectorGraphicShape_Group> innerShapeGroup = ConstructShape<vg::VectorGraphicShape_Group>(node, shapeGroup);
		if (innerShapeGroup)
		{
			// Recurse parsing of children nodes for Groups
			for (xml::NodeIterator currentNode(node.GetDocument(), node.GetCurrent()); currentNode; ++currentNode)
			{
				if (!ConstructShapeFromNode(currentNode, innerShapeGroup))
				{
					return false;
				}
			}
			return true;
		}
	}

	return true; // Temporarily return true until all shape types are supported
}

template<class T>
THandle<T> SVGResourceImporter::ConstructShape(xml::NodeIterator& node, THandle<vg::VectorGraphicShape_Group>& shapeGroup)
{
	THandle<T> newShape = shapeGroup->CreateShape<T>();
	if (newShape)
	{
		ParseCommonShapeAttributes(node, newShape);
		ParseShapeAttributes(node, newShape);
	}
	return newShape;
}

//================================================================================================
// Shape Attributes
//================================================================================================

void SVGResourceImporter::ParseCommonShapeAttributes(xml::NodeIterator& node, vg::BaseVectorGraphicShape::Handle shape)
{
	if (m_styles.Size() > 0)
	{
		ParseAttribute_Styles(node, shape->Style);
	}
	ParseAttribute_Transform(node, shape->LocalTransform);

	Vector2 position;
	if (ParseAttribute_Position(node, position))
	{
		shape->LocalTransform.SetTranslation(position);
	}
}

void SVGResourceImporter::ParseShapeAttributes(xml::NodeIterator& node, THandle<vg::VectorGraphicShape_Group>& shape)
{
	// Nothing for now
}

void SVGResourceImporter::ParseShapeAttributes(xml::NodeIterator& node, THandle<vg::VectorGraphicShape_Rect>& shape)
{
	ParseAttribute_Dimensions(node, shape->Dimensions);
}

void SVGResourceImporter::ParseShapeAttributes(xml::NodeIterator& node, THandle<vg::VectorGraphicShape_Circle>& shape)
{
	ParseAttribute_Radius(node, shape->Radius);
}

void SVGResourceImporter::ParseShapeAttributes(xml::NodeIterator& node, THandle<vg::VectorGraphicShape_PolyLine>& shape)
{
	ParseAttribute_Points(node, shape->Points);
}

void SVGResourceImporter::ParseShapeAttributes(xml::NodeIterator& node, THandle<vg::VectorGraphicShape_Polygon>& shape)
{
	ParseAttribute_Points(node, shape->Points);
}

void SVGResourceImporter::ParseShapeAttributes(xml::NodeIterator& node, THandle<vg::VectorGraphicShape_Text>& shape)
{
	shape->Text = node.GetValue().AsChar();
}


//================================================================================================
// Attribute Helpers
//================================================================================================

Bool SVGResourceImporter::ParseAttribute_Styles(xml::NodeIterator& node, vg::VectorGraphicStyle::Handle& outStyle)
{
	bool foundAttribute = false;
	for (xml::AttributeIterator it(node, "class"); it; ++it)
	{
		// Styles are listed by name with a space delimiter
		// ex: class="st1 st4"
		// Copy all styles to the shapes style. This supports Text shapes which have separate styles for Font type and size.
		String valueStr = it.GetValue().AsChar();
		valueStr = valueStr.Trim();

		red::DynArray<String> stylesStrList = valueStr.Split(" ");
		for (const String& styleName : stylesStrList)
		{
			vg::VectorGraphicStyle foundStyle;
			if (m_styles.Find(styleName, foundStyle))
			{
				// Copy over style attributes
				outStyle->CopyAttributes(foundStyle);
			}
			else
			{
				node.GetDocument().Error("Invalid style '%hs' specified for shape", styleName.AsChar());
			}
		}

		foundAttribute = true;
	}
	return foundAttribute;
}

Bool SVGResourceImporter::ParseAttribute_Position(xml::NodeIterator& node, Vector2& outPosition)
{
	bool foundAttribute = false;
	for (xml::AttributeIterator it(node); it; ++it)
	{
		if (it.GetName() == "x" || it.GetName() == "cx")
		{
			outPosition.X = it.GetValue().ToFloat();
			foundAttribute = true;
		}
		else if (it.GetName() == "y" || it.GetName() == "cy")
		{
			outPosition.Y = it.GetValue().ToFloat();
			foundAttribute = true;
		}
	}
	return foundAttribute;
}

Bool SVGResourceImporter::ParseAttribute_Dimensions(xml::NodeIterator& node, Vector2& outDimensions)
{
	bool foundAttribute = false;
	for (xml::AttributeIterator it(node); it; ++it)
	{
		if (it.GetName() == "width")
		{
			outDimensions.X = it.GetValue().ToFloat();
			foundAttribute = true;
		}
		else if (it.GetName() == "height")
		{
			outDimensions.Y = it.GetValue().ToFloat();
			foundAttribute = true;
		}
	}
	return foundAttribute;
}

Bool SVGResourceImporter::ParseAttribute_Radius(xml::NodeIterator& node, Float& outRadius)
{
	bool foundAttribute = false;
	for (xml::AttributeIterator it(node, "r"); it; ++it)
	{
		outRadius = it.GetValue().ToFloat();
		foundAttribute = true;
	}
	return foundAttribute;
}

Bool SVGResourceImporter::ParseAttribute_Points(xml::NodeIterator& node, red::DynArray<Vector2>& outPoints)
{
	bool foundAttribute = false;
	for (xml::AttributeIterator it(node); it; ++it)
	{
		if (it.GetName() == "points")
		{
			// Points are formatted with space as delimiter between points, and comma as delimiter between X and Y
			// ex: points="0.0,1.0 2.0,3.5 4.3,1.0"
			String valueStr = it.GetValue().AsChar();
			valueStr = valueStr.Trim();

			red::DynArray<String> pointsStrList = valueStr.Split(" ");
			for (const String& pointStr : pointsStrList)
			{
				red::DynArray<String> pointCoordsStr = pointStr.Split(",");
				if (pointCoordsStr.Size() == 2)
				{
					float x, y;
					if (FromString(pointCoordsStr[0], x) && FromString(pointCoordsStr[1], y))
					{
						outPoints.PushBack(Vector2(x, y));
						foundAttribute = true;
					}
					else
					{
						node.GetDocument().Error("Point attribute has non-float values");
					}
				}
				else
				{
					node.GetDocument().Error("Point attribute has invalid format");
				}
			}
		}
	}
	return foundAttribute;
}

Bool SVGResourceImporter::ParseAttribute_Transform(xml::NodeIterator& node, Matrix& outTransform)
{
	bool foundAttribute = false;
	for (xml::AttributeIterator it(node, "transform"); it; ++it)
	{
		// For now we support 'matrix' transform (TODO: translation, scale, rotation, skew)
		// Convert [a b c d e f] to
		//	[ a c e ]
		//  [ b d f ]
		//  [ 0 0 1 ]
		String valueStr = it.GetValue().AsChar();
		String transformStr = valueStr.StringAfter("(").StringBeforeFromRight(")");
		red::DynArray<String> transformStrList = transformStr.Split(" ");
		if (transformStrList.Size() == 6)
		{
			Float a, b, c, d, e, f;
			FromString(transformStrList[0], a);
			FromString(transformStrList[1], b);
			FromString(transformStrList[2], c);
			FromString(transformStrList[3], d);
			FromString(transformStrList[4], e);
			FromString(transformStrList[5], f);

			outTransform.SetIdentity();
			outTransform.SetRow(0, Vector4(a, c, e, 0.f));
			outTransform.SetRow(1, Vector4(b, d, f, 0.f));
			
			foundAttribute = true;
		}
		else
		{
			node.GetDocument().Error("Matrix transform attribute has more than 6 values");
		}
	}
	return foundAttribute;
}

Bool SVGResourceImporter::ConvertHexValueToColor(const String& hexValue, Color& outColor)
{
	if (hexValue == "none")
	{
		outColor = Color::BLACK(); // Default
		return true;
	}

	// Strip out '#' char
	String hexStripped = hexValue.StringAfter("#");
	Uint32 colorValue = 0;
	const auto* hexValueChar = hexStripped.AsChar();
	if (GParseHex(hexValueChar, colorValue))
	{
		outColor = Color(colorValue);
		return true;
	}
	return false;
}