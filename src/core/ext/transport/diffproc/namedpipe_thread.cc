/*
 *
 * Copyright 2017 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>
#include <src/core/ext/transport/diffproc/namedpipe_thread.h>
#include <src\core\lib\iomgr\exec_ctx.h>
#include <grpc/support/string_util.h>


grpc_thread_handle* grpc_createHandle(HANDLE hd, const char* name) {
  char* final_name;
  grpc_thread_handle* tHandle = (grpc_thread_handle*)gpr_malloc(sizeof(grpc_thread_handle));
  memset(tHandle, 0, sizeof(grpc_thread_handle));
  tHandle->pipeHandle = hd;
  gpr_mu_init(&tHandle->state_mu);
  gpr_asprintf(&final_name, "%s:Handle=0x%p", name, tHandle);
  gpr_free(final_name);
  return tHandle;
}


DWORD WINAPI InstanceThread(LPVOID lparam) { 
  grpc_thread_handle* thread = (grpc_thread_handle*)lparam;
  int connectSuccess = ConnectNamedPipe(thread->pipeHandle, NULL);
  if (connectSuccess == 1) {
    puts("Connection Successful; Now Creating a new Thread Instance");
    //grpc_core::ExecCtx::Run(DEBUG_LOCATION, thread->complete_closure, GRPC_ERROR_NONE);
    thread->complete_closure->cb(thread->complete_closure->cb_arg, GRPC_ERROR_NONE);
    return 1;
  } else {
    puts("Error connecting to pipe..");
    CloseHandle(thread->pipeHandle);
    return (DWORD)-1;
  }
 

}


grpc_error* CreateThreadProcess(grpc_thread_handle* thread) {
  DWORD dwThreadId = 0;
  grpc_error* error = NULL;
  HANDLE t;

    t = CreateThread(NULL,                // no security attribute
                            0,                   // default stack size
                            InstanceThread,      // thread proc
                            (LPVOID)thread,  // thread parameter
                            0,                   // not suspended
                            &dwThreadId);   
    if (t != NULL) {
      CloseHandle(t);
    } else {
      puts("Error, creating thread");
      error = GRPC_WSA_ERROR(GetLastError(), "ThreadCreation");
    }
    return error;
}
