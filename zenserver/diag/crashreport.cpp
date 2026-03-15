// Copyright Noah Games, Inc. All Rights Reserved.

#include "crashreport.h"

#include <zencore/filesystem.h>
#include <zencore/zencore.h>

#include <client/windows/handler/exception_handler.h>

#include <filesystem>

// A callback function to run after the minidump has been written.
// minidump_id is a unique id for the dump, so the minidump
// file is <dump_path>\<minidump_id>.dmp.  context is the parameter supplied
// by the user as callback_context when the handler was created.  exinfo
// points to the exception record, or NULL if no exception occurred.
// succeeded indicates whether a minidump file was successfully written.
// assertion points to information about an assertion if the handler was
// invoked by an assertion.
//
// If an exception occurred and the callback returns true, Breakpad will treat
// the exception as fully-handled, suppressing any other handlers from being
// notified of the exception.  If the callback returns false, Breakpad will
// treat the exception as unhandled, and allow another handler to handle it.
// If there are no other handlers, Breakpad will report the exception to the
// system as unhandled, allowing a debugger or native crash dialog the
// opportunity to handle the exception.  Most callback implementations
// should normally return the value of |succeeded|, or when they wish to
// not report an exception of handled, false.  Callbacks will rarely want to
// return true directly (unless |succeeded| is true).
//
// For out-of-process dump generation, dump path and minidump ID will always
// be NULL. In case of out-of-process dump generation, the dump path and
// minidump id are controlled by the server process and are not communicated
// back to the crashing process.

static bool
CrashMinidumpCallback(const wchar_t*	  dump_path,
					  const wchar_t*	  minidump_id,
					  void*				  context,
					  EXCEPTION_POINTERS* exinfo,
					  MDRawAssertionInfo* assertion,
					  bool				  succeeded)
{
	ZEN_UNUSED(dump_path, minidump_id, context, exinfo, assertion, succeeded);

	// TODO!
	return succeeded;
}

// A callback function to run before Breakpad performs any substantial
// processing of an exception.  A FilterCallback is called before writing
// a minidump.  context is the parameter supplied by the user as
// callback_context when the handler was created.  exinfo points to the
// exception record, if any; assertion points to assertion information,
// if any.
//
// If a FilterCallback returns true, Breakpad will continue processing,
// attempting to write a minidump.  If a FilterCallback returns false,
// Breakpad will immediately report the exception as unhandled without
// writing a minidump, allowing another handler the opportunity to handle it.

bool
CrashFilterCallback(void* context, EXCEPTION_POINTERS* exinfo, MDRawAssertionInfo* assertion)
{
	ZEN_UNUSED(context, exinfo, assertion);

	// Yes, write a dump
	return false;
}

void
InitializeCrashReporting(const std::filesystem::path& DumpPath)
{
	// handler_types specifies the types of handlers that should be installed.

	zen::CreateDirectories(DumpPath);

	static google_breakpad::ExceptionHandler _(DumpPath.native().c_str(),					   // Dump path
											   CrashFilterCallback,							   // Filter Callback
											   CrashMinidumpCallback,						   // Minidump callback
											   nullptr,										   // Callback context
											   google_breakpad::ExceptionHandler::HANDLER_ALL  // Handler Types
	);
}

