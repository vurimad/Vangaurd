/*
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "editorGraphNodeClassesDescriptor.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( CNodeClassDescriptor, tools );
	RTTI_PARENT_TYPE( IBackendData );
	RTTI_PROPERTY( m_childrenClasses );
	RTTI_PROPERTY( m_isAbstract );
	RTTI_PROPERTY( m_isDisabled );
	RTTI_PROPERTY( m_descriptorClassName );
	RTTI_PROPERTY( m_representationClassName );
	RTTI_PROPERTY( m_descriptiveName );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( CNodeClassesDescriptorStructure, tools );
	RTTI_PARENT_TYPE( IBackendData );
	RTTI_PROPERTY( m_rootclasses );
RTTI_END_TYPE();

namespace tools
{
	CNodeClassesDescriptorStructure::Builder::Builder( CNodeClassesDescriptorStructure& structure )
		: m_structure( &structure )
	{
	}

	CNodeClassesDescriptorStructure::Builder::~Builder()
	{
	}

	Bool CNodeClassesDescriptorStructure::Builder::MapRttiClassHierarchy( const rtti::ClassType& rootClassType )
	{
		struct Local
		{
			CNodeClassesDescriptorStructure& m_structure;
			red::DynArray< const rtti::ClassType* > m_subClasses;

			Local( CNodeClassesDescriptorStructure& structure )
				: m_structure( structure )
				, m_subClasses{ red::PoolFrame() }
			{
			}

			void MapClassRec( const rtti::ClassType& classType, CNodeClassDescriptor* parentDescriptor = nullptr )
			{
				// check if descriptor is already mapped
				NodeCollection& nodes = m_structure.m_representationToDescriptor;
				CName className = classType.GetName();
				auto itFind = nodes.Find( className );
				if ( itFind != nodes.End() )
				{
					if ( parentDescriptor )
					{
						parentDescriptor->m_childrenClasses.PushBack( itFind.Value() );
						// remove that class from root classes collection (as it probably was there)
						m_structure.m_rootclasses.Remove( itFind.Value() );
					}

					return;
				}
				// map new descriptor
				THandle< CNodeClassDescriptor > descriptorPtr = CreateHandle< CNodeClassDescriptor >();
				CNodeClassDescriptor& desc = *descriptorPtr.Get();

				desc.m_isAbstract = classType.IsAbstract();
				desc.m_representationClassName = className;
				desc.m_descriptiveName = classType.GetDescriptiveName();

				// link new descriptor with structure
				if ( parentDescriptor )
				{
					parentDescriptor->m_childrenClasses.PushBack( descriptorPtr );
				}
				else
				{
					m_structure.m_rootclasses.PushBack( descriptorPtr );
				}

				nodes.Insert( className, descriptorPtr );

				// recursive call
				{
					Uint32 subClassesCount = m_subClasses.Size();
					GetRttiSystem().EnumDerivedClasses( &classType, m_subClasses );

					for ( Uint32 i = subClassesCount, n = m_subClasses.Size(); i != n; ++i )
					{
						MapClassRec( *m_subClasses[ i ], &desc );
					}

					m_subClasses.Resize( subClassesCount );
				}
			}
		};

		CNodeClassesDescriptorStructure& structure = *m_structure.Get();
		Local local( structure );

		if ( structure.m_representationToDescriptor.Find( rootClassType.GetName() ) != structure.m_representationToDescriptor.End() )
		{
			// already mapped
			return false;
		}

		local.MapClassRec( rootClassType );

		return true;
	}

	Bool CNodeClassesDescriptorStructure::Builder::MarkRttiRepresentationClass( const rtti::ClassType& representationClassType, CName descriptorClassName )
	{
		CNodeClassesDescriptorStructure& structure = *m_structure.Get();

		auto itFind = structure.m_representationToDescriptor.Find( representationClassType.GetName() );
		if ( itFind == structure.m_representationToDescriptor.End() )
		{
			return false;
		}


		itFind.Value()->m_descriptorClassName = descriptorClassName;
		return true;
	}

	Bool CNodeClassesDescriptorStructure::Builder::MarkRttiRepresentationClassDisabled( const rtti::ClassType& representationClassType )
	{
		CNodeClassesDescriptorStructure& structure = *m_structure.Get();

		auto itFind = structure.m_representationToDescriptor.Find( representationClassType.GetName() );
		if ( itFind == structure.m_representationToDescriptor.End() )
		{
			return false;
		}

		CNodeClassDescriptor* desc = itFind.Value().Get();
		desc->m_isDisabled = true;
		desc->m_isDisabledAsBranch = true;
		return true;
	}

	void CNodeClassesDescriptorStructure::Builder::SpreadMarking()
	{
		struct Local
		{
			static void SpreadMarkingRec( CNodeClassDescriptor& descriptor, CName descriptorClassName = CName::NONE(), Bool disableRecurisvely = false )
			{
				if ( !descriptor.m_descriptorClassName.Empty() )
				{
					descriptorClassName = descriptor.m_descriptorClassName;
					disableRecurisvely = false;
				}
				else if ( descriptor.m_isDisabledAsBranch )
				{
					disableRecurisvely = true;
					descriptor.m_isDisabled = true;
				}
				else if ( disableRecurisvely )
				{
					descriptor.m_isDisabled = true;
				}
				else
				{
					descriptor.m_descriptorClassName = descriptorClassName;
				}

				for ( const CNodeClassDescriptor::NodePtr& ptr : descriptor.m_childrenClasses )
				{
					SpreadMarkingRec( *ptr.Get(), descriptorClassName, disableRecurisvely );
				}
			}
		};

		for ( const THandle< CNodeClassDescriptor >& rootNode : m_structure->m_rootclasses )
		{
			Local::SpreadMarkingRec( *rootNode.Get() );
		}
	}

	CNodeClassesDescriptorStructure::CNodeClassesDescriptorStructure()
		: m_representationToDescriptor{ red::PoolBackend() }
		, m_rootclasses{ red::PoolBackend() }
	{
	}

	CNodeClassesDescriptorStructure::~CNodeClassesDescriptorStructure()
	{

	}
}