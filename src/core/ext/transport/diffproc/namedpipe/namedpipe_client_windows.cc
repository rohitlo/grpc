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


 

typedef struct{
    grpc_closure* on_done;
    grpc_closure on_connect;
    HANDLE handle;
    int refs;
    const char* addr_name;
    grpc_endpoint** endpoint;
    grpc_channel_args* channel_args;
}connection_details;



static void on_connect(void* cdc, grpc_error* error){
    printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
    connection_details* cd = (connection_details*) cdc;
    grpc_endpoint** ep = cd->endpoint;
    HANDLE hd = cd->handle;
    grpc_closure* on_done = cd->on_done;
    printf("\n 65 on_connect n_c_w Endpoint ptr : %p %p \n ", ep, &ep);
    if(error == GRPC_ERROR_NONE){
        *ep = grpc_namedpipe_create(hd, cd->channel_args, cd->addr_name, 1);
        //hd = NULL;
    }else{
        puts("Fail");
    }
    printf("\n 72 on_connect n_c_w Endpoint ptr : %p %p \n ", ep, &ep);
    printf("\n 72 Endpoint ptr : %p and handle : %p \n ", cd->endpoint, hd);
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, error);
}



 void np_connect(grpc_closure* on_done, grpc_endpoint** ep,
                             const grpc_channel_args* channel_args, const char* addr){
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
    }else{
        puts("Connected to server successfully...");
      printf("\n 104 n_c_w CLIENT SIDE HANDLE : %p\n", clientHandle);
      pipeMode = PIPE_READMODE_MESSAGE;
      fSuccess = SetNamedPipeHandleState(clientHandle,      // pipe handle
                                        &pipeMode,  // new pipe mode
                                         NULL,       // don't set maximum bytes
                                         NULL);      // don't set maximum time 

       if (!fSuccess) {
        puts("Failed changing modes");
         error = GRPC_WSA_ERROR(WSAGetLastError(), "PIPEMODE");
        goto failure;
        }

       else {
         // Testing purpose
         LPCTSTR lpvMessage =
             TEXT("Hey this is the first time I am trying to use pipe");
         DWORD cbToWrite = 4096;
         DWORD cbWritten;
         _tprintf(TEXT("Sending %d byte message: \"%s\"\n"), cbToWrite,
                  lpvMessage);
           fSuccess = WriteFile(clientHandle,  // pipe handle
                                lpvMessage,    // message
                                cbToWrite,     // message length
                                &cbWritten,    // bytes written
                                NULL);
           if (!fSuccess) {
             _tprintf(TEXT("WriteFile to pipe failed. GLE=%d\n"),
                      GetLastError());
             error = GRPC_WSA_ERROR(WSAGetLastError(), "PIPEMODE");
             goto failure;
           }

           printf("\nMessage sent to server, receiving reply as follows:\n");
         }
         // if (!fSuccess) {
         //  puts("Failed Writing modes");
         //  goto failure;
         //}
         cd = (connection_details*)gpr_malloc(sizeof(connection_details));
         cd->on_done = on_done;
         cd->refs = 2;
         cd->endpoint = ep;
         cd->handle = clientHandle;
         cd->addr_name = addr;
         cd->channel_args = grpc_channel_args_copy(channel_args);
         printf("\n 124 np_connect n_c_w Endpoint ptr : %p %p \n ", ep, &ep);
         on_connect(cd, GRPC_ERROR_NONE);
         return;
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
