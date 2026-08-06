/**
* Copyright (c) 2015-2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "editorObjectIdPath.h"
#include "types.h"

#include "../../redContainers/include/string/stringBuilder.h"
#include "rttiSimpleType.h"
#include "stringSerialization.h"
#include "rttiClassBuilder.h"

#include "../../redSystem/include/crc.h"
#include "../../redFileSystem/include/file.h"
#include "../../../common/redContainers/include/fundamentalStringParser.h"


namespace tools
{
	EditorObjectIDPath::EditorObjectIDPath()
		: m_elements( red::PoolBackend() )
	{}

	EditorObjectIDPath::EditorObjectIDPath( const EditorObjectIDPath& other, Uint32 numElements )
		: m_elements( red::PoolBackend() )
	{
		numElements = math::Min( numElements, other.m_elements.Size() );

		m_elements.Reserve( numElements );

		for ( Uint32 i = 0; i < numElements; ++i )
		{
			m_elements.PushBack( other.m_elements[i] );
		}
	}

	EditorObjectIDPath::EditorObjectIDPath( const EditorObjectID& root )
		: m_elements( red::PoolBackend() )
	{
		if ( root.IsValid() )
			m_elements.PushBack( root );
	}

	EditorObjectIDPath::EditorObjectIDPath( const red::ArraySpan< const EditorObjectID >& elements )
		: m_elements( red::PoolBackend() )
	{
		m_elements.Reserve( elements.Size() );
		for ( const EditorObjectID& id : elements )
		{
			m_elements.PushBack( id );
		}
	}

	EditorObjectIDPath::EditorObjectIDPath( red::DynArray< EditorObjectID >&& elements )
		: m_elements( red::PoolBackend() )
	{
		m_elements = std::move( elements );
	}

	EditorObjectIDPath::EditorObjectIDPath( const EditorObjectIDPath& parent, const EditorObjectID& child )
		: m_elements( red::PoolBackend() )
	{
		m_elements.Reserve( parent.m_elements.Size() + 1 );
		m_elements.PushBack( parent.m_elements );
		if (child.IsValid())
			m_elements.PushBack(child);
	}

	EditorObjectIDPath::EditorObjectIDPath( const EditorObjectID& parent, const EditorObjectIDPath& child )
		: m_elements( red::PoolBackend() )
	{
		m_elements.PushBack( parent );
		m_elements.PushBack( child.GetElements() );
	}

	EditorObjectIDPath::EditorObjectIDPath( const EditorObjectIDPath& parent, const EditorObjectIDPath& child )
		: m_elements( red::PoolBackend() )
	{
		m_elements.Reserve( parent.GetElements().Size() + child.GetElements().Size() );
		m_elements.PushBack( parent.GetElements() );
		m_elements.PushBack( child.GetElements() );
	}

	EditorObjectIDPath::EditorObjectIDPath( EditorObjectIDPath&& parent, const EditorObjectID& child )
		: m_elements( std::move( parent.m_elements ) )
	{
		if ( child.IsValid() )
			m_elements.PushBack( child );
	}

	Bool EditorObjectIDPath::operator == ( const EditorObjectIDPath& other ) const
	{
		return m_elements == other.m_elements;
	}

	Bool EditorObjectIDPath::operator != ( const EditorObjectIDPath& other ) const
	{
		return m_elements != other.m_elements;
	}

	Bool EditorObjectIDPath::operator < ( const EditorObjectIDPath& other ) const
	{
		if ( m_elements.Size() < other.m_elements.Size() )
			return true;

		if ( m_elements.Size() > other.m_elements.Size() )
			return false;

		for ( Uint32 i = 0; i < m_elements.Size(); ++i )
		{
			if ( m_elements[i] < other.m_elements[i] )
				return true;
			else if ( other.m_elements[i] < m_elements[i] )
				return false;
		}

		return false;
	}

	Bool EditorObjectIDPath::IsValid() const
	{
		if ( IsEmpty() )
			return false;

		for ( const EditorObjectID& id : m_elements )
		{
			if ( !id.IsValid() )
			{
				return false;
			}
		}

		return true;
	}

	Bool EditorObjectIDPath::IsEmpty() const
	{
		return m_elements.Empty();
	}

	const EditorObjectIDPath::TPathElements& EditorObjectIDPath::GetElements() const
	{
		return m_elements;
	}

	Uint32 EditorObjectIDPath::GetDepth() const
	{
		return m_elements.Size();
	}

	const EditorObjectID& EditorObjectIDPath::GetFirstChildID() const
	{
		static EditorObjectID theNull;
		return m_elements.Empty() ? theNull : m_elements.Front();
	}

	const EditorObjectID& EditorObjectIDPath::GetFinalChildID() const
	{
		static EditorObjectID theNull;
		return m_elements.Empty() ? theNull : m_elements.Back();
	}

	void EditorObjectIDPath::RemoveFirstElement()
	{
		if ( !m_elements.Empty() )
		{
			m_elements.RemoveAt( 0 );
		}
	}

	String EditorObjectIDPath::ToString() const
	{
		red::StringBuilder<String> b;
	
		for ( const EditorObjectID& pathId : m_elements )
		{
			b.Append( pathId.ToString() );
			b.Append( '#' );
		}

		return b.ToString();
	}

	Bool EditorObjectIDPath::FromString( const String& str, EditorObjectIDPath& outIdPath )
	{
		red::DynArray<String> tokens{ red::PoolBackend() };
		str.GetTokens( '#', false, tokens );
		for ( const String& token : tokens )
		{
			EditorObjectID el;
			if( !EditorObjectID::FromString( token, el ) )
			{
				return false;
			}
			outIdPath.m_elements.PushBack( el );
		}
		return true;
	}

	Uint32 EditorObjectIDPath::CalcHash() const
	{
		Uint32 hash = RED_FNV_OFFSET_BASIS32;

		for ( const EditorObjectID& element : m_elements )
		{
			hash = red::CombineHashes32( hash, element.CalcHash() );
		}

		return hash;
	}

	Uint64 EditorObjectIDPath::CalcHash64() const
	{
		red::THash64 hash = RED_FNV_OFFSET_BASIS64;

		for ( const EditorObjectID& element : m_elements )
		{
			hash = red::CombineHashes64( hash, element.CalcHash64() );
		}

		return hash;
	}

	void EditorObjectIDPath::RemoveLastElements( Uint32 num )
	{
		if ( num > 0 )
		{
			if ( num > m_elements.Size() )
			{
				m_elements.Clear();
				return;
			}

			m_elements.RemoveAt( m_elements.Size() - num, m_elements.Size() );
		}
	}

	EditorObjectIDPath EditorObjectIDPath::RemovedLastElements( Uint32 num ) const
	{
		const Uint32 toCopy = num >= m_elements.Size() ? 0 : m_elements.Size() - num;
		return EditorObjectIDPath( *this, toCopy );
	}

	EditorObjectIDPath EditorObjectIDPath::StrippedFromKind( CName kind ) const
	{
		EditorObjectIDPath ret;
		ret.m_elements.Reserve( m_elements.Size() );

		for ( const EditorObjectID& id : m_elements )
		{
			if ( id.GetKind() != kind )
			{
				ret.m_elements.PushBack( id );
			}
		}

		return ret;
	}

	EditorObjectIDPath EditorObjectIDPath::StrippedFromIntermediateKind( CName kind ) const
	{
		EditorObjectIDPath ret;
		ret.m_elements.Reserve( m_elements.Size() );

		if ( !m_elements.Empty() )
		{
			for ( Uint32 i = 0; i < m_elements.Size() - 1; ++i )
			{
				const EditorObjectID& id = m_elements[i];
				if ( id.GetKind() != kind )
				{
					ret.m_elements.PushBack( id );
				}
			}

			ret.m_elements.PushBack( m_elements.Back() );
		}

		return ret;
	}

	EditorObjectIDPath EditorObjectIDPath::StrippedFromKindConstSufix( CName kind ) const
	{
		EditorObjectIDPath ret;
		ret.m_elements.Reserve( m_elements.Size() );

		if ( !m_elements.Empty() )
		{
			for ( Uint32 i = 0; i < m_elements.Size(); ++i )
			{
				const EditorObjectID& id = m_elements[i];
				if ( id.GetKind() != kind )
				{
					ret.m_elements.PushBack( id );
				}
			}

			Uint32 sufixBegin = ret.m_elements.Size();
			for ( Int32 i = m_elements.Size() - 1; i >= 0 && m_elements[i].GetKind() == kind; --i )
			{
				ret.m_elements.InsertAt( sufixBegin, m_elements[i] );
			}
		}

		Bool isImmediate = false;
		for ( const EditorObjectID& id : ret.GetElements().Reverse() )
		{
			if ( id.GetKind() != kind )
				isImmediate = true;

			RED_ASSERT( !( isImmediate && id.GetKind() == kind ), "" );
		}

		return ret;
	}

	Bool EditorObjectIDPath::HasCommonParent( const tools::EditorObjectIDPath& a, const tools::EditorObjectIDPath& b )
	{
		RED_FATAL_ASSERT( !a.IsEmpty() && !b.IsEmpty(), "Empty objects not allowed" );

		if ( a.GetDepth() != b.GetDepth() )
			return false;

		const auto& elementsA = a.GetElements();
		const auto& elementsB = b.GetElements();

		const auto parentDepth = a.GetDepth() - 1;
		for ( Uint32 i = 0; i < parentDepth; ++i )
			if ( elementsA[i] != elementsB[i] )
				return false;

		return true;
	}

	Bool EditorObjectIDPath::StartsWith( const EditorObjectIDPath& a, const EditorObjectIDPath& b )
	{
		if ( a.GetDepth() < b.GetDepth() )
		{
			return false;
		}

		for ( Uint32 i = 0u; i < b.GetDepth(); ++i )
		{
			if ( a.GetElements()[ i ] != b.GetElements()[ i ] )
			{
				return false;
			}
		}
		return true;
	}

} // tools

RTTI_BEGIN_TYPE_IN_NAMESPACE(EditorObjectIDPath, tools);
	RTTI_PROPERTY(m_elements);
RTTI_END_TYPE();

