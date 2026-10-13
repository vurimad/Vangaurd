#include "build.h"
#include "objImporter.h"

#include "../../common/redContainers/include/fundamentalStringParser.h"
#include "../../common/redFileSystem/include/fileSys.h"

namespace
{
	// Get tail of string after first token and possibly following spaces
	String Tail( const String& in )
	{
		Uint32 spaceIndex = String::npos;
		Uint32 tabIndex = String::npos;

		if ( in.IndexOf( ' ', spaceIndex ) || in.IndexOf( '\t', tabIndex ) )
		{
			return in.RightString( in.Length() - math::Min( spaceIndex, tabIndex ) ).Trim();
		}

		return String::EMPTY();
	}

	String FirstToken( const String& in )
	{
		if ( !in.Empty() )
		{
			const AnsiChar* stream = in.AsChar();
			GParseWhitespaces( stream );

			String token;
			GParseToken( stream, token );

			return token;
		}
		return String::EMPTY();
	}
}

namespace tools
{
	ObjImporter::ObjImporter() = default;
	ObjImporter::~ObjImporter() = default;

	Bool ParseIndex( const AnsiChar*& stream, red::DynArray< Uint32 >& outIndices )
	{
		String indexStr;
		if ( !GParseString( stream, indexStr ) )
		{
			return false;
		}

		const AnsiChar* subStream = indexStr.AsChar();
		Uint32 index;
		if ( GParseUint32( subStream, index ) )
		{
			outIndices.PushBack( index - 1 );
			return true;
		}

		return false;
	}

	Bool ObjImporter::LoadFile( const red::AbsolutePath& path )
	{
		if ( red::paths::GetExtension( path ) != "obj" )
		{
			return false;
		}

		String fileContent;
		red::LoadFileToString( path, fileContent );

		red::DynArray< String > lines{ red::PoolBackend() };
		fileContent.GetTokens( '\n', false, lines );

		m_meshes.Clear();
		m_vertices.Clear();
		m_faces.Clear();

		red::DynArray<Vector3> vertices{ red::PoolBackend() };
		red::DynArray<GeometryShapeFace> faces{ red::PoolBackend() };

		Bool listening = false;
		String meshName;

		for ( const String& line : lines )
		{
			// Generate a Mesh Object or Prepare for an object to be created
			if ( ::FirstToken( line ) == "o" || ::FirstToken( line ) == "g" || line[ 0 ] == 'g' )
			{
				if ( !listening )
				{
					listening = true;

					if ( ::FirstToken( line ) == "o" || ::FirstToken( line ) == "g" )
					{
						meshName = ::Tail( line );
					}
					else
					{
						meshName = "unnamed";
					}
				}
				else
				{
					// Generate the mesh to put into the array

					if ( !faces.Empty() && !vertices.Empty() )
					{
						Mesh& mesh = m_meshes.EmplaceBack();
						mesh.m_name = meshName;
						mesh.m_vertices = std::move( vertices );
						mesh.m_faces = std::move( faces );

						meshName = ::Tail( line );
					}
					else
					{
						if ( ::FirstToken( line ) == "o" || ::FirstToken( line ) == "g" )
						{
							meshName = ::Tail( line );
						}
						else
						{
							meshName = "unnamed";
						}
					}
				}
			}
			// Generate a Vertex Position
			else if ( ::FirstToken( line ) == "v" )
			{
				const String tail = ::Tail( line );
				const AnsiChar* stream = tail.AsChar();
				Vector3 vpos;

				// change of order is intentional
				Bool result = GParseFloat( stream, vpos.X );
				result &= GParseFloat( stream, vpos.Z );
				result &= GParseFloat( stream, vpos.Y );

				m_vertices.PushBack( vpos );
			}
			// Generate a Face (vertices & indices)
			else if ( ::FirstToken( line ) == "f" )
			{
				const String tail = ::Tail( line );
				const AnsiChar* stream = tail.AsChar();

				GeometryShapeFace face;
				Bool result = ParseIndex( stream, face.m_indices );
				result &= ParseIndex( stream, face.m_indices );
				result &= ParseIndex( stream, face.m_indices );

				while ( ParseIndex( stream, face.m_indices ) );

				if ( result )
				{
					m_faces.PushBack( face );
				}
			}
		}

		// Deal with last mesh

		if ( !faces.Empty() && !vertices.Empty() )
		{
			Mesh& mesh = m_meshes.EmplaceBack();
			mesh.m_name = meshName;
			mesh.m_vertices = std::move( vertices );
			mesh.m_faces = std::move( faces );
		}

		return !( m_meshes.Empty() && m_vertices.Empty() && m_faces.Empty() );
	}
}