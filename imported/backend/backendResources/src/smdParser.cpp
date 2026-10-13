/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "smdParser.h"
#include "../../../common/redReflection/include/stringParser.h"
#include "../../../common/redFileSystem/include/fileSys.h"

using red::String;

namespace smd
{
	static const Float GLOBAL_SCALE = 2.2f / 64.0f;

	const Bool FileData::Parse(const red::AbsolutePath &fileName, String &outError)
	{
		// Open source file
		String fileText;
		if (!red::LoadFileToString(fileName, fileText))
		{
			outError = "Unable to open source file";
			return false;
		}

		// Parse version
		const AnsiChar* buffer = fileText.AsChar();
		if (!ParseFile(buffer, outError))
		{
			if (outError.Empty())
				outError = "Parse error"; // output some generic error

			return false;
		}

		// Done, no errors
		return true;
	}

	Int8 FileData::IndexMaterial(const AnsiChar* name)
	{
		for ( Uint32 i : m_materials.Indices() )
			if (m_materials[i].m_name == name)
				return (Int8)i;

		// Add new
		const auto index = m_materials.Size();
		m_materials.PushBack(Material(name));

		return (Int8)index;
	}

	const Bool FileData::ParseFile(const AnsiChar*& buffer, String &outError)
	{
		// Skip the initial comment
		if (buffer[0] == '/' && buffer[1] == '/')
			while (*buffer >= 32)
				buffer++;

		// Parse version keyword
		if (!GParseKeyword(buffer, "version"))
		{
			outError = "Missing version keyword";
			return false;
		}

		// Parse version number
		if (!GParseInt32(buffer, m_fileVersion))
		{
			outError = "Missing version number";
			return false;
		}

		// Check version
		if (m_fileVersion != 1)
		{
			outError = String::Printf("Unsupported SMD version %i", m_fileVersion);
			return false;
		}

		// Clear data
		m_frames.Clear();
		m_materials.Clear();
		m_nodes.Clear();
		m_triangles.Clear();

		// Parse nodes
		if (!GParseKeyword(buffer, "nodes"))
		{
			outError = "Missing nodes block";
			return false;
		}
		else if (!ParseNodes(buffer, outError))
		{
			return false;
		}

		// Parse skeleton
		if (!GParseKeyword(buffer, "skeleton"))
		{
			outError = "Missing skeleton block";
			return false;
		}
		else if (!ParseSkeleton(buffer, outError))
		{
			return false;
		}

		// Parse triangles - optional
		if (GParseKeyword(buffer, "triangles"))
		{
			if (!ParseTriangles(buffer, outError))
			{
				return false;
			}
		}

		// Group triangles
		for (auto& tri : m_triangles)
		{
			auto& mat = m_materials[tri.m_material];

			tri.m_next = mat.m_triangles;
			mat.m_triangles = &tri;
			mat.m_numTriangles += 1;
		}

		return true;
	}

	const Bool FileData::ParseNodes(const AnsiChar*& buffer, String &outError)
	{
		// Parsing loop
		for (;;)
		{
			// End command
			if (GParseKeyword(buffer, "end"))
				break;

			// Node index
			Int32 index = -1;
			if (!GParseInt32(buffer, index))
			{
				outError = "Missing node index";
				return false;
			}

			// Parse node name
			String nodeName;
			if (!GParseString(buffer, nodeName))
			{
				outError = "Missing node name";
				return false;
			}

			// Parse parent node
			Int32 parent = -1;
			if (!GParseInt32(buffer, parent))
			{
				outError = "Invalid node parent";
				return false;
			}

			// Check parent index
			if (parent < -1 || parent >= index)
			{
				outError = String::Printf("Invalid node parent index %i/%i", parent, index);
				return false;
			}

			// Strip node name
			String shortName;
			if (nodeName.SplitFromRight(".", nullptr, &shortName))
				nodeName = shortName;

			// Add node
			m_nodes.EmplaceBack(nodeName.AsChar(), parent);
		}

		return true;
	}

	const Bool FileData::ParseFrame(const AnsiChar*& buffer, String &outError, Frame& outFrame)
	{
		for ( Uint32 i : m_nodes.Indices() )
		{
			// Check index
			Int32 index = -1;
			if (!GParseInt32(buffer, index) || index != (Int32)i)
			{
				outError = "Invalid key bone index";
				return false;
			}

			// Load data
			Vector3 pos;
			Vector3 rot;
			bool state = true;
			state &= GParseFloat(buffer, pos.X);
			state &= GParseFloat(buffer, pos.Y);
			state &= GParseFloat(buffer, pos.Z);
			state &= GParseFloat(buffer, rot.X);
			state &= GParseFloat(buffer, rot.Y);
			state &= GParseFloat(buffer, rot.Z);
			if (!state)
			{
				outError = "Unable to parse bone state";
				return false;
			}

			// Apply scale
			pos.X *= GLOBAL_SCALE;
			pos.Y *= GLOBAL_SCALE;
			pos.Z *= GLOBAL_SCALE;

			// Convert radians to degrees
			Matrix xRot, yRot, zRot;
			xRot.SetIdentity();
			xRot.SetRotX33(rot.X);
			yRot.SetIdentity();
			yRot.SetRotY33(rot.Y);
			zRot.SetIdentity();
			zRot.SetRotZ33(rot.Z);

			Matrix fullRot = xRot * yRot * zRot; // roll X, pitch Y, yaw Z - SMD coordinates

			// Add key
			outFrame.m_keys.EmplaceBack(pos, fullRot.ToQuat());
		}

		return true;
	}

	const Bool FileData::ParseSkeleton(const AnsiChar*& buffer, String &outError)
	{
		for (;;)
		{
			if (GParseKeyword(buffer, "end"))
				break;

			// Should be a time command
			if (!GParseKeyword(buffer, "time"))
			{
				outError = "Missing time command";
				return false;
			}

			// Get time index
			Int32 time = -1;
			if (!GParseInt32(buffer, time))
			{
				outError = "Invalid frame index";
				return false;
			}

			// Add frame
			Frame& frame = m_frames.EmplaceBack();
			if (!ParseFrame(buffer, outError, frame))
				return false;
		}

		return true;
	}

	const Bool FileData::ParseVertex(const AnsiChar*& buffer, String &outError, Vertex& outVertex)
	{
		// Load stuff
		Int32 bone = -1;
		Bool state = true;
		state &= GParseInt32(buffer, bone);
		state &= GParseFloat(buffer, outVertex.m_pos.X);
		state &= GParseFloat(buffer, outVertex.m_pos.Y);
		state &= GParseFloat(buffer, outVertex.m_pos.Z);
		state &= GParseFloat(buffer, outVertex.m_normal.X);
		state &= GParseFloat(buffer, outVertex.m_normal.Y);
		state &= GParseFloat(buffer, outVertex.m_normal.Z);
		state &= GParseFloat(buffer, outVertex.m_uv.X);
		state &= GParseFloat(buffer, outVertex.m_uv.Y);

		// Apply scale
		outVertex.m_pos.X *= GLOBAL_SCALE;
		outVertex.m_pos.Y *= GLOBAL_SCALE;
		outVertex.m_pos.Z *= GLOBAL_SCALE;

		// Swap UV mapping - SMD specific
		outVertex.m_uv.Y = 1.0f - outVertex.m_uv.Y;

		if (!state)
		{
			outError = "Error parsing basic vertex";
			return false;
		}

		// Parse skinning data
		Int32 numBones = 0;
		outVertex.m_numSkin = 0;
		if (GParseInt32(buffer, numBones) && numBones >= 1)
		{
			RED_FATAL_ASSERT(numBones < RED_ARRAY_COUNT(outVertex.m_skin), "To many bones per vertex");

			// Parse skinning
			for (Int32 i = 0; i < numBones; i++)
			{
				SkinVertex& sv = outVertex.m_skin[outVertex.m_numSkin++];
				state &= GParseInt16(buffer, sv.m_bone);
				state &= GParseFloat(buffer, sv.m_weight);

				// Check index
				if (sv.m_bone < 0 || sv.m_bone >= (Int32)m_nodes.Size())
				{
					outError = String::Printf("Invalid skinning bone index %i/%i", sv.m_bone, m_nodes.Size());
					return false;
				}
			}
		}
		else
		{
			// Setu single bone skinning
			outVertex.m_numSkin = 1;

			SkinVertex& sv = outVertex.m_skin[0];
			sv.m_bone = bone;
			sv.m_weight = 1;
		}

		// Error
		if (!state)
		{
			outError = "Error parsing skinning data";
			return false;
		}

		return true;
	}

	const Bool FileData::ParseTriangle(const AnsiChar*& buffer, String &outError, Triangle& outTriangle)
	{
		// Parse material
		String materialName;
		if (!GParseToken(buffer, materialName))
		{
			outError = "Missing triangle material name";
			return false;
		}

		// Lookup triangle name
		outTriangle.m_material = IndexMaterial(materialName.AsChar());

		// Parse vertices
		for (Uint32 i = 0; i < 3; ++i)
			if (!ParseVertex(buffer, outError, outTriangle.m_verts[i]))
				return false;
	
		return true;
	}

	const Bool FileData::ParseTriangles(const AnsiChar*& buffer, String &outError)
	{
		for (;;)
		{
			// End command
			if (GParseKeyword(buffer, "end"))
				break;

			// Parse triangle
			Triangle& tri = m_triangles.EmplaceBack();
			if (!ParseTriangle(buffer, outError, tri))
				return false;
		}

		return true;
	}

} // smd
