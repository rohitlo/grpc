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

#ifndef GRPC_CORE_EXT_TRANSPORT_DIFFPROC_NAMEDPIPE_THREAD_H
#define GRPC_CORE_EXT_TRANSPORT_DIFFPROC_NAMEDPIPE_THREAD_H

#include <grpc/support/port_platform.h>
#include  <stdio.h>
#include <src\core\lib\iomgr\closure.h>

typedef struct grpc_np_callback_info {

  OVERLAPPED overlap;

  grpc_closure* closure;

  int fPending;

  DWORD bytes_transferred;

  int np_error;

}grpc_np_callback_info;



typedef struct grpc_thread_handle {

  HANDLE threadHandle;
  HANDLE pipeHandle;

  
  grpc_np_callback_info write_info;
  grpc_np_callback_info read_info;

  grpc_closure* complete_closure;

  bool shutdown_called;

  gpr_mu state_mu;

  grpc_closure shutdown_closure;

} grpc_thread_handle;




/* Creates a thread for namedpipe to run operations after succesfull connection*/
grpc_error* CreateThreadProcess(grpc_thread_handle* thread);

grpc_thread_handle* grpc_createHandle(HANDLE hd, const char* name);




#endif /* GRPC_CORE_EXT_TRANSPORT_DIFFPROC_NAMEDPIPE_THREAD_H */
