/**
* Copyright (c) 2015-2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "editableCollection.h"
#include "editableCollectionCustomPropertyEditor.h"
#include "editableCollectionInterop.h"
#include "../../../common/redReflection/include/editableView.h"
#include "../../../common/redReflection/include/serializableMap.h"
#include "../../../common/world/include/worldNode.h"
#include "../../../common/world/include/worldNodeEditorData.h"
#include "../../../common/issueReporter/include/jiraDataStruct.h"
#include "../../../common/redReflection/include/rttiValueHolder.h"
#include "../../../common/interop/include/interopBase.h"
#include "../../../common/redReflection/include/weakHandle.h"
#include "resolvedView.h"
#include "bugFlagsService.h"

namespace tools
{

	//----------------

	namespace helper
	{
		static red::DynArray< const rtti::ClassType* > EnumerateCustomEditorFactoryTypes()
		{
			red::DynArray< const rtti::ClassType* > customEditorFactoryClasses{ red::PoolBackend() };
			GetRttiSystem().EnumClasses( ClassID< IEditableCustomProperty >(), customEditorFactoryClasses );
			return customEditorFactoryClasses;
		}
	}

	IEditableCustomProperty* IEditableCustomProperty::CreateCustomEditor( const CName customPropertyType, EditableCollection* collection, prv::EditableCollectionRootItem* root, const rtti::AccessPath& pathSoFar, const rtti::IType* type, const Bool isInlined, const Bool isReadOnly )
	{
		static const red::DynArray< const rtti::ClassType* > customEditorFactoryClasses = helper::EnumerateCustomEditorFactoryTypes();

		for( const auto* classPtr : customEditorFactoryClasses )
		{
			const auto* factoryPtr = classPtr->GetDefaultObject<IEditableCustomProperty>();
			if( factoryPtr->HandlesCustomType( customPropertyType, root ) )
			{
				auto* factory = classPtr->CreateObject<IEditableCustomProperty>();
				factory->Initialize( customPropertyType, collection, root, pathSoFar, type, isInlined, isReadOnly );
				return factory;
			}
		}

		return nullptr;
	}

	Bool IEditableCustomProperty::HasOptions() const
	{
		return false;
	}

	const red::DynArray<String>& IEditableCustomProperty::GetOptions() const
	{
		static red::DynArray<String> empty{ red::PoolBackend() };
		return empty;
	}

	Bool IEditableCustomProperty::GetChildrenInfo(interop::IContext& ctx, red::DynArray< interop::ChildWithCategory >& outInfo) const
	{
		return false;
	}

	Bool IEditableCustomProperty::HasUnlistedChildren() const
	{
		return false;
	}

	interop::ObjectPtr IEditableCustomProperty::GetUnlistedChild( interop::IContext& ctx, const String& name ) const
	{
		return interop::ObjectPtr();
	}

	interop::ObjectPtr IEditableCustomProperty::GetChild( interop::IContext& ctx, const CName& name ) const
	{
		return interop::ObjectPtr();
	}

	void IEditableCustomProperty::Initialize( const CName customPropertyType, EditableCollection* collection, prv::EditableCollectionRootItem* root, const rtti::AccessPath& pathSoFar, const rtti::IType* type, const Bool isInlined, const Bool isReadOnly )
	{
		m_customType = customPropertyType;
		m_collection = collection;
		m_root = root;
		m_pathSoFar = pathSoFar;
		m_type = type;
		m_isInlined = isInlined;
		m_isReadOnly = isReadOnly;
	}

	Bool IEditableCustomProperty::CanReset() const
	{
		return true;
	}

	Bool IEditableCustomProperty::GetValue( IRTTIContext& ctx, const rtti::AccessPath& path, red::SharedPtr<rtti::ValueHolder>& outValue ) const
	{
		return m_root->ReadValue( ctx, path, outValue );
	}

	Bool IEditableCustomProperty::SetValue( IRTTIContext& ctx, const rtti::AccessPath& path, const red::SharedPtr<rtti::ValueHolder>& newValue ) const
	{
		if( m_isReadOnly )
			return ctx.ReportError( m_type, "Trying to write to a read only value" );

		return m_root->WriteValue( ctx, path, newValue );
	}

	void IEditableCustomProperty::Rebind( const rtti::AccessPath& oldPath, const rtti::AccessPath& newPath )
	{
		String path = m_pathSoFar.ToString();
		path.Replace( oldPath.ToString(), newPath.ToString() );
		m_pathSoFar = rtti::AccessPath( path.AsChar() );
	}

	THandle< ISerializable > IEditableCustomProperty::FindParentObject( const rtti::ClassType* classType ) const
	{
		red::SharedPtr< tools::IEditableView > view = m_root->GetNegotiatedView();
		while( view != nullptr )
		{
			THandle<ISerializable> obj;
			if( GSerializableMap->FindSerializable( view->GetSerializableID(), obj ) == false )
				return nullptr;

			while( obj )
			{
				if( obj->IsA( classType ) )
					return obj;

				obj = HandleFromPtr( obj->GetParent() );
			}

			// not found in this view, try the parent view if specified
			view = view->GetParentView();
		}

		// really not found
		return nullptr;
	}

	THandle< ISerializable > IEditableCustomProperty::FindParentObject(ParentObjectVisitor& visitor) const
	{
		red::SharedPtr< tools::IEditableView > view = m_root->GetNegotiatedView();
		while( view != nullptr )
		{
			THandle<ISerializable> obj;
			if( GSerializableMap->FindSerializable( view->GetSerializableID(), obj ) == false )
				return nullptr;

			while( obj )
			{
				if( visitor.ProcessParentObject( obj ) )
					return obj;

				obj = HandleFromPtr( obj->GetParent() );
			}

			// not found in this view, try the parent view if specified
			view = view->GetParentView();
		}

		// really not found
		return nullptr;
	}

	const Bool IEditableCustomProperty::IsMultiValue() const
	{
		return m_root->IsMultiValue( m_pathSoFar );
	}

	//----------------

	/// selection components in the entity by name (binding)
	class CustomPropertyEditor_ClassName : public IEditableCustomProperty
	{
		RTTI_DECLARE_TYPE( CustomPropertyEditor_ClassName );

	public:
		CustomPropertyEditor_ClassName()
			: m_options{ red::PoolBackend() }
		{
		}

		virtual CName GetControlType() const
		{
			// NOTE: doesn't have to be the same as customTypeName, it's only for C#
			return RED_NAME( red::String::Printf( "%hs:Restricted", ::GetTypeName<CName>().AsChar() ).AsChar() );
		}

		virtual Bool HasOptions() const override final
		{
			return true;
		}

		virtual const red::DynArray<String>& GetOptions() const override final
		{
			if( m_options.Empty() )
				BuildOptionList( const_cast<red::DynArray< red::String >&>( m_options ) );

			return m_options;
		}

		virtual const Bool HandlesCustomType( const CName customTypeName, const prv::EditableCollectionRootItem* /*root*/ ) const override final
		{
			const auto name = red::String( customTypeName.AsChar() );
			return name.BeginsWith( "ClassName:" );
		}

		//----

		const rtti::ClassType* GetClassFilter() const
		{
			const auto name = red::String( GetCustomTypeName().AsChar() );
			const auto className = name.StringAfter( "ClassName:" );

			return GetRttiSystem().FindClass( RED_NAME_NOREG( className ) );
		}

		void BuildOptionList( red::DynArray< red::String >& outOptions ) const
		{
			outOptions.Clear();

			const auto* filterClass = GetClassFilter();
			if( nullptr != filterClass )
			{
				red::DynArray< const rtti::ClassType* > allClasses{ red::PoolBackend() };
				GetRttiSystem().EnumClasses( filterClass, allClasses );

				for( auto* classType : allClasses )
					outOptions.PushBack( classType->GetName().AsChar() );
			}
		}

	private:
		red::DynArray< String > m_options;
	};

	//---------------------------------------------------------------------
	class CustomPropertyEditor_NodeName : public IEditableCustomProperty
	{
		RTTI_DECLARE_TYPE( CustomPropertyEditor_NodeName );

	public:
		CustomPropertyEditor_NodeName()
		{
		}

		virtual CName GetControlType() const override
		{
			return s_customType;
		}
		
		virtual const Bool HandlesCustomType( const CName customTypeName, const prv::EditableCollectionRootItem* /*root*/ ) const override final
		{
			return s_customType == customTypeName;
		}

	private:
		static CName s_customType;
	};

	CName CustomPropertyEditor_NodeName::s_customType = RED_NAME_CONSTEXPR( "nodeName" );

	//---------------------------------------------------------------------
	class CustomPropertyEditor_GlobalNodeName: public IEditableCustomProperty
	{
		RTTI_DECLARE_TYPE( CustomPropertyEditor_GlobalNodeName );

	public:
		CustomPropertyEditor_GlobalNodeName()
		{
		}

		virtual void Initialize( const CName customPropertyType, EditableCollection* collection, prv::EditableCollectionRootItem* root, const rtti::AccessPath& pathSoFar, const rtti::IType* type, const Bool isInlined, const Bool isReadOnly ) override
		{
			IEditableCustomProperty::Initialize( customPropertyType, collection, root, pathSoFar, type, isInlined, isReadOnly );
			if ( THandle< world::Node > node = FindParentObject< world::Node >() )
			{
				m_defaultGlobalName = node->CalculateInstanceGlobalName();

				m_isLocked = node->GetEditorData().IsPrimaryGlobalNameLocked();

				m_node = node;
			}
		}

		virtual CName GetControlType() const override
		{
			return s_customType;
		}

		virtual const Bool HandlesCustomType( const CName customTypeName, const prv::EditableCollectionRootItem* /*root*/ ) const override final
		{
			return s_customType == customTypeName;
		}

		virtual Bool HasUnlistedChildren() const override
		{
			return true;
		}

		virtual interop::ObjectPtr GetUnlistedChild( interop::IContext& ctx, const String& name ) const override
		{
			using FunctorType = interop::utils::Functor1< void, Bool >;

			if ( name == "_watermark" )
			{
				return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject< String > )( m_defaultGlobalName ) );
			}
			else if ( name == "_isLocked" )
			{
				if ( THandle< world::Node > node = m_node.ToHandle() )
				{
					m_isLocked = node->GetEditorData().IsPrimaryGlobalNameLocked();
				}

				return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject< Bool > )( m_isLocked ) );
			}
			else if ( name == "_setLocked" )
			{
				return interop::ObjectPtr( RED_NEW( FunctorType )(
					[ this ]( interop::IContext& ctx, const Bool& value )
				{
					if ( THandle< world::Node > node = m_node.ToHandle() )
					{
						node->GetEditorData().SetPirmaryGlobalNameLocked( value );
					}
				}
				) );
			}

			return IEditableCustomProperty::GetUnlistedChild( ctx, name );
		}

	private:
		static CName s_customType;

		WeakHandle< world::Node > m_node;
		String m_defaultGlobalName = String::EMPTY();
		mutable Bool m_isLocked{ false };
	};

	CName CustomPropertyEditor_GlobalNodeName::s_customType = RED_NAME_CONSTEXPR( "globalNodeName" );


	//--------------------------------------------------------------------------------------------------------------------

	class JiraProject_CustomProperty : public IEditableCustomProperty
	{
		RTTI_DECLARE_TYPE( JiraProject_CustomProperty );

	public:
		virtual const Bool HandlesCustomType( const CName customTypeName, const prv::EditableCollectionRootItem* root ) const override
		{
			static const CName sJiraProjectCustomPropertyType = RED_NAME_CONSTEXPR( "jiraProject" );
			return sJiraProjectCustomPropertyType == customTypeName;
		}

		virtual Bool GetValue( IRTTIContext& ctx, const rtti::AccessPath& path, red::SharedPtr<rtti::ValueHolder>& outValue ) const override
		{
			return IEditableCustomProperty::GetValue( ctx, path[ "key" ], outValue );
		}

		virtual Bool SetValue( IRTTIContext& ctx, const rtti::AccessPath& path, const red::SharedPtr<rtti::ValueHolder>& newValue ) const override
		{
			return IEditableCustomProperty::SetValue( ctx, path[ "key" ], newValue );
		}

		virtual Bool HasOptions() const override
		{
			return true;
		}

		virtual const red::DynArray<String>& GetOptions() const override
		{
			return s_options;
		}

		virtual CName GetControlType() const override
		{
			static const CName sControlType = RED_NAME_CONSTEXPR( "CName:Restricted" );
			return sControlType;
		}

	private:
		static red::DynArray<String > s_options;
	};

	red::DynArray< String > JiraProject_CustomProperty::s_options{ "CYB", "CPO" };

	class JiraPriority_CustomProperty : public IEditableCustomProperty
	{
		RTTI_DECLARE_TYPE( JiraPriority_CustomProperty );

	public:
		virtual const Bool HandlesCustomType( const CName customTypeName, const prv::EditableCollectionRootItem* root ) const override
		{
			static const CName sJiraProjectCustomPropertyType = RED_NAME_CONSTEXPR( "jiraPriority" );
			return sJiraProjectCustomPropertyType == customTypeName;
		}

		virtual Bool GetValue( IRTTIContext& ctx, const rtti::AccessPath& path, red::SharedPtr<rtti::ValueHolder>& outValue ) const override
		{
			return IEditableCustomProperty::GetValue( ctx, path[ "name" ], outValue );
		}

		virtual Bool SetValue( IRTTIContext& ctx, const rtti::AccessPath& path, const red::SharedPtr<rtti::ValueHolder>& newValue ) const override
		{
			return IEditableCustomProperty::SetValue( ctx, path[ "name" ], newValue );
		}

		virtual Bool HasOptions() const override
		{
			return true;
		}

		virtual const red::DynArray<String>& GetOptions() const override
		{
			return s_options;
		}

		virtual CName GetControlType() const override
		{
			static const CName sControlType = RED_NAME_CONSTEXPR( "CName:Restricted" );
			return sControlType;
		}

	private:
		static red::DynArray<String > s_options;
	};

	red::DynArray< String > JiraPriority_CustomProperty::s_options{ "P0", "P1", "P2", "P3", "P4" };

	class JiraAssignee_CustomProperty : public IEditableCustomProperty
	{
		RTTI_DECLARE_TYPE( JiraAssignee_CustomProperty );

	public:
		virtual const Bool HandlesCustomType( const CName customTypeName, const prv::EditableCollectionRootItem* root ) const override
		{
			static const CName sJiraProjectCustomPropertyType = RED_NAME_CONSTEXPR( "jiraAssignee" );
			return sJiraProjectCustomPropertyType == customTypeName;
		}

		virtual Bool GetValue( IRTTIContext& ctx, const rtti::AccessPath& path, red::SharedPtr<rtti::ValueHolder>& outValue ) const override
		{
			return IEditableCustomProperty::GetValue( ctx, path["name"], outValue );
		}

		virtual Bool SetValue( IRTTIContext& ctx, const rtti::AccessPath& path, const red::SharedPtr<rtti::ValueHolder>& newValue ) const override
		{
			return IEditableCustomProperty::SetValue( ctx, path["name"], newValue );
		}

		virtual Bool HasOptions() const override
		{
			return true;
		}

		virtual const red::DynArray<String>& GetOptions() const override
		{
			return s_options;
		}

		virtual CName GetControlType() const override
		{
			static const CName sControlType = RED_NAME_CONSTEXPR( "CName:Restricted" );
			return sControlType;
		}

	private:
		static red::DynArray<String > s_options;
	};

	red::DynArray< String > JiraAssignee_CustomProperty::s_options{ "Committee", "CinematicsCommittee", "StoryCommittee" };

	class Screenshot_CustomProperty : public IEditableCustomProperty
	{
		RTTI_DECLARE_TYPE( Screenshot_CustomProperty );

	public:
		virtual const Bool HandlesCustomType( const CName customTypeName, const prv::EditableCollectionRootItem* root ) const override
		{
			return s_screenshotPropertyType == customTypeName;
		}

		virtual Bool SetValue( IRTTIContext& ctx, const rtti::AccessPath& path, const red::SharedPtr<rtti::ValueHolder>& newValue ) const override
		{
			THandle< ISerializable > userContext = GetRoot()->GetResolvedView()->GetNegotiatedView()->GetUserContext();
			if ( THandle< BugFlagItemContext > bugFlagCtx = Cast< BugFlagItemContext >( userContext ) )
			{
				red::AbsolutePath screenshotPath;
				bugFlagCtx->TakeScreenshot( screenshotPath );
				
				rtti::ValuePtr newPathValue = rtti::ValueHolder::Parse( screenshotPath.AsChar() );
				if ( newPathValue && IEditableCustomProperty::SetValue( ctx, path, newPathValue ) )
				{
					if ( THandle< ISerializable > parentItem = FindParentObject< ISerializable >() )
					{
						parentItem->NotifyPropertyChanged( path );
					}
					return true;
				}
			}
			return false;
		}

		virtual CName GetControlType() const override
		{
			return s_screenshotPropertyType;
		}

	private:
		static CName s_screenshotPropertyType;
	};
	CName Screenshot_CustomProperty::s_screenshotPropertyType = RED_NAME_CONSTEXPR( "screenshot" );

	class CameraLocation_CustomProperty : public Screenshot_CustomProperty
	{
		RTTI_DECLARE_TYPE( CameraLocation_CustomProperty );

	public:
		virtual const Bool HandlesCustomType( const CName customTypeName, const prv::EditableCollectionRootItem* root ) const override
		{
			static const CName sCameraLocationType = RED_NAME_CONSTEXPR( "cameraLocation" );
			return sCameraLocationType == customTypeName;
		}

		virtual Bool SetValue( IRTTIContext& ctx, const rtti::AccessPath& path, const red::SharedPtr<rtti::ValueHolder>& newValue ) const override
		{
			THandle< ISerializable > userContext = GetRoot()->GetResolvedView()->GetNegotiatedView()->GetUserContext();
			if ( THandle< BugFlagItemContext > bugFlagCtx = Cast< BugFlagItemContext >( userContext ) )
			{
				red::AbsolutePath screenshotPath;
				bugFlagCtx->TakeScreenshot( screenshotPath );

				rtti::ValuePtr newPathValue = rtti::ValueHolder::Parse( screenshotPath.AsChar() );
				if ( screenshotPath.IsFilePath() && IEditableCustomProperty::SetValue( ctx, path, newPathValue ) )
				{
					if ( THandle< JiraCreateIssueBody > parentItem = FindParentObject< JiraCreateIssueBody >() )
					{
						parentItem->m_fields.m_cameraLocation = bugFlagCtx->GetCurrentCameraLocation();
						parentItem->NotifyPropertyChanged( path );
						return true;
					}
					else if ( THandle< JiraEditIssueBody > parentItem = FindParentObject< JiraEditIssueBody >() )
					{
						parentItem->m_fields.m_cameraLocation = bugFlagCtx->GetCurrentCameraLocation();
						parentItem->NotifyPropertyChanged( path );
						return true;
					}
				}
			}
			return false;
		}
	};


	//---------------------------------------------------------------------
	class CustomPropertyEditor_FlatGlobalName: public IEditableCustomProperty
	{
		RTTI_DECLARE_TYPE( CustomPropertyEditor_FlatGlobalName );

	public:
		CustomPropertyEditor_FlatGlobalName()
		{
		}

		virtual void Initialize( const CName customPropertyType, EditableCollection* collection, prv::EditableCollectionRootItem* root, const rtti::AccessPath& pathSoFar, const rtti::IType* type, const Bool isInlined, const Bool isReadOnly ) override
		{
			IEditableCustomProperty::Initialize( customPropertyType, collection, root, pathSoFar, type, isInlined, isReadOnly );
			if ( THandle< world::Node > node = FindParentObject< world::Node >() )
			{
				m_defaultGlobalName = node->CalculateInstanceGlobalName();

				m_isLocked = node->GetEditorData().IsAlternativeGlobalNameLocked();
				m_node = node;
			}
		}

		virtual CName GetControlType() const override
		{
			return s_customType;
		}

		virtual const Bool HandlesCustomType( const CName customTypeName, const prv::EditableCollectionRootItem* /*root*/ ) const override final
		{
			return s_customType == customTypeName;
		}

		virtual Bool HasUnlistedChildren() const override
		{
			return true;
		}

		virtual interop::ObjectPtr GetUnlistedChild( interop::IContext& ctx, const String& name ) const override
		{
			using FunctorType = interop::utils::Functor1< void, Bool >;

			if ( name == "_isLocked" )
			{
				if ( THandle< world::Node > node = m_node.ToHandle() )
				{
					m_isLocked = node->GetEditorData().IsAlternativeGlobalNameLocked();
				}

				return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject< Bool > )( m_isLocked ) );
			}
			else if ( name == "_setLocked" )
			{
				return interop::ObjectPtr( RED_NEW( FunctorType )(
					[ this ]( interop::IContext& ctx, const Bool& value )
				{
					if ( THandle< world::Node > node = m_node.ToHandle() )
					{
						node->GetEditorData().SetAlternativeGlobalNameLocked( value );
					}
				}
				) );
			}

			return IEditableCustomProperty::GetUnlistedChild( ctx, name );
		}

	private:
		static CName s_customType;
		String m_defaultGlobalName = String::EMPTY();

		WeakHandle< world::Node > m_node;
		mutable Bool m_isLocked{ false };
	};

	CName CustomPropertyEditor_FlatGlobalName::s_customType = RED_NAME_CONSTEXPR( "flatGlobalName" );
	
} // tools


RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( IEditableCustomProperty, tools );
RTTI_END_TYPE();


RTTI_BEGIN_TYPE_IN_NAMESPACE( CustomPropertyEditor_ClassName, tools );
	RTTI_PARENT_TYPE( tools::IEditableCustomProperty );
RTTI_END_TYPE();


RTTI_BEGIN_TYPE_IN_NAMESPACE( CustomPropertyEditor_NodeName, tools );
	RTTI_PARENT_TYPE( tools::IEditableCustomProperty );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( CustomPropertyEditor_GlobalNodeName, tools );
	RTTI_PARENT_TYPE( tools::IEditableCustomProperty );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( JiraProject_CustomProperty, tools );
	RTTI_PARENT_TYPE( tools::IEditableCustomProperty );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( CustomPropertyEditor_FlatGlobalName, tools );
	RTTI_PARENT_TYPE( tools::IEditableCustomProperty );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( JiraPriority_CustomProperty, tools );
	RTTI_PARENT_TYPE( tools::IEditableCustomProperty );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( JiraAssignee_CustomProperty, tools );
	RTTI_PARENT_TYPE( tools::IEditableCustomProperty );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( CameraLocation_CustomProperty, tools );
	RTTI_PARENT_TYPE( tools::IEditableCustomProperty );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( Screenshot_CustomProperty, tools );
	RTTI_PARENT_TYPE( tools::IEditableCustomProperty );
RTTI_END_TYPE();


