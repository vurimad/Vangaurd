#include "build.h"

#include "../../../common/redConfig/include/configVarSystem.h"
#include "../../../common/redConfig/include/configVarRegistry.h"
#include "../../../common/interop/include/interopBase.h"
#include "../../../common/interop/include/interopNotifier.h"
#include "../../../common/interop/include/interopUtilsManual.h"

#include "configBackend.h"
#include "dataControlBackend.h"
#include "../../common/generalEditorBackend/include/interopHierarchyWrappers.h"

using red::DynArray;

namespace tools
{
	class DataControlInterop;
}

namespace Config
{
	class ConfigGroupProxy;

	static const CName FloatRanged = RED_NAME_CONSTEXPR("Float:Ranged");
	static const CName Int32Ranged = RED_NAME_CONSTEXPR("Int32:Ranged");

	enum ProxyType : Uint32
	{
		Var,
		Group
	};

	class SharedConfigData
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		proxy::GenericId GetIdByItem( const interop::IBaseObject* item ) const;
		interop::IBaseObject* GetItemById( const proxy::GenericId id ) const;

		void CollectBranchIds( proxy::ItemHierarchy& hierarchy, const ConfigGroupProxy* groupProxy );

		proxy::GenericId RegisterItem( interop::IBaseObject* item, ProxyType type );
		void UnregisterItem( interop::IBaseObject* item );

		void NotifyItemChanged( const interop::IBaseObject* item );
		red::DynArray< red::FixedSizeFunction< void( proxy::GenericId ) > > OnItemChanged;

	private:
		red::HashMap< proxy::GenericId, interop::IBaseObject* > m_idToItem;
		red::HashMap< interop::IBaseObject*, proxy::GenericId > m_itemToId;

		red::Atomic< Uint64 > m_nextId;
	};

	class ConfigDataControl : public tools::DataControlBackend
	{
	public:
		ConfigDataControl( const proxy::GenericId rootId, const red::SharedPtr< SharedConfigData >& sharedData );

		virtual proxy::GenericId GetRootId() const;

		virtual proxy::ItemHierarchy GetBranchIds( const proxy::GenericId branchRootId ) const override;
		virtual red::DynArray< proxy::ItemData > GetItemsData( const red::DynArray< proxy::GenericId >& itemsIds ) const override;

	private:
		proxy::GenericId m_rootId;
		red::SharedPtr< SharedConfigData > m_sharedData;
	};

	class ConfigVarProxy : public interop::IValueObject
	{
		friend class ConfigGroupProxy;
		IConfigVar* m_var;

		CName m_name;
		CName m_category;

		red::SharedPtr< SharedConfigData > m_sharedData;

	public:
		ConfigVarProxy( IConfigVar* var, CName name, CName category, const red::SharedPtr< SharedConfigData >& sharedData );
		virtual ~ConfigVarProxy();

		void OnChanged() const;
		void OnReset();

		virtual void Get( interop::IContext& ctx, interop::InteropString& outValue ) const override;
		virtual void Set( interop::IContext& ctx, const interop::InteropString& newValue ) const override;

		virtual const CName GetType() const override;
		virtual const Bool IsReadOnly() const override;
		virtual const Uint8 IsDefault() const override;

		virtual interop::ObjectPtr GetUnlistedChild( interop::IContext& ctx, const String& name ) const override;

		const CName GetName()	  const;
		const CName GetCategory() const;
	
	};

	class ConfigVarGlobalNotifierProxy : public IConfigVarGlobalNotifier
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

		interop::ObjectPtr m_root;
	public:

		ConfigVarGlobalNotifierProxy(interop::ObjectPtr root);
		virtual void OnChanged( IConfigVar* changedVar ) override final;
	};

	class ConfigGroupProxy : public ::interop::ICompoundObject
	{
		void InitializeDataControl();

		typedef red::SharedPtr<ConfigGroupProxy>	ConfigGroupProxyPtr;
		typedef red::SharedPtr<ConfigVarProxy>		ConfigVarProxyPtr;

		typedef red::StringView						StringView;
		typedef red::DynArray<ConfigGroupProxyPtr>	GroupProxyArray;
		typedef red::DynArray<ConfigVarProxyPtr  >	VarProxyArray;

		GroupProxyArray m_groups;
		VarProxyArray	m_vars;

		CName			m_name;
		CName			m_category;

		red::SharedPtr< SharedConfigData > m_sharedData;
		red::SharedPtr< ConfigDataControl > m_dataControl;
		red::UniquePtr< tools::DataControlInterop > m_dataControlInterop;

	public:
		ConfigGroupProxy( CName name, CName category, const red::SharedPtr< SharedConfigData >& sharedData );
		ConfigGroupProxy( const DynArray< IConfigVar* >& vars, CName name, CName category, const red::SharedPtr< SharedConfigData >& sharedData );
		virtual ~ConfigGroupProxy();

		ConfigVarProxyPtr FindProxyForVar( IConfigVar* var);

		virtual void GetChildrenInfo( ::interop::IContext& ctx, red::DynArray< ::interop::ChildWithCategory >& outInfo ) const override final;
		virtual ::interop::ObjectPtr GetChild( ::interop::IContext& ctx, CName name ) const override final;
		virtual ::interop::ObjectPtr GetUnlistedChild( interop::IContext& ctx, const String& name ) const override final;
		virtual const CName GetType() const;

		const CName GetName()	  const;
		const CName GetCategory() const;

		const GroupProxyArray& GetGroups() const { return m_groups; }
		const VarProxyArray& GetVars() const { return m_vars; }

		friend class ConfigVarGlobalNotifierProxy;
	};

	ConfigGroupProxy::ConfigGroupProxy( CName name, CName category, const red::SharedPtr< SharedConfigData >& sharedData )
		: m_name( name )
		, m_category( category )
		, m_sharedData( sharedData )
	{
		InitializeDataControl();
	}

	ConfigGroupProxy::ConfigGroupProxy( const DynArray< IConfigVar* >& vars, CName name, CName category, const red::SharedPtr< SharedConfigData >& sharedData )
		: m_name( name )
		, m_category( category )
		, m_sharedData( sharedData )
	{
		InitializeDataControl();

		struct Helper
		{
			static ConfigGroupProxyPtr GetOrCreateGroup( GroupProxyArray& groups, CName name, const red::SharedPtr< SharedConfigData >& sharedData )
			{
				for( auto group : groups )
				{
					if( group->GetName() == name )
						return group;
				}

				ConfigGroupProxyPtr group = red::CreateSharedPtr<ConfigGroupProxy>(name, CName(nullptr), sharedData);
				groups.PushBack(group);
				return group;
			}

			static void AddVarToGroup( IConfigVar* var, StringView name, GroupProxyArray& groups, const red::SharedPtr< SharedConfigData >& sharedData )
			{
				StringView groupPath = var->GetGroup();

				if( !name.Empty() && groupPath.Find(name) != StringView::npos )
				{
					groupPath = groupPath.SubView( groupPath.Find(name) );
				}

				if( groupPath.Find('/') == StringView::npos )
				{
					CName groupName = RED_NAME( groupPath );
					for( auto group : groups )
					{
						if( group->GetName() == groupName )
						{
							group->m_vars.PushBack( red::CreateSharedPtr<ConfigVarProxy>( var, RED_NAME( var->GetName() ), CName( nullptr ), sharedData ) );
							return;
						}
					}

					ConfigGroupProxyPtr group = red::CreateSharedPtr<ConfigGroupProxy>( groupName, CName( nullptr ), sharedData );
					group->m_vars.PushBack( red::CreateSharedPtr<ConfigVarProxy>( var, RED_NAME( var->GetName() ), CName( nullptr ), sharedData ) );
					groups.PushBack(group);
				}
				else
				{
					CName groupName = RED_NAME( groupPath.SubView( 0, groupPath.Find('/') ) );
					groupPath = groupPath.SubView( groupPath.Find('/') + 1 );

					ConfigGroupProxyPtr group = GetOrCreateGroup(groups, groupName, sharedData);
					AddVarToGroup(var, groupPath, group->m_groups, sharedData);
				}
			}

			static void SortLexicographically( ConfigGroupProxy& group )
			{
				auto sort_vars_predicate = []( ConfigVarProxyPtr a, ConfigVarProxyPtr b)
				{ 
					return red::Strcmp( a->m_name.AsChar(), b->m_name.AsChar() ) < 0;
				};

				auto sort_groups_predicate = []( ConfigGroupProxyPtr a, ConfigGroupProxyPtr b)
				{ 
					return red::Strcmp( a->m_name.AsChar(), b->m_name.AsChar() ) < 0;
				};

				std::sort(group.m_groups.Begin(), group.m_groups.End(), sort_groups_predicate);
				std::sort(group.m_vars.Begin(), group.m_vars.End(), sort_vars_predicate);

				for( auto& group : group.m_groups )
				{
					SortLexicographically(*group.Get());
				}
			}
		};

		for ( IConfigVar* var : vars )
		{
			Helper::AddVarToGroup(var, StringView(nullptr), m_groups, m_sharedData);
		}

		Helper::SortLexicographically(*this);
	}

	ConfigGroupProxy::~ConfigGroupProxy()
	{
		m_sharedData->UnregisterItem( this );
	}

	void ConfigGroupProxy::InitializeDataControl()
	{
		const proxy::GenericId thisId = m_sharedData->RegisterItem( this, ProxyType::Group );

		m_dataControl = red::CreateSharedPtr< ConfigDataControl >( thisId, m_sharedData );
		m_dataControlInterop = red::CreateUniquePtr< tools::DataControlInterop >( m_dataControl );
	}

	ConfigGroupProxy::ConfigVarProxyPtr ConfigGroupProxy::FindProxyForVar( IConfigVar* var )
	{
		for ( auto elem : m_vars )
		{
			if ( elem->m_var == var )
				return elem;
		}

		for ( const auto group : m_groups )
		{
			auto childProxy = group->FindProxyForVar(var);
			if ( childProxy != nullptr )
			{
				return childProxy;
			}
		}

		// not found
		return nullptr;
	}

	void ConfigGroupProxy::GetChildrenInfo( ::interop::IContext& ctx, red::DynArray< ::interop::ChildWithCategory >& outInfo ) const
	{
		outInfo.Reserve(m_groups.Size());

		for( auto group : m_groups )
		{
			outInfo.PushBack( ::interop::ChildWithCategory(group->m_name, group->m_category) );
		}

		for( auto var : m_vars )
		{
			outInfo.PushBack( ::interop::ChildWithCategory(var->m_name, CName()) );
		}

	}

	::interop::ObjectPtr ConfigGroupProxy::GetChild( ::interop::IContext& ctx, CName name ) const
	{
		for( auto var : m_vars )
		{
			if( var->GetName() == name )
			{
				return ::interop::ObjectPtr(var);
			}
		}

		for( auto group : m_groups )
		{
			if( group->GetName() == name )
				return ::interop::ObjectPtr(group);
		}

		return ::interop::ObjectPtr();
	}

	::interop::ObjectPtr ConfigGroupProxy::GetUnlistedChild( interop::IContext& ctx, const String& name ) const
	{
		if ( m_dataControlInterop && name == "_data" )
		{
			return interop::ObjectPtr( m_dataControlInterop->CreateInterop() );
		}

		return interop::ObjectPtr();
	}

	const CName ConfigGroupProxy::GetType() const
	{
		return GetTypeName<ConfigGroupProxy>();
	}

	const CName ConfigGroupProxy::GetName() const
	{
		return m_name;
	}

	const CName ConfigGroupProxy::GetCategory() const
	{
		return m_category;
	}

	ConfigVarGlobalNotifierProxy::ConfigVarGlobalNotifierProxy(::interop::ObjectPtr root)
	: m_root(root)
	{
	}

	void ConfigVarGlobalNotifierProxy::OnChanged( IConfigVar* changedVar )
	{
		typedef ConfigGroupProxy::ConfigVarProxyPtr	  ConfigVarProxyPtr;
		typedef ConfigGroupProxy::ConfigGroupProxyPtr ConfigGroupProxyPtr;

		ConfigVarProxyPtr proxy = red::StaticCast<ConfigGroupProxy>(m_root)->FindProxyForVar(changedVar);

		if ( proxy )
		{
			proxy->OnChanged();
		}
	}

	ConfigVarProxy::ConfigVarProxy( IConfigVar* var, CName name, CName category, const red::SharedPtr< SharedConfigData >& sharedData )
		: m_var( var )
		, m_name( name )
		, m_category( category )
		, m_sharedData( sharedData )
	{
		m_sharedData->RegisterItem( this, ProxyType::Var );
	}

	ConfigVarProxy::~ConfigVarProxy()
	{
		m_sharedData->UnregisterItem( this );
	}

	void ConfigVarProxy::OnChanged() const
	{
		m_sharedData->NotifyItemChanged( this );
	}

	void ConfigVarProxy::OnReset()
	{
		// to implement
	}

	const CName ConfigVarProxy::GetType() const
	{
		switch ( m_var->GetType() )
		{
			case EConfigVarType::eConsoleVarType_Bool: 
				return GetTypeName<Bool>(); 

			case EConfigVarType::eConsoleVarType_Float:
				return m_var->IsRanged() ? FloatRanged : GetTypeName<Float>();

			case EConfigVarType::eConsoleVarType_Int:
				return m_var->IsRanged() ? Int32Ranged : GetTypeName<Int32>();

			case EConfigVarType::eConsoleVarType_String: 
				return GetTypeName<String>();
				
			case EConfigVarType::eConsoleVarType_Color:
				return GetTypeName<Color>();

			default:
				return CName();
		}
	}
 
	void ConfigVarProxy::Get( ::interop::IContext& ctx, ::interop::InteropString& outValue ) const
	{
		if ( m_var->GetType() == EConfigVarType::eConsoleVarType_Color )
		{
			Color color;
			if ( m_var->GetRaw( &color, EConfigVarType::eConsoleVarType_Color ) )
			{
				rtti::ValueHolderPtr value;
				if ( color.GetClass()->ReadDirectValue( IRTTIContext::GetDefault(), &color, value ) )
				{
					outValue = value->ToString();
					return;
				}
			}
			ctx.ThrowException( String::Printf( "Cannot read Color value (%hs)", m_name.AsChar() ).AsChar() );
		}
		else if ( !m_var->GetText( outValue ) )
			ctx.ThrowException( "Unable to read config var" );
	}

	void ConfigVarProxy::Set( ::interop::IContext& ctx, const ::interop::InteropString& newValue ) const
	{
		if ( m_var->GetType() == EConfigVarType::eConsoleVarType_Color )
		{
			Color color;
			rtti::ValueHolderPtr value = rtti::ValueHolder::Parse( newValue.AsChar() );

			if ( value && color.GetClass()->WriteDirectValue( IRTTIContext::GetDefault(), &color, *value, false ) )
			{
				if ( !m_var->SetRaw( &color, EConfigVarType::eConsoleVarType_Color ) )
				{
					ctx.ThrowException( "Unable to write config var" );
				}
			}
			else
			{
				ctx.ThrowException( String::Printf( "Cannot parse %hs to Color object", newValue.AsChar() ).AsChar() );
			}
		}
		else if ( !m_var->SetText( newValue ) )
			ctx.ThrowException( "Unable to write config var" );

		if ( !ctx.HasErrors() )
		{
			OnChanged();
			GetConfigSystem().Save();
		}
	}

	const Bool ConfigVarProxy::IsReadOnly() const
	{
		return m_var->HasFlag( Config::eConsoleVarFlag_ReadOnly );
	}

	const Uint8 ConfigVarProxy::IsDefault() const
	{
		return 0;
	}

	::interop::ObjectPtr ConfigVarProxy::GetUnlistedChild( ::interop::IContext& ctx, const String& name ) const
	{
		if ( name == "_min" )
		{
			switch( m_var->GetType() )
			{
				case EConfigVarType::eConsoleVarType_Float:
				{
					Float min = RED_FLT_MAX;
					if( m_var->GetRawMin(&min, eConsoleVarType_Float) )
						return ::interop::ObjectPtr( RED_NEW( ::interop::utils::ConstValueObject<Float> )( min ) );
				}

				case EConfigVarType::eConsoleVarType_Int:
				{
					Int32 min = INT_MAX;
					if( m_var->GetRawMin(&min, eConsoleVarType_Int) )
						return ::interop::ObjectPtr( RED_NEW( ::interop::utils::ConstValueObject<Int32> )( min ) );
				}
			}
		}
		else if ( name == "_max" )
		{
			switch( m_var->GetType() )
			{
				case EConfigVarType::eConsoleVarType_Float:
				{
					Float max = RED_FLT_MAX;
					if( m_var->GetRawMax(&max, eConsoleVarType_Float) )
						return ::interop::ObjectPtr( RED_NEW( ::interop::utils::ConstValueObject<Float> )( max ) );
				}

				case EConfigVarType::eConsoleVarType_Int:
				{
					Int32 max = INT_MAX;
					if( m_var->GetRawMax(&max, eConsoleVarType_Int) )
						return ::interop::ObjectPtr( RED_NEW( ::interop::utils::ConstValueObject<Int32> )( max ) );
				}
			}
		}
		else if ( name == "_useSlider" )
		{
			return ::interop::ObjectPtr( RED_NEW( ::interop::utils::ConstValueObject<Bool> )( true ) );
		}

		return ::interop::ObjectPtr();
	}

	const CName ConfigVarProxy::GetName() const
	{
		return m_name;
	}

	const CName ConfigVarProxy::GetCategory() const
	{
		return m_category;
	}

	ConfigBackend::ConfigBackend()
	{
	}

	ConfigBackend::~ConfigBackend()
	{
		IConfigVar::UnRegisterGlobalNotifier( m_notifier );
		RED_DELETE(m_notifier);
	}

	::interop::ObjectPtr ConfigBackend::CreateInterop()
	{
		if( m_root == nullptr )
		{
			red::DynArray<IConfigVar*> outVars;
			Config::GetConfigSystem().GetRegistry().EnumVars(outVars);

			m_sharedData = red::CreateSharedPtr< SharedConfigData >();
			m_root = red::CreateSharedPtr<ConfigGroupProxy>( outVars, CName(nullptr), CName(nullptr), m_sharedData );
			m_notifier = RED_NEW(ConfigVarGlobalNotifierProxy)( m_root );
			IConfigVar::RegisterGlobalNotifier( m_notifier );
		}

		return m_root;
	}

	RTTI_DECLARE_TYPE_NAME(ConfigGroupProxy);

	//////////////////////////////////////////////////////////////////////////

	proxy::GenericId SharedConfigData::GetIdByItem( const interop::IBaseObject* item ) const
	{
		auto it = m_itemToId.Find( item );
		return it != m_itemToId.End() ? it.Value() : proxy::GenericId();
	}

	interop::IBaseObject* SharedConfigData::GetItemById( const proxy::GenericId id ) const
	{
		auto it = m_idToItem.Find( id );
		return it != m_idToItem.End() ? it.Value() : nullptr;
	}

	void SharedConfigData::CollectBranchIds( proxy::ItemHierarchy& hierarchy, const ConfigGroupProxy* groupProxy )
	{
		// IMPORTANT: keep in mind this has to have the same order as GetChildrenInfo
		hierarchy.m_children.Reserve( groupProxy->GetGroups().Size() + groupProxy->GetVars().Size() );
		for ( const red::SharedPtr<ConfigGroupProxy>& group : groupProxy->GetGroups() )
		{
			proxy::ItemHierarchy child;
			child.m_itemId = GetIdByItem( group.Get() );
			CollectBranchIds( child, group.Get() );
			hierarchy.m_children.PushBack( child );
		}

		for ( const red::SharedPtr<ConfigVarProxy>& var : groupProxy->GetVars() )
		{
			proxy::ItemHierarchy child;
			child.m_itemId = GetIdByItem( var.Get() );
			hierarchy.m_children.PushBack( child );
		}
	}

	proxy::GenericId SharedConfigData::RegisterItem( interop::IBaseObject* item, ProxyType type )
	{
		proxy::GenericId newId( m_nextId.PostIncrement(), type );
		m_idToItem.Insert( newId, item );
		m_itemToId.Insert( item, newId );

		return newId;
	}

	void SharedConfigData::UnregisterItem( interop::IBaseObject* item )
	{
		auto it = m_itemToId.Find( item );
		RED_ASSERT( it != m_itemToId.End() );

		proxy::GenericId id = it.Value();

		m_itemToId.Remove( it );
		m_idToItem.Remove( id );
	}

	void SharedConfigData::NotifyItemChanged( const interop::IBaseObject* item )
	{
		for ( auto func : OnItemChanged )
		{
			proxy::GenericId itemId = GetIdByItem( item );
			func( itemId );
		}
	}

	//////////////////////////////////////////////////////////////////////////

	ConfigDataControl::ConfigDataControl( const proxy::GenericId rootId, const red::SharedPtr< SharedConfigData >& sharedData )
		: m_rootId( rootId )
		, m_sharedData( sharedData )
	{
		m_sharedData->OnItemChanged.PushBack( [this] ( proxy::GenericId id )
		{
			NotifyItemChanged( id );
		} );
	}

	proxy::GenericId ConfigDataControl::GetRootId() const
	{
		return m_rootId;
	}

	proxy::ItemHierarchy ConfigDataControl::GetBranchIds( const proxy::GenericId branchRootId ) const
	{
		proxy::ItemHierarchy hierarchy;
		hierarchy.m_itemId = branchRootId;

		if ( branchRootId.GetType() == ProxyType::Group )
		{
			ConfigGroupProxy* groupProxy = static_cast<ConfigGroupProxy*>( m_sharedData->GetItemById( branchRootId ) );
			m_sharedData->CollectBranchIds( hierarchy, groupProxy );
		}

		return hierarchy;
	}

	red::DynArray< proxy::ItemData > ConfigDataControl::GetItemsData( const red::DynArray< proxy::GenericId >& itemsIds ) const
	{
		red::DynArray< proxy::ItemData > data;
		data.Reserve( itemsIds.Size() );

		for ( const proxy::GenericId id : itemsIds )
		{
			interop::IBaseObject* item = m_sharedData->GetItemById( id );
			if ( item )
			{
				proxy::ItemData itemData;
				itemData.m_itemId = id;
				tools::ExtractInteropData( item, itemData );

				// TODO: something safer
				THandle< tools::BaseObjectData > baseData = Cast< tools::BaseObjectData >( itemData.m_data.Front() );
				RED_ASSERT( baseData );

				if ( id.GetType() == ProxyType::Var )
				{
					ConfigVarProxy* varProxy = static_cast<ConfigVarProxy*>( item );
					baseData->m_nameAndCategoryInfo.m_name = varProxy->GetName();
					baseData->m_nameAndCategoryInfo.m_category = varProxy->GetCategory();
				}
				else
				{
					ConfigGroupProxy* groupProxy = static_cast<ConfigGroupProxy*>( item );
					baseData->m_nameAndCategoryInfo.m_name = groupProxy->GetName();
					baseData->m_nameAndCategoryInfo.m_category = groupProxy->GetCategory();
				}

				data.PushBack( itemData );
			}
		}
		return data;
	}
}

RTTI_DECLARE_ENUM_IN_NAMESPACE( ProxyType, Config );

RTTI_BEGIN_ENUM_IN_NAMESPACE( ProxyType, Config );
	RTTI_ENUM_OPTION( Var );
	RTTI_ENUM_OPTION( Group );
RTTI_END_ENUM();