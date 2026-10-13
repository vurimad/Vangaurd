#pragma once

namespace smd
{

	/// SMD file parser
	class FileData
	{
	public:
		/// Node ( bone in 3dsmax )
		struct Node
		{
			red::String	m_name;			//!< Name of the bone
			Int32		m_parent;		//!< Index of parent bone 

			RED_FORCE_INLINE Node(const AnsiChar*name, const Int32 parent)
				: m_name(name)
				, m_parent(parent)
			{};
		};

		/// Animation key
		struct Key
		{
			Quaternion		m_rotation;		//!< Bone relative rotation
			Vector3			m_position;		//!< Bone relative position

			RED_FORCE_INLINE Key(const Vector3 &pos, const Quaternion &rot)
				: m_position(pos)
				, m_rotation(rot)
			{};
		};

		/// Frame - collection of keys
		struct Frame
		{
			typedef red::DynArray< Key > TKeys;

			TKeys	m_keys;		//!< Animation keys
		};

		/// Reference to a bone with a weight, used for skinning
		struct SkinVertex
		{
			Int16	m_bone;			//!< Bone index
			Float	m_weight;		//!< Skinning weights

			RED_FORCE_INLINE SkinVertex()
				: m_bone(0)
				, m_weight(0.0f)
			{};
		};

		/// Mesh Vertex, Constant format
		struct Vertex
		{
			Vector3				m_pos;			//!< Vertex position
			Vector3				m_normal;		//!< Vertex normal
			Vector2				m_uv;			//!< Vertex UV
			SkinVertex			m_skin[4];		//!< Skinning data
			Uint8				m_numSkin;		//!< Number of skinning bones

			RED_FORCE_INLINE Vertex()
				: m_numSkin(0)
			{};
		};

		/// Triangle
		struct Triangle
		{
			Int8		m_material;			//!< Index of material
			Vertex		m_verts[3];			//!< Vertices
			Triangle*	m_next;				//!< Next triangle by material

			RED_FORCE_INLINE Triangle()
				: m_material(0)
				, m_next(nullptr)
			{};
		};

		/// Material
		struct Material
		{
			red::String			m_name;			//!< Name of material
			Triangle*			m_triangles;	//!< Triangles sharing this material
			Uint32				m_numTriangles;
												
			Material(const red::String& name = "")
				: m_name(name)
				, m_triangles(nullptr)
				, m_numTriangles(0)
			{};
		};

	public:
		typedef red::DynArray<Node> TNodes;
		typedef red::DynArray<Frame> TFrames;
		typedef red::DynArray<Triangle> TTriangles;
		typedef red::DynArray<Material> TMaterials;

		Int32			m_fileVersion;      //!< File version  
		TNodes			m_nodes;            //!< Nodes
		TFrames			m_frames;			//!< Frames
		TTriangles		m_triangles;		//!< Triangles
		TMaterials		m_materials;		//!< Materials

	public:
		//! Parse file
		const Bool Parse(const red::AbsolutePath &fileName, red::String &outError);

	protected:
		Int8 IndexMaterial(const AnsiChar* name);

		const Bool ParseFile(const AnsiChar*& buffer, red::String &outError);
		const Bool ParseNodes(const AnsiChar*& buffer, red::String &outError);
		const Bool ParseFrame(const AnsiChar*& buffer, red::String &outError, Frame& outFrame);
		const Bool ParseSkeleton(const AnsiChar*& buffer, red::String &outError);
		const Bool ParseVertex(const AnsiChar*& buffer, red::String &outError, Vertex& outVertex);
		const Bool ParseTriangle(const AnsiChar*& buffer, red::String &outError, Triangle& outTriangle);
		const Bool ParseTriangles(const AnsiChar*& buffer, red::String &outError);
	};

} // smd