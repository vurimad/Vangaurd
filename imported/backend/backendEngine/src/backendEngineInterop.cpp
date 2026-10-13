/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "backendEngine.h"

#include "../../../common/interop/include/interopUtilsManual.h"
#include "../../../common/interop/include/interopInterchange.h"
#include "../../../common/interopServices/include/interopService.h"

#include "../../../common/redReflection/include/resource.h"
#include "../../../common/redReflection/include/version.h"

#include "../../../common/commChannel/include/networkConnection.h"

#include "resourceBackend.h"
#include "configBackend.h"
#include "engineStatsBackend.h"
#include "backendInteropUtils.h"

namespace Config
{
	TConfigVar< Int32 > cvDefaultBackendInteropStartingPort	( "Engine/Backend", "DefaultBackendInteropStartingPort", 37080, eConsoleVarFlag_Save | eConsoleVarFlag_Developer );
}

namespace helper
{
	class RTTIProperty : public interop::utils::Object
	{
	public:
		RTTIProperty( const rtti::Property* prop )
			: interop::utils::Object( "RTTIProperty" )
			, m_property( prop )
		{
			RegisterValue<String>( "name", [this](const interop::IContext& ctx, String& outVal) { outVal = m_property->GetName().AsChar(); } );
			RegisterValue<Uint32>( "offset", [this](const interop::IContext& ctx, Uint32& outVal) { outVal = m_property->GetDataOffset(); } );
			RegisterValue<String>( "type", [this](const interop::IContext& ctx, String& outVal) { outVal = m_property->GetType()->GetName().AsChar(); } );
			RegisterValue<String>( "customType", [this](const interop::IContext& ctx, String& outVal) { outVal = m_property->GetCustomEditorType().AsChar(); } );
			RegisterValue<Bool>( "isEditable", [this](const interop::IContext& ctx, Bool& outVal) { outVal = m_property->IsEditable(); } );
			RegisterValue<Bool>( "isReadonly ", [this](const interop::IContext& ctx, Bool& outVal) { outVal = m_property->IsReadOnly(); } );
			RegisterValue<Bool>( "isRanged", [this]( const interop::IContext& ctx, Bool& outVal ) { outVal = m_property->IsRanged(); } );
			RegisterValue<Bool>( "hasXAxisRange", [this]( const interop::IContext& ctx, Bool& outVal ) { outVal = m_property->HasXAxisConstraints(); } );
			RegisterValue<Bool>( "hasMinSize", [this]( const interop::IContext& ctx, Bool& outVal ) { outVal = m_property->HasMinSizeConstraints(); } );
			RegisterValue<Bool>( "hasMaxSize", [this]( const interop::IContext& ctx, Bool& outVal ) { outVal = m_property->HasMaxSizeConstraints(); } );
			RegisterValue<Bool>( "isInlined", [this](const interop::IContext& ctx, Bool& outVal) { outVal = m_property->IsInlined(); } );
			RegisterValue<Bool>( "canClear", [this]( const interop::IContext& ctx, Bool& outVal ) { outVal = m_property->CanClear(); } );
			RegisterValue<Bool>( "canReset", [this]( const interop::IContext& ctx, Bool& outVal ) { outVal = m_property->CanReset(); } );
			RegisterValue<Bool>( "canSelect", [this]( const interop::IContext& ctx, Bool& outVal ) { outVal = m_property->CanSelect(); } );
		}

	private:
		const rtti::Property*		m_property;
	};

	class RTTIEnum : public interop::utils::Object
	{
	public:
		RTTIEnum( const rtti::EnumType* enumType )
			: interop::utils::Object( "RTTIEnum" )
			, m_enumType( enumType )
		{
			RegisterValue<String>( "name", [this]( interop::IContext& ctx, String& outVal ) { outVal = m_enumType->GetName().AsChar(); } );
			RegisterValue< red::DynArray< CName > >( "options",
				[this]( interop::IContext& ctx, red::DynArray< CName >& outValue )
				{
					outValue = m_enumType->GetOptions();
				} );
			RegisterValue< red::DynArray< Int64 > >( "values",
				[this]( interop::IContext& ctx, red::DynArray< Int64 >& outValue )
				{
					outValue = m_enumType->GetValues();
				} );
		}

	private:
		const rtti::EnumType* m_enumType;
	};

	class RTTIBitfield : public interop::utils::Object
	{
	public:
		RTTIBitfield( const rtti::BitFieldType* bitfieldType )
			: interop::utils::Object( "RTTIBitfield" )
			, m_bitfieldType( bitfieldType )
		{
			RegisterValue<String>( "name", [this]( interop::IContext& ctx, String& outVal ) { outVal = m_bitfieldType->GetName().AsChar(); } );
			RegisterValue< red::DynArray< CName > >( "options",
				[this]( interop::IContext& ctx, red::DynArray< CName >& outValue )
				{
					m_bitfieldType->GetOptions( outValue );
				} );
		}

	private:
		const rtti::BitFieldType* m_bitfieldType;
	};

	class RTTIClass : public interop::utils::Object
	{
	public:
		RTTIClass( const rtti::ClassType* classType )
			: interop::utils::Object( "RTTIClass" )
			, m_classType( classType )
		{
			RegisterValue< String >( "name", [this](const interop::IContext& ctx, String& outVal) { outVal = m_classType->GetName().AsChar(); } );

			if ( m_classType->GetBaseClass() )
				RegisterValue< String >( "base", [this](const interop::IContext& ctx, String& outVal) { outVal = m_classType->GetBaseClass()->GetName().AsChar(); } );

			RegisterValue< Bool >( "isAbstract", [this](const interop::IContext& ctx, Bool& outVal) { outVal = m_classType->IsAbstract(); });

			if ( !classType->IsAbstract() )
			{
				if ( classType->IsA( CResource::GetStaticClass() ) )
				{
					if ( CResource* defObj = classType->GetDefaultObject<CResource>() )
					{
						RegisterValue< String >( "extension", [defObj]( const interop::IContext& ctx, String& outVal ) { outVal = defObj->GetExtension(); } );
						RegisterValue< String >( "deprecatedExtension", [defObj]( const interop::IContext & ctx, String & outVal ) { outVal = defObj->GetDeprecatedExtension(); } );
						RegisterValue< String >( "description", [defObj]( const interop::IContext& ctx, String& outVal ) { outVal = defObj->GetFriendlyDescription(); } );
					}
				}
			}

			//for ( const auto* prop : m_classType->GetLocalProperties() )
				//m_properties.PushBack( interop::ObjectPtr( RED_NEW( RTTIProperty )(prop) ) );

			//RegisterArray( "properties", m_properties.Size(), [this](const interop::IContext& ctx, const Uint32 index) { return m_properties[index]; });
		}

		const rtti::ClassType& GetClassType() const { return *m_classType; }

	private:
		const rtti::ClassType*				m_classType;
		red::DynArray< interop::ObjectPtr >	m_properties;
	};

	class RTTIRoot : public interop::utils::Object
	{
	public:
		RTTIRoot()
			: interop::utils::Object( "RTTIRoot" )
		{
			red::DynArray< const rtti::ClassType* >	classes;
			GetRttiSystem().EnumClasses( nullptr, classes );

			for ( const auto* classType : classes )
				m_classes.PushBack( interop::ObjectPtr( RED_NEW( RTTIClass )( classType ) ) );

			RegisterValue<interop::RTTIClassDump>( "classDump", [this]( ::interop::IContext& ctx, interop::RTTIClassDump& outClassDump )
			{
				// #tbd: shouldn't have to do this, but EnumClasses() returns duplicates for the math types
				red::DynArray< const rtti::ClassType* >	classes;
				GetRttiSystem().EnumClasses( nullptr, classes );
				std::sort( classes.Begin(), classes.End(), []( const rtti::ClassType* a, const rtti::ClassType* b ) { return a < b; } );
				classes.Remove( std::unique( classes.Begin(), classes.End() ), classes.End() );

				RTTIClassDumpMapper mapper( outClassDump, classes.Size() );
				for ( auto classType : classes )
				{
					mapper.MapClass( *classType );
				}
				RED_FATAL_ASSERT( mapper.VerifyAllClassesMapped(), "Not all RTTI classes mapped. Duplicated names?" );
			});

			RegisterArray( "classes", m_classes.Size(), [this](const interop::IContext& ctx, const Uint32 index) { return m_classes[index]; });

			RegisterMap( "enums", [this]( interop::IContext& ctx, const String& elementName )
			{
				if ( const rtti::EnumType* enumType = GetRttiSystem().FindEnum( RED_NAME_NOREG( elementName ) ) )
				{
					return interop::ObjectPtr( RED_NEW( RTTIEnum )( enumType ) );
				}
				return interop::ObjectPtr();
			} );

			RegisterMap( "bitfields", [this]( interop::IContext& ctx, const String& elementName )
			{
				if ( const rtti::BitFieldType* bitfieldType = GetRttiSystem().FindBitField( RED_NAME_NOREG( elementName ) ) )
				{
					return interop::ObjectPtr( RED_NEW( RTTIBitfield )( bitfieldType ) );
				}
				return interop::ObjectPtr();
			} );
		}

	private:
		red::DynArray< interop::ObjectPtr >	m_classes;
	};
}

Bool CBackendEngine::InitializeInterop()
{
	RED_LOG( "Engine: Initializing interop root ..." );

	m_interopRoot = red::CreateSharedPtr< ::interop::utils::Object >( "EngineRoot" );

	// create the RTTI root
	m_interopRoot->RegisterObject( "rtti", interop::ObjectPtr( RED_NEW( helper::RTTIRoot )() ) );

	// #todo: ideally return true, but mess of fallback cache and base engine not checking results if compiled ok etc...
	m_interopRoot->RegisterFunctor<void, Bool>( "compileScripts", [this] ( interop::IContext& ctx, const Bool& force )
	{
		CheckForUpdatedScriptsBlob( 0.f, force );
	} );

	m_interopRoot->RegisterValueRO<String>( "appSignature", []()
	{
		const String appSignature = APP_LAST_P4_CHANGE APP_P4_SHELF;
		return appSignature;
	} );

	m_interopRoot->RegisterValueRO< Bool >( "isHoudiniEngineAvailable",
		[]() { return houdini_legacy::IsHoudiniEngineAvailable(); } );

	m_interopRoot->RegisterObject( "depot", [this] ( interop::IContext& ctx ) { return m_resourceBackend->CreateInterop(); } );
	m_interopRoot->RegisterObject( "config", [this] ( interop::IContext& ctx ) { return m_configBackend->CreateInterop(); } );
	m_interopRoot->RegisterObject( "stats", [this] ( interop::IContext& ctx ) { return m_statsBackend->CreateInterop(); } );
	m_interopRoot->RegisterObject( "profiler", [this] ( interop::IContext& ctx ) { return m_profilerBackend->CreateInterop(); } );
	m_interopRoot->RegisterObject( "debug", [this] ( interop::IContext& ctx ) { return m_debuggerBackend->CreateInterop(); } );
	m_interopRoot->RegisterObject( "debugFilter", [this] ( interop::IContext& ctx ) { return m_debugFilterHelper->CreateInterop(); } );
	m_interopRoot->RegisterObject( "qualityLevel", [this]( interop::IContext& ctx ) { return m_qualityLevelHelper->CreateInterop(); } );

	return static_cast< Bool >( m_interopRoot );
}

Bool CBackendEngine::InitializeInteropService()
{
	if ( m_interopStartingPort == 0 )
	{
		RED_LOG_DEBUG("No port override provided, using config value");
		red::Int32 port = Config::cvDefaultBackendInteropStartingPort.Get();
		RED_FATAL_ASSERT(
			port > 1024 && port < (red::Int32)(std::numeric_limits<red::Uint16>::max() - 2),
			"Invalid config value of %d for backend interop port - must be within range 1024 and 65535",
			port);
		m_interopStartingPort = (red::Uint16)port;
	}
	RED_LOG("Engine: Initializing interop service start at port %d...", m_interopStartingPort);

	m_interopService = red::CreateUniquePtr< interop::srv::InteropService >();

	return m_interopService->Open(
		m_interopStartingPort,
		m_interopRoot,
		[this] ( Int32 exitCode ) { RequestExit( exitCode ); },
		[this] ()                 { RequestDetach(); },
		true );
}
