namespace dbgutils
{

struct RegisteredAttachmentTable;

namespace win32
{
	struct IPCContext;

	struct ErrorReporterArgs
	{
		ErrorReporterArgs()
			: m_errorReason( red::eErrorReason_Unknown )
			, m_pAttachmentTable( nullptr )
			, m_pCustomErrMsg( nullptr )
			, m_outErrorMessageBuffer( nullptr )
			, m_errorMessageBufferCount( 0 )
			, m_appVersionNumber( "" )
		{}

		red::EErrorReason					m_errorReason;
		const RegisteredAttachmentTable*	m_pAttachmentTable;
		const red::ErrorMessage*			m_pCustomErrMsg;
		char*								m_outErrorMessageBuffer;
		Uint32								m_errorMessageBufferCount;
		const char*							m_appVersionNumber;
	};

	Bool WakeOutOfProcessErrorReporter( const IPCContext& ipcContext, const ErrorReporterArgs& args, _EXCEPTION_POINTERS* exceptionInfo );
	Bool WaitForOutOfProcessErrorReporter(const IPCContext& ipcContext);
	Int32 ErrorReporterMainLoop( const char* connectionString );
	Bool IsAttachedToProcess( Uint32 processID );
}
}
