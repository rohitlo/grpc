/*
 *
 * Copyright 2015 gRPC authors.
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

#include "src/core/lib/iomgr/port.h"

#include <inttypes.h>

#ifdef GRPC_WINSOCK_SOCKET

#include "src/core/lib/iomgr/sockaddr_windows.h"

#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/log_windows.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/iocp_windows.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/socket_windows.h"
#include "src/core/lib/iomgr/timer.h"
#include <src\core\lib\iomgr\endpoint.h>
#include <src/core/ext/transport/diffproc/namedpipe/namedpipe_windows.h>
#include <src/core/ext/transport/diffproc/namedpipe/namedpipe_client.h>
#include <tchar.h>
#include <src\core\lib\transport\transport.h>


static void async_connect_unlock_and_cleanup(connection_details* ac,
                                             HANDLE socket) {
  int done = (--ac->refs == 0);
  gpr_mu_unlock(&ac->mu);
  if (done) {
    grpc_channel_args_destroy(ac->channel_args);
    gpr_mu_destroy(&ac->mu);
    gpr_free(ac);
  }
  if (socket != NULL) CloseHandle(socket);
}

static void on_connect(void* arg, void* cdc, grpc_error* error){
    printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
    connection_details* cd = (connection_details*) cdc;
    grpc_endpoint** ep = cd->endpoint;
    gpr_mu_unlock(&cd->mu);
    gpr_mu_lock(&cd->mu);
    HANDLE hd = cd->handle;
    cd->handle = NULL;
    gpr_mu_unlock(&cd->mu);
    gpr_mu_lock(&cd->mu);
    /*grpc_closure* on_done = cd->on_done;*/
    if(error == GRPC_ERROR_NONE){
        *ep = grpc_namedpipe_create(hd, cd->channel_args, cd->addr_name, 1);
        cd->clientsidedetails->endpoint = *ep;
        cd->clientsidedetails->hd = hd;
        printf("\n%d :: %s :: %s :: %p :: %p\n", __LINE__, __func__, __FILE__,
               *ep, hd);
        hd = NULL;
    }else{
        puts("Fail");
    }
    async_connect_unlock_and_cleanup(cd, hd);
    cd->done(cd->clientsidedetails,error);
}



 void np_connect(grpc_closure* on_done, grpc_endpoint** ep,
                const grpc_channel_args* channel_args, const char* addr,
                conndetails* condetail, void* done) {
  grpc_on_done d = (grpc_on_done)done;
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
    HANDLE clientHandle;
    BOOL fSuccess;
    connection_details* cd;
    DWORD pipeMode;
    grpc_error* error = GRPC_ERROR_NONE;
    *ep = NULL;
    puts("Starting Client....."); 
    printf("\n Address of pipe is %s\n", addr);
    clientHandle = CreateFile("\\\\.\\pipe\\namedpipe",
        GENERIC_READ | GENERIC_WRITE, // Read and Write Access
        0, //No sharing of file
        NULL, // Security
        OPEN_EXISTING, // Open existing pipe
                    FILE_ATTRIBUTE_NORMAL,              // Default Attrs
        NULL // No template file
        );
    
    if (clientHandle == INVALID_HANDLE_VALUE) {
        printf("Cannot create and connnect to named pipe at server end..");
        error = GRPC_WSA_ERROR(WSAGetLastError(), "WSASocket");
        goto failure;
    } else {
      puts("Connected to server successfully...");
      pipeMode = PIPE_READMODE_MESSAGE;
      fSuccess = SetNamedPipeHandleState(clientHandle,  // pipe handle
                                         &pipeMode,     // new pipe mode
                                         NULL,   // don't set maximum bytes
                                         NULL);  // don't set maximum time

      if (!fSuccess) {
        puts("Failed changing modes");
        error = GRPC_WSA_ERROR(WSAGetLastError(), "PIPEMODE");
        goto failure;
      }

      else {

        cd = (connection_details*)gpr_malloc(sizeof(connection_details));
        cd->refs = 2;
        //cd->on_done = on_done;
        cd->endpoint = ep;
        cd->handle = clientHandle;
        cd->addr_name = addr;
        cd->channel_args = grpc_channel_args_copy(channel_args);
        cd->clientsidedetails = condetail;
        cd->done = d;
        printf("\n%d :: %s :: %s :: %p :: %p\n", __LINE__, __func__, __FILE__,
               cd->endpoint, cd->handle);
        on_connect(done, cd, error);
        return;
      }
    }

failure:
    GPR_ASSERT(error != GRPC_ERROR_NONE);
     grpc_error* final_error =
      grpc_error_set_str(GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                             "Failed to connect", &error, 1),
                         GRPC_ERROR_STR_TARGET_ADDRESS,
                         grpc_slice_from_copied_string(
                             addr == nullptr ? "NULL" : addr));
    GRPC_ERROR_UNREF(error);
    if(clientHandle != INVALID_HANDLE_VALUE){
        CloseHandle(clientHandle);
    }

}
#endif  // GRPC_WINSOCK_SOCKET
