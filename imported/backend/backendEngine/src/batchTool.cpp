#include "build.h"
#include "batchTool.h"

#include "objectCollection.h"
#include "editActionHistory.h"
#include "resourceBackend.h"

#include "../../../common/redReflection/include/resource.h"
#include "../../../common/interop/include/interopUtilsRTTI.h"
#include "../../../common/redReflection/include/editableView.h"
#include "../../../common/interop/include/interopBase.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMaterial/include/bitmapTexture.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/core/include/message.h"
#include "../../../common/redJobs2/include/jobBuilder.h"
#include "../../../common/worldEntities/include/entityTemplate.h"
#include "../../../common/resourceMaterial/include/multilayerMask.h"
#include "../../../common/redReflection/include/cloningUtils.h"

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( IResourceCustomPropertiesDescriptor, tools );
	RTTI_PARENT_TYPE( ISerializable );
RTTI_END_TYPE();


namespace tools
{
	ResourceBaseProperties::ResourceBaseProperties( const rtti::ClassType* resourceClass, tools::IEditActionHistory* actionHistory )
		: m_resourceTemplate( resourceClass->CreateHandle< CResource >() )
	{
		if ( actionHistory )
		{
			m_resourceView = RED_NEW( tools::ObjectCollection )( *actionHistory );
			m_resourceView->SetObject( m_resourceTemplate.Get() );
		}
	}

	void ResourceBaseProperties::Initialize( const red::DynArray<THandle<CResource>>& resources )
	{
		if ( m_resourceTemplate->IsA< CMesh >() )
		{
			Uint32 lodCount = 0;
			for ( const THandle< CResource >& res : resources )
			{
				THandle< CMesh > meshRes = Cast< CMesh >( res );
				if ( meshRes->GetNumberOfLODs() > lodCount )
				{
					m_resourceTemplate = Cast< CMesh >( CloneObject( meshRes ) );
					lodCount = meshRes->GetNumberOfLODs();
				}
			}

			if ( lodCount > 0 )
			{
				m_resourceView->SetObject( m_resourceTemplate.Get() );
			}
		}
	}

	const THandle< CResource >& ResourceBaseProperties::GetResourceTemplate() const
	{
		return m_resourceTemplate;
	}

	red::Map< CName, Bool >& ResourceBaseProperties::GetSwitches()
	{
		return m_propertiesSwitches;
	}

	void ResourceBaseProperties::ApplyChanges( const THandle<CResource>& resourceToEdit ) const
	{
		RED_FATAL_ASSERT( resourceToEdit->GetClass() == m_resourceTemplate->GetClass(), "Resource %hs is not of type %hs",
			resourceToEdit->GetPath().ToDebugString(), m_resourceTemplate->GetClass()->GetName().AsChar() );

		red::SharedPtr< IEditableView > resourceView = resourceToEdit->CreateView();
		red::SharedPtr< IEditableView > templateView = m_resourceTemplate->CreateView();

		for ( auto it = m_propertiesSwitches.Begin(); it != m_propertiesSwitches.End(); ++it )
		{
			rtti::AccessPath propertyPath( it.Key().AsChar() );
			rtti::ValueHolderPtr valueHolder;

			if ( it.Value() && templateView->GetValue( IRTTIContext::GetDefault(), propertyPath, valueHolder ) )
			{
				rtti::ValuePtr oldValue;
				resourceView->GetValue( IRTTIContext::GetDefault(), propertyPath, oldValue );
				resourceView->SetValue( IRTTIContext::GetDefault(), propertyPath, oldValue, valueHolder );
			}
		}
	}

	void ResourceBaseProperties::SetupInterop( ResourceBaseProperties* ptr, ::interop::utils::Object& op )
	{
		if ( ptr->m_resourceView )
		{
			op.RegisterObject( "properties", ptr->m_resourceView->GetRootInterop() );
		}

		op.RegisterMap( "_propertiesSwitches", [ptr] ( interop::IContext& ctx, const String& propertyPath )
		{
			const CName name = RED_NAME( propertyPath );
			if ( !ptr->m_propertiesSwitches.KeyExist( name ) )
			{
				ptr->m_propertiesSwitches.Insert( name, false );
			}
			return interop::utils::CreateRTTIAccessFunc( ctx, &ptr->m_propertiesSwitches[name] );
		} );
	}

	//--------------------------------------------------

	ResourceCustomProperties::ResourceCustomProperties( const THandle<IResourceCustomPropertiesDescriptor>& propertiesDesc, tools::IEditActionHistory* actionHistory )
		: m_customPropertiesDescriptor( propertiesDesc )
		, m_actionHistory( *actionHistory )
	{
		m_customPropertiesView = RED_NEW( tools::ObjectCollection )( *actionHistory );
		m_customPropertiesView->SetObject( propertiesDesc.Get() );
	}

	red::AbsolutePath ResourceCustomProperties::ApplyChanges( const job::RunContext& runContext ) const
	{
		m_customPropertiesDescriptor->ApplyChanges( runContext );
		return m_customPropertiesDescriptor->DumpMessages();
	}

	void ResourceCustomProperties::SetupInterop( ResourceCustomProperties* ptr, ::interop::utils::Object& op )
	{
		op.RegisterObject( "actions", ptr->m_actionHistory );

		op.RegisterNotifier( "_onPropertiesSwitchChanged", ptr->m_customPropertiesDescriptor->GetSwitchesNotifier() );

		op.RegisterValueRO< String >( "name", [ptr] ()
		{
			return ptr->m_customPropertiesDescriptor->GetTitle();
		} );

		op.RegisterObject( "properties", ptr->m_customPropertiesView->GetRootInterop() );

		op.RegisterMap( "_propertiesSwitches", [ptr] ( interop::IContext& ctx, const String& propertyPath )
		{
			const CName name = RED_NAME( propertyPath );
			if ( !ptr->m_customPropertiesDescriptor->GetSwitches().KeyExist( name ) )
			{
				ptr->m_customPropertiesDescriptor->GetSwitches().Insert( name, false );
			}

			return interop::utils::CreateRTTIAccessFunc( ctx, &ptr->m_customPropertiesDescriptor->GetSwitches()[ name ], [ &ptr, propertyPath ]( const rtti::AccessPath &path, const red::String &value )
			{
				auto &switches = ptr->m_customPropertiesDescriptor->GetSwitches();

				if ( propertyPath.EndsWith( ".selectAll" ) )
				{
					const String path = propertyPath.StringBefore( ".selectAll" );
					const Bool boolValue = FromStringDirect<Bool>( value );

					for ( auto &tempSwitch : switches )
					{
						const String key = tempSwitch.Key().AsChar();
						if ( key.BeginsWith( path ) && !key.EndsWith(".addNew") )
						{
							tempSwitch.Value() = boolValue;
						}
					}

					ptr->m_customPropertiesDescriptor->GetSwitchesNotifier().Notify();
				}
			} );
		} );
	}

	//----------------------------------------------------------------------------------

	void IResourceCustomPropertiesDescriptor::Initialize( const red::DynArray<THandle<CResource>>& resources )
	{
		m_resources = resources;
	}

	red::Map< CName, Bool >& IResourceCustomPropertiesDescriptor::GetSwitches()
	{
		return m_switches;
	}

	Bool IResourceCustomPropertiesDescriptor::IsSwitchChecked( const CName& name ) const
	{
		IResourceCustomPropertiesDescriptor* thisNonConst = const_cast<IResourceCustomPropertiesDescriptor*>( this );
		if ( !thisNonConst->GetSwitches().KeyExist( name ) )
		{
			return false;
		}
		return thisNonConst->GetSwitches()[name];
	}

	red::DynArray< THandle<IResourceCustomPropertiesDescriptor> > IResourceCustomPropertiesDescriptor::CreateDescriptors(const rtti::ClassType* resourceClass)
	{
		red::DynArray< THandle<IResourceCustomPropertiesDescriptor> > result;

		red::DynArray< const rtti::ClassType* > descriptorsClasses;
		GetRttiSystem().EnumClasses(ClassID<IResourceCustomPropertiesDescriptor>(), descriptorsClasses);

		for (const rtti::ClassType* descriptorClass : descriptorsClasses)
		{
			IResourceCustomPropertiesDescriptor* descriptor = descriptorClass->GetDefaultObject< IResourceCustomPropertiesDescriptor >();
			if (descriptor->SupportsResource(resourceClass))
			{
				result.PushBack( descriptorClass->CreateHandle<IResourceCustomPropertiesDescriptor>() );
			}
		}

		return result;
	}

	void IResourceCustomPropertiesDescriptor::ReportMessage( const String& message ) const
	{
		m_messages.PushBack( message );
	}
	
	red::AbsolutePath IResourceCustomPropertiesDescriptor::DumpMessages() const
	{
		if ( m_messages.Empty() )
		{
			return red::AbsolutePath();
		}

		String messagesString;
		for ( const String& message : m_messages )
		{
			messagesString += message;
			messagesString += "\n";
		}

		CDateTime nowDateTime;;
		red::Clock::GetInstance().GetLocalTime( nowDateTime );

		String dateTimeString;
		ToString( dateTimeString, nowDateTime, "%02d_%02d_%02d__%02d_%02d_%02d" );

		red::AbsolutePath path = red::paths::GetUserDirectory();
		path.AppendDirPath( "batch_tool_logs" );
		path.AppendFilePath( String::Printf( "%hs_%d.txt", dateTimeString.AsChar(), red::ThreadId::CurrentThread().AsNumber() ) );

		red::SaveStringToFile( path, messagesString );
		return path;
	}

	//---------------------------------------------------------------------------------------------------------------

	BatchTool::BatchTool( const rtti::ClassType* resourceClass, const red::DynArray< res::ResourcePath >& paths, ResourceBackend* backend )
		: m_resourceClass( resourceClass )
		, m_actionHistory( red::CreateUniquePtr< tools::IEditActionHistory >( nullptr ) )
		, m_backend( backend )
		, m_resourceProperties( resourceClass, m_actionHistory.Get() )
		, m_resources{ red::PoolBackend() }
	{
		for ( auto path : paths )
		{
			TResRef< CResource > resReference( path );
			if ( const THandle< CResource > resource = resReference.GetSafe() )
			{
				if ( resource->GetClass() == resourceClass )
				{
					m_resources.PushBack( resource );
				}
			}
		}
		m_resourceProperties.Initialize( m_resources );

		red::DynArray< THandle< IResourceCustomPropertiesDescriptor > > customPropertiesDescriptors =
			IResourceCustomPropertiesDescriptor::CreateDescriptors(resourceClass);

		for ( auto customPropertiesDescriptor : customPropertiesDescriptors )
		{
			customPropertiesDescriptor->Initialize( m_resources );

			ResourceCustomProperties customProps( customPropertiesDescriptor, m_actionHistory.Get() );
			m_customProperties.PushBack( customProps );
		}
	}

	BatchTool::~BatchTool()
	{}

	Bool BatchTool::Run()
	{
		job::Builder builder;

		if ( HasGlobalTab() )
		{
			builder.DispatchParallelForJob( "BatchTool/ApplyChanges/Base", [ this ]() { return m_resources.Size(); },
				[ this ]( Uint32 index, const job::RunContext& )
			{
				const THandle<CResource>& resource = m_resources[ index ];
				m_resourceProperties.ApplyChanges( resource );
			} );
		}

		auto runCustomProperties = [this] ( ResourceCustomProperties& customPropsDescriptor, const job::RunContext& runContext )
		{
			red::AbsolutePath logFile = customPropsDescriptor.ApplyChanges( runContext );

			if ( logFile.IsFilePath() )
			{
				red::ScopedLock< red::RWLock > lock( m_lock );
				m_logFiles.PushBack( logFile.AsChar() );
			}
		};

		builder.DispatchParallelForJob( "BatchTool/ApplyChanges/Custom", [ this ]() { return m_customProperties.Size(); },
			[ this, runCustomProperties]( Uint32 index, const job::RunContext& runContext )
		{
			auto customPropertiesDescriptor = m_customProperties[ index ];
			if ( customPropertiesDescriptor.RequiresMainThread() )
			{
				return;
			}

			runCustomProperties( customPropertiesDescriptor, runContext );
		} );

		for ( ResourceCustomProperties& customProperties : m_customProperties )
		{
			if ( customProperties.RequiresMainThread() )
			{
				runCustomProperties( customProperties, job::RunContext() );
			}
		}

		builder.DispatchParallelForJob( "BatchTool/SaveResources", [ this ]() { return m_resources.Size(); },
			[ this ]( Uint32 index, const job::RunContext& )
		{
			const THandle<CResource>& resource = m_resources[ index ];
			m_backend->SaveResource( resource, false );
		} );

		job::FlushCounter( builder.ExtractWaitCounter() );
		
		return true;
	}

	Bool BatchTool::HasGlobalTab() const
	{
		return m_resourceClass != ClassID< CBitmapTexture >()
			&& m_resourceClass != ClassID< ent::EntityTemplate >()
			&& m_resourceClass != ClassID< Multilayer_Mask >();
	}

	void BatchTool::SetupInterop( BatchTool* ptr, ::interop::utils::Object& op )
	{
		ptr->m_resourceProperties.ExposeInterop( op );
		op.RegisterFunctor<Bool>( "run", [ptr]( interop::IContext& ctx ) { return ptr->Run(); } );

		op.RegisterValueRO<Bool>( "hasGlobalTab", [ ptr ]() { return ptr->HasGlobalTab(); } );
		op.RegisterValueRO<Int32>( "customTabsCount", [ptr]() { return ptr->m_customProperties.Size(); } );		
		op.RegisterValueRO<red::DynArray< String >>( "logFiles", [ptr]() { return ptr->m_logFiles; } );

		if ( !ptr->m_customProperties.Empty() )
		{
			op.RegisterArray( "customProperties", ptr->m_customProperties.Size(),
				[ptr] ( interop::IContext& ctx, Uint32 index )
				{
					return ptr->m_customProperties[index].CreateInterop();
				} );
		}
	}
}