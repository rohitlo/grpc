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


static void destroy(grpc_thread_handle* handle) {
  gpr_mu_destroy(&handle->state_mu);
  gpr_free(handle);
}

static bool check_destroyable(grpc_thread_handle* handle) {
  return handle->destroy_called == true;
}

void grpc_nphandle_destroy(grpc_thread_handle* handle) {
  gpr_mu_lock(&handle->state_mu);
  GPR_ASSERT(!handle->destroy_called);
  handle->destroy_called = true;
  bool should_destroy = check_destroyable(handle);
  gpr_mu_unlock(&handle->state_mu);
  if (should_destroy) destroy(handle);
}



/* Schedule a shutdown of the namedpipe handle operations. Will call the pending
   operations to abort them. We need to do that this way because of the
   various callsites of that function, which happens to be in various
   mutex hold states, and that'd be unsafe to call them directly. */
void grpc_nphandle_shutdown(grpc_thread_handle* thread) {
  int status;
  gpr_mu_lock(&thread->state_mu);
  if (thread->shutdown_called) {
    gpr_mu_unlock(&thread->state_mu);
    return;
  }
  thread->shutdown_called = true;
  gpr_mu_unlock(&thread->state_mu);

  CloseHandle(thread->pipeHandle);
}



//DWORD WINAPI InstanceThread(LPVOID lparam) { 
//  grpc_thread_handle* thread = (grpc_thread_handle*)lparam;
//  puts("In instance thread");
//  int connectSuccess = ConnectNamedPipe(thread->pipeHandle, NULL);
//  if (connectSuccess == 1) {
//    puts("Connection Successful; Now Creating a new Thread Instance");
//    thread->grpc_on_accept(thread->arg, GRPC_ERROR_NONE);
//    return 1;
//  } else {
//    puts("Error connecting to pipe..");
//    thread->grpc_on_error(thread->arg, GRPC_ERROR_NONE);
//    CloseHandle(thread->pipeHandle);
//    return (DWORD) - 1;
//  }
//  
// // grpc_core::ExecCtx::Run(DEBUG_LOCATION, thread->complete_closure,
//                          //GRPC_ERROR_NONE);
//  }

DWORD WINAPI DataThread(LPVOID lparam) {
  grpc_thread_handle* thread = (grpc_thread_handle*)lparam;
  puts("******** In data thread ************* \n");
    // thread->grpc_on_accept(thread->arg, GRPC_ERROR_NONE);
  do{
   // puts("\n************* calling new batch ops **************** ");
    thread->grpc_on_accept_stream(thread->arg, GRPC_ERROR_NONE);
    //puts(" \n**************** After batch completion in Instance thread *****************");
  } while (WAIT_OBJECT_0 == WaitForSingleObject(thread->pipeHandle,INFINITE));

  puts("\n *************** DATA THREAD EXITING ***************** ");
  return 1;
}

 DWORD WINAPI InstanceThread(LPVOID lparam) {
  grpc_thread_handle* thread = (grpc_thread_handle*)lparam;
  puts("In instance thread");
  int connectSuccess = ConnectNamedPipe(thread->pipeHandle, NULL);
  if (connectSuccess == 1) {
    puts("Connection Successful; Now Creating a new Thread Instance");
    thread->grpc_on_accept(thread->arg, GRPC_ERROR_NONE);
    return 1;
  } else {
    puts("Error connecting to pipe..");
    thread->grpc_on_error(thread->arg, GRPC_ERROR_NONE);
    CloseHandle(thread->pipeHandle);
    return (DWORD) - 1;
  }

 // grpc_core::ExecCtx::Run(DEBUG_LOCATION, thread->complete_closure,
                          //GRPC_ERROR_NONE);
  }
 

grpc_error* CreateDataProcess(grpc_thread_handle* thread) {
  // grpc_core::ExecCtx exec_ctx;
  DWORD dwThreadId = 0;
  grpc_error* error = NULL;
  HANDLE t;

  t = CreateThread(NULL,            // no security attribute
                   0,               // default stack size
                   DataThread,  // thread proc
                   (LPVOID)thread,  // thread parameter
                   0,               // not suspended
                   &dwThreadId);
  if (t != NULL) {
    puts("Data Thread Successfully created and running.....");
    CloseHandle(t);
  } else {
    puts("Error, creating Data thread");
    error = GRPC_WSA_ERROR(GetLastError(), "ThreadCreation");
  }
  return error;
}

grpc_error* CreateThreadProcess(grpc_thread_handle* thread) {
    //grpc_core::ExecCtx exec_ctx;
  DWORD dwThreadId = 0;
  grpc_error* error = NULL;
  HANDLE t;

    t = CreateThread(NULL,            // no security attribute
                     0,               // default stack size
                     InstanceThread,  // thread proc
                     (LPVOID)thread,  // thread parameter
                     0,               // not suspended
                     &dwThreadId);
    if (t != NULL) {
      puts("Thread Successfully created and running.....");
      CloseHandle(t);
    } else {
      puts("Error, creating thread");
      error = GRPC_WSA_ERROR(GetLastError(), "ThreadCreation");
    }
    return error;
}
