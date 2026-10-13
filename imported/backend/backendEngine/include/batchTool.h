#pragma once

#include "../../../common/interop/include/interopUtilsManual.h"

namespace tools
{
	class ObjectCollection;
	class IEditActionHistory;
	class ResourceBackend;



	class BACKEND_ENGINE_API IResourceCustomPropertiesDescriptor : public ISerializable
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_TYPE( IResourceCustomPropertiesDescriptor );

	public:
		virtual ~IResourceCustomPropertiesDescriptor()
		{
		}

		virtual void Initialize( const red::DynArray< THandle< CResource > >& resources );
		virtual red::Map< CName, Bool >& GetSwitches();
		Bool IsSwitchChecked( const CName& name ) const;

		const red::DynArray< THandle< CResource > >& GetResources() const { return m_resources; }

		virtual Bool RequiresMainThread() const { return false; }
		virtual void ApplyChanges( const job::RunContext& runContext ) const = 0;
		virtual Bool SupportsResource( const rtti::ClassType* resourceClass ) const = 0;
		red::AbsolutePath DumpMessages() const;

		virtual const String& GetTitle() const = 0;

		static red::DynArray< THandle<IResourceCustomPropertiesDescriptor> > CreateDescriptors( const rtti::ClassType* resourceClass );

		interop::InteropNotifier& GetSwitchesNotifier()
		{
			return m_switchesNotifier;
		}

	protected:
		void ReportMessage( const String& message ) const;

		red::DynArray< THandle< CResource > > m_resources;
		red::Map< CName, Bool > m_switches;
		ResourceBackend* m_backend;
		interop::InteropNotifier m_switchesNotifier;

	private:
		mutable red::DynArray< String > m_messages;
	};

	class BACKEND_ENGINE_API ResourceBaseProperties
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		DECLARE_INTEROP( ResourceBaseProperties );

	public:
		ResourceBaseProperties( const rtti::ClassType* resourceClass, tools::IEditActionHistory* actionHistory );
		void Initialize( const red::DynArray< THandle< CResource > >& resources );
		void ApplyChanges( const THandle< CResource >& resourceToEdit ) const;

		const THandle< CResource >& GetResourceTemplate() const;
		red::Map< CName, Bool >& GetSwitches();

	private:
		THandle< CResource > m_resourceTemplate;
		tools::ObjectCollection* m_resourceView;

		red::Map< CName, Bool > m_propertiesSwitches;
	};

	class BACKEND_ENGINE_API ResourceCustomProperties
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		DECLARE_INTEROP( ResourceCustomProperties );

	public:
		ResourceCustomProperties( const THandle<IResourceCustomPropertiesDescriptor>& propertiesDesc, tools::IEditActionHistory* actionHistory );
		red::AbsolutePath ApplyChanges( const job::RunContext& runContext ) const;

		virtual Bool RequiresMainThread() const { return m_customPropertiesDescriptor->RequiresMainThread(); }

	private:
		IEditActionHistory& m_actionHistory;
		THandle<IResourceCustomPropertiesDescriptor> m_customPropertiesDescriptor;
		tools::ObjectCollection* m_customPropertiesView;
	};

	class BACKEND_ENGINE_API BatchTool
	{
		RED_USE_MEMORY_POOL(red::PoolBackend);
		DECLARE_INTEROP( BatchTool );

	public:
		BatchTool( const rtti::ClassType* resourceClass, const red::DynArray< res::ResourcePath >& paths, ResourceBackend* backend );
		virtual ~BatchTool();

		Bool HasGlobalTab() const;

	protected:
		Bool Run();

		const rtti::ClassType* m_resourceClass;
		red::UniquePtr< tools::IEditActionHistory > m_actionHistory;
		ResourceBackend* m_backend;

		ResourceBaseProperties m_resourceProperties;
		red::DynArray< ResourceCustomProperties > m_customProperties;
		red::DynArray< THandle< CResource > > m_resources;
		red::DynArray< String > m_logFiles;

		red::RWLock m_lock;
	};
}