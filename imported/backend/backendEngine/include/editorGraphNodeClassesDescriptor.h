/*
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../../common/redReflection/include/backendData.h"

namespace tools
{
	class CNodeClassesDescriptorStructure;

	class BACKEND_ENGINE_API CNodeClassDescriptor : public IBackendData
	{
		friend CNodeClassesDescriptorStructure;
		RTTI_DECLARE_TYPE( CNodeClassDescriptor )
	public:
		typedef THandle< CNodeClassDescriptor > NodePtr;
		typedef red::DynArray< NodePtr > NodeList;

		typedef Uint32 NodeClassId;
		static const NodeClassId INVALID_NODE_CLASS_ID = 0xffffffff;

		CNodeClassDescriptor()
			: m_isAbstract( false )
			, m_isDisabled( false )
			, m_isProcessed( false )
			, m_childrenClasses{ red::PoolBackend() }
		{
		}

		const Bool IsAbstract() const { return m_isAbstract; }
		const Bool IsDisabled() const { return m_isDisabled; }

		const NodeList& GetChildren() { return m_childrenClasses; }
		const String& GetName() const { return m_descriptiveName; }
		const CName& GetClassName() const { return m_representationClassName; }

	protected:
		NodeList m_childrenClasses;

		Bool m_isAbstract;
		Bool m_isDisabled;
		Bool m_isDisabledAsBranch;
		Bool m_isProcessed;	// Internal, not-a-property.

		CName m_descriptorClassName;
		CName m_representationClassName;

		red::String m_descriptiveName;
	};

	class BACKEND_ENGINE_API CNodeClassesDescriptorStructure : public IBackendData
	{
	public:
		RTTI_DECLARE_TYPE( CNodeClassesDescriptorStructure )

		CNodeClassesDescriptorStructure();
		~CNodeClassesDescriptorStructure();
		
		// Maps rtti class hierarchy, creating descriptor for every rtti class in.
		class BACKEND_ENGINE_API Builder
		{
		public:
			Builder( CNodeClassesDescriptorStructure& structure );
			~Builder();

			Bool MapRttiClassHierarchy( const rtti::ClassType& rootClassType );
			Bool MarkRttiRepresentationClass( const rtti::ClassType& representationClassType, CName descriptorClassName );
			Bool MarkRttiRepresentationClassDisabled( const rtti::ClassType& representationClassType );

			void SpreadMarking();
		protected:
			THandle< CNodeClassesDescriptorStructure >	m_structure;
		};
		
		const red::DynArray< THandle< CNodeClassDescriptor > >& GetRoots() { return m_rootclasses; }
		
	protected:
		typedef red::HashMap< CName, THandle< CNodeClassDescriptor > > NodeCollection;
		NodeCollection m_representationToDescriptor;
		red::DynArray< THandle< CNodeClassDescriptor > > m_rootclasses;
	};	
}
