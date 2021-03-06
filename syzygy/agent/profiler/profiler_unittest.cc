// Copyright 2012 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Profiler unittests.
#include "syzygy/agent/profiler/profiler.h"

#include <intrin.h>
#include <psapi.h>

#include "base/bind.h"
#include "base/file_util.h"
#include "base/message_loop.h"
#include "base/scoped_temp_dir.h"
#include "base/threading/platform_thread.h"
#include "base/threading/thread.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "syzygy/agent/common/process_utils.h"
#include "syzygy/core/unittest_util.h"
#include "syzygy/trace/common/unittest_util.h"
#include "syzygy/trace/parse/parser.h"
#include "syzygy/trace/parse/unittest_util.h"
#include "syzygy/trace/protocol/call_trace_defs.h"
#include "syzygy/trace/service/service.h"
#include "syzygy/trace/service/service_rpc_impl.h"
#include "syzygy/trace/service/trace_file_writer_factory.h"

extern "C" {

// We register a TLS callback to test TLS thread notifications.
extern PIMAGE_TLS_CALLBACK profiler_test_tls_callback_entry;
void WINAPI ProfilerTestTlsCallback(PVOID h, DWORD reason, PVOID reserved);

// Force the linker to include the TLS entry.
#pragma comment(linker, "/INCLUDE:__tls_used")
#pragma comment(linker, "/INCLUDE:_profiler_test_tls_callback_entry")

#pragma data_seg(push, old_seg)
// Use a typical possible name in the .CRT$XL? list of segments.
#pragma data_seg(".CRT$XLB")
PIMAGE_TLS_CALLBACK profiler_test_tls_callback_entry =
    &ProfilerTestTlsCallback;
#pragma data_seg(pop, old_seg)

PIMAGE_TLS_CALLBACK tls_action = NULL;

void WINAPI ProfilerTestTlsCallback(PVOID h, DWORD reason, PVOID reserved) {
  if (tls_action)
    tls_action(h, reason, reserved);
}

}  // extern "C"

namespace agent {
namespace profiler {

namespace {

using agent::common::GetProcessModules;
using agent::common::ModuleVector;
using file_util::FileEnumerator;
using testing::_;
using testing::Return;
using testing::StrictMockParseEventHandler;
using trace::service::RpcServiceInstanceManager;
using trace::service::TraceFileWriterFactory;
using trace::service::Service;
using trace::parser::Parser;
using trace::parser::ParseEventHandler;

// The information on how to set the thread name comes from
// a MSDN article: http://msdn2.microsoft.com/en-us/library/xcb2z8hs.aspx
const DWORD kVCThreadNameException = 0x406D1388;

typedef struct tagTHREADNAME_INFO {
  DWORD dwType;  // Must be 0x1000.
  LPCSTR szName;  // Pointer to name (in user addr space).
  DWORD dwThreadID;  // Thread ID (-1=caller thread).
  DWORD dwFlags;  // Reserved for future use, must be zero.
} THREADNAME_INFO;

// This function has try handling, so it is separated out of its caller.
void SetNameInternal(const char* name) {
  THREADNAME_INFO info;
  info.dwType = 0x1000;
  info.szName = name;
  info.dwThreadID = -1;
  info.dwFlags = 0;

  __try {
    RaiseException(kVCThreadNameException, 0, sizeof(info)/sizeof(DWORD),
                   reinterpret_cast<DWORD_PTR*>(&info));
  } __except(EXCEPTION_CONTINUE_EXECUTION) {
  }
}

// Return address location resolution function.
typedef uintptr_t (__cdecl *ResolveReturnAddressLocationFunc)(
    uintptr_t pc_location);

MATCHER_P(ModuleAtAddress, module, "") {
  return arg->module_base_addr == module;
}

// TODO(rogerm): Create a base fixture (perhaps templatized) to factor out
//     the common bits of testing various clients with the call trace service.
class ProfilerTest : public testing::Test {
 public:
  ProfilerTest()
      : module_(NULL),
        resolution_func_(NULL) {
  }

  virtual void SetUp() OVERRIDE {
    testing::Test::SetUp();

    // Create a temporary directory for the call trace files.
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());

    service_.SetEnvironment();
  }

  virtual void TearDown() OVERRIDE {
    tls_action = NULL;

    UnloadDll();

    // Stop the call trace service.
    service_.Stop();
  }

  void StartService() {
    service_.Start(temp_dir_.path());
  }

  void StopService() {
    service_.Stop();
  }

  void ReplayLogs() {
    // Stop the service if it's running.
    ASSERT_NO_FATAL_FAILURE(StopService());

    Parser parser;
    ASSERT_TRUE(parser.Init(&handler_));

    // Queue up the trace file(s) we engendered.
    file_util::FileEnumerator enumerator(temp_dir_.path(),
                                         false,
                                         FileEnumerator::FILES);
    size_t num_files = 0;
    while (true) {
      FilePath trace_file = enumerator.Next();
      if (trace_file.empty())
        break;
      ASSERT_TRUE(parser.OpenTraceFile(trace_file));
      ++num_files;
    }
    EXPECT_GT(num_files, 0U);
    ASSERT_TRUE(parser.Consume());
  }

  // TODO(siggi): These are shareable with the other instrumentation DLL tests.
  //    Move them to a shared fixture superclass.
  void LoadDll() {
    ASSERT_TRUE(module_ == NULL);
    static const wchar_t kCallTraceDll[] = L"profile_client.dll";
    ASSERT_EQ(NULL, ::GetModuleHandle(kCallTraceDll));
    module_ = ::LoadLibrary(kCallTraceDll);
    ASSERT_TRUE(module_ != NULL);
    _indirect_penter_dllmain_ =
        ::GetProcAddress(module_, "_indirect_penter_dllmain");
    _indirect_penter_ = ::GetProcAddress(module_, "_indirect_penter");

    ASSERT_TRUE(_indirect_penter_dllmain_ != NULL);
    ASSERT_TRUE(_indirect_penter_ != NULL);

    resolution_func_ = reinterpret_cast<ResolveReturnAddressLocationFunc>(
        ::GetProcAddress(module_, "ResolveReturnAddressLocation"));
    ASSERT_TRUE(resolution_func_ != NULL);
  }

  void UnloadDll() {
    if (module_ != NULL) {
      ASSERT_TRUE(::FreeLibrary(module_));
      module_ = NULL;
      _indirect_penter_ = NULL;
      _indirect_penter_dllmain_ = NULL;
    }
  }

  static BOOL WINAPI IndirectDllMain(HMODULE module,
                                     DWORD reason,
                                     LPVOID reserved);
  static BOOL WINAPI DllMainThunk(HMODULE module,
                                  DWORD reason,
                                  LPVOID reserved);

  static int IndirectFunctionA(int param1, const void* param2);
  static int FunctionAThunk(int param1, const void* param2);
  static int TestResolutionFuncThunk(ResolveReturnAddressLocationFunc resolver);
  static int TestResolutionFuncNestedThunk(
      ResolveReturnAddressLocationFunc resolver);

 protected:
  // The directory where trace file output will be written.
  ScopedTempDir temp_dir_;

  // The handler to which the trace file parser will delegate events.
  StrictMockParseEventHandler handler_;

  // The address resolution function exported from the profiler dll.
  ResolveReturnAddressLocationFunc resolution_func_;

  // Our call trace service process instance.
  testing::CallTraceService service_;

 private:
  HMODULE module_;
  static FARPROC _indirect_penter_;
  static FARPROC _indirect_penter_dllmain_;
};

FARPROC ProfilerTest::_indirect_penter_ = NULL;
FARPROC ProfilerTest::_indirect_penter_dllmain_ = NULL;

BOOL WINAPI ProfilerTest::IndirectDllMain(HMODULE module,
                                          DWORD reason,
                                          LPVOID reserved) {
  return TRUE;
}

BOOL __declspec(naked) WINAPI ProfilerTest::DllMainThunk(HMODULE module,
                                                         DWORD reason,
                                                         LPVOID reserved) {
  __asm {
    push IndirectDllMain
    jmp _indirect_penter_dllmain_
  }
}

int ProfilerTest::IndirectFunctionA(int param1,
                                    const void* param2) {
  return param1 + reinterpret_cast<int>(param2);
}

int __declspec(naked) ProfilerTest::FunctionAThunk(int param1,
                                                   const void* param2) {
  __asm {
    push IndirectFunctionA
    jmp _indirect_penter_
  }
}

void TestResolutionFunc(ResolveReturnAddressLocationFunc resolver) {
  uintptr_t pc_location =
      reinterpret_cast<uintptr_t>(_AddressOfReturnAddress());
  ASSERT_NE(pc_location, resolver(pc_location));

  // Make sure we unwind thunk chains.
  pc_location = resolver(pc_location);
  ASSERT_EQ(pc_location, resolver(pc_location));
}

int __declspec(naked) ProfilerTest::TestResolutionFuncThunk(
    ResolveReturnAddressLocationFunc resolver) {
  __asm {
    push TestResolutionFunc
    jmp _indirect_penter_
  }
}

int __declspec(naked) ProfilerTest::TestResolutionFuncNestedThunk(
    ResolveReturnAddressLocationFunc resolver) {
  // This will make like tail call elimination and create nested thunks.
  __asm {
    push TestResolutionFuncThunk
    jmp _indirect_penter_
  }
}

}  // namespace

TEST_F(ProfilerTest, NoServerNoCrash) {
  ASSERT_NO_FATAL_FAILURE(LoadDll());

  EXPECT_TRUE(DllMainThunk(NULL, DLL_PROCESS_ATTACH, NULL));
}

TEST_F(ProfilerTest, ResolveReturnAddressLocation) {
  // Spin up the RPC service.
  ASSERT_NO_FATAL_FAILURE(StartService());

  ASSERT_NO_FATAL_FAILURE(LoadDll());

  // Test the return address resolution function.
  ASSERT_NO_FATAL_FAILURE(TestResolutionFuncThunk(resolution_func_));

  // And with a nested thunk.
  ASSERT_NO_FATAL_FAILURE(TestResolutionFuncNestedThunk(resolution_func_));
}

TEST_F(ProfilerTest, RecordsAllModulesAndFunctions) {
  // Spin up the RPC service.
  ASSERT_NO_FATAL_FAILURE(StartService());

  // Get our own module handle.
  HMODULE self_module = ::GetModuleHandle(NULL);

  ASSERT_NO_FATAL_FAILURE(LoadDll());
  // TODO(rogerm): This generates spurious error logs at higher log levels
  //     because the module paths are different when depending on who infers
  //     them (one is drive letter based and the other is device based).
  EXPECT_TRUE(DllMainThunk(self_module, DLL_PROCESS_ATTACH, NULL));

  // Get the module list prior to unloading the profile DLL.
  ModuleVector modules;
  GetProcessModules(&modules);

  ASSERT_NO_FATAL_FAILURE(UnloadDll());

  // Set up expectations for what should be in the trace.
  EXPECT_CALL(handler_, OnProcessStarted(_, ::GetCurrentProcessId(), _));
  for (size_t i = 0; i < modules.size(); ++i) {
    EXPECT_CALL(handler_, OnProcessAttach(_,
                                          ::GetCurrentProcessId(),
                                          ::GetCurrentThreadId(),
                                          ModuleAtAddress(modules[i])));
  }

  // TODO(siggi): Match harder here.
  EXPECT_CALL(handler_, OnInvocationBatch(_,
                                          ::GetCurrentProcessId(),
                                          ::GetCurrentThreadId(),
                                          1,
                                          _));
  EXPECT_CALL(handler_, OnProcessEnded(_, ::GetCurrentProcessId()));

  // Replay the log.
  ASSERT_NO_FATAL_FAILURE(ReplayLogs());
}

namespace {

// We invoke the thunks through these intermediate functions to make sure
// we can generate two or more identical invocation records, e.g. same
// call site, same callee. We turn off inlining to make sure the functions
// aren't assimilated into the callsite by the compiler or linker, thus
// defeating our intent.
#pragma auto_inline(off)
void InvokeDllMainThunk(HMODULE module) {
  EXPECT_TRUE(ProfilerTest::DllMainThunk(module, DLL_PROCESS_ATTACH, NULL));
}

void InvokeFunctionAThunk() {
  const int kParam1 = 0xFAB;
  const void* kParam2 = &kParam1;
  const int kExpected = kParam1 + reinterpret_cast<int>(kParam2);
  EXPECT_EQ(kExpected, ProfilerTest::FunctionAThunk(kParam1, kParam2));
}
#pragma auto_inline(on)

}  // namespace

TEST_F(ProfilerTest, RecordsOneEntryPerModuleAndFunction) {
  // Spin up the RPC service.
  ASSERT_NO_FATAL_FAILURE(StartService());

  // Get our own module handle.
  HMODULE self_module = ::GetModuleHandle(NULL);

  ASSERT_NO_FATAL_FAILURE(LoadDll());

  // Record the module load twice.
  EXPECT_NO_FATAL_FAILURE(InvokeDllMainThunk(self_module));
  EXPECT_NO_FATAL_FAILURE(InvokeDllMainThunk(self_module));

  // And invoke Function A twice.
  ASSERT_NO_FATAL_FAILURE(InvokeFunctionAThunk());
  ASSERT_NO_FATAL_FAILURE(InvokeFunctionAThunk());

  // Get the module list prior to unloading the profile DLL.
  ModuleVector modules;
  GetProcessModules(&modules);

  ASSERT_NO_FATAL_FAILURE(UnloadDll());

  EXPECT_CALL(handler_, OnProcessStarted(_, ::GetCurrentProcessId(), _));

  // We should only have one event per module,
  // despite the double DllMain invocation.
  for (size_t i = 0; i < modules.size(); ++i) {
    EXPECT_CALL(handler_, OnProcessAttach(_,
                                          ::GetCurrentProcessId(),
                                          ::GetCurrentThreadId(),
                                          ModuleAtAddress(modules[i])));
  }

  // TODO(siggi): Match harder here.
  // We should only have two distinct invocation records,
  // despite calling each function twice.
  EXPECT_CALL(handler_, OnInvocationBatch(_,
                                          ::GetCurrentProcessId(),
                                          ::GetCurrentThreadId(),
                                          2,
                                          _));
  EXPECT_CALL(handler_, OnProcessEnded(_, ::GetCurrentProcessId()));

  // Replay the log.
  ASSERT_NO_FATAL_FAILURE(ReplayLogs());
}

TEST_F(ProfilerTest, RecordsThreadName) {
  if (::IsDebuggerPresent()) {
    LOG(WARNING) << "This test fails under debugging.";
    return;
  }

  // Spin up the RPC service.
  ASSERT_NO_FATAL_FAILURE(StartService());

  ASSERT_NO_FATAL_FAILURE(LoadDll());

  // And invoke a function to get things initialized.
  ASSERT_NO_FATAL_FAILURE(InvokeFunctionAThunk());

  // Beware that this test will fail under debugging, as the
  // debugger by default swallows the exception.
  static const char kThreadName[] = "Profiler Test Thread";
  SetNameInternal(kThreadName);

  ASSERT_NO_FATAL_FAILURE(UnloadDll());

  EXPECT_CALL(handler_, OnProcessStarted(_, ::GetCurrentProcessId(), _));
  EXPECT_CALL(handler_, OnProcessAttach(_,
                                        ::GetCurrentProcessId(),
                                        ::GetCurrentThreadId(),
                                        _))
      .Times(testing::AnyNumber());
  EXPECT_CALL(handler_, OnInvocationBatch(_,
                                          ::GetCurrentProcessId(),
                                          ::GetCurrentThreadId(),
                                          _ ,_));
  EXPECT_CALL(handler_, OnThreadName(_,
                                     ::GetCurrentProcessId(),
                                     ::GetCurrentThreadId(),
                                     base::StringPiece(kThreadName)));
  EXPECT_CALL(handler_, OnProcessEnded(_, ::GetCurrentProcessId()));

  // Replay the log.
  ASSERT_NO_FATAL_FAILURE(ReplayLogs());
}

namespace {

void WINAPI TlsAction(PVOID h, DWORD reason, PVOID reserved) {
  InvokeFunctionAThunk();
}

}  // namespace

TEST_F(ProfilerTest, ReleasesBufferOnThreadExit) {
  // Spin up the RPC service.
  ASSERT_NO_FATAL_FAILURE(StartService());

  ASSERT_NO_FATAL_FAILURE(LoadDll());

  tls_action = TlsAction;

  // Spinning 400 * 8 threads should exhaust the address
  // space if we're leaking a buffer for each thread.
  for (size_t i = 0; i < 400; ++i) {
    base::Thread thread1("one");
    base::Thread thread2("two");
    base::Thread thread3("three");
    base::Thread thread4("four");
    base::Thread thread5("five");
    base::Thread thread6("six");
    base::Thread thread7("seven");
    base::Thread thread8("eight");

    base::Thread* threads[8] = { &thread1, &thread2, &thread3, &thread4,
                                 &thread5, &thread6, &thread7, &thread8};

    // Start all the threads, and make them do some work.
    for (size_t j = 0; j < arraysize(threads); ++j) {
      base::Thread* thread = threads[j];
      thread->Start();
      thread->message_loop()->PostTask(
          FROM_HERE, base::Bind(InvokeFunctionAThunk));
    }

    // This will implicitly wind down all the threads.
  }

  ASSERT_NO_FATAL_FAILURE(UnloadDll());
}

}  // namespace profiler
}  // namespace agent
