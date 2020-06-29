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
    if(error == GRPC_ERROR_NONE){
        *ep = grpc_namedpipe_create(hd, cd->channel_args, cd->addr_name);
        hd = NULL;
    }else{
        puts("Fail");
    }
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, error);
}



 void np_connect(grpc_closure* on_done, grpc_endpoint** ep,
                             const grpc_channel_args* channel_args, const char* addr){
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
    HANDLE clientHandle = nullptr;
    BOOL fSuccess;
    connection_details* cd;
    char writeBuffer[] = "Hey Server, How are you? \n";
    char readBuffer[1023];
    DWORD writeBufferSize = strlen(writeBuffer);
    DWORD bytesRead, bytesWrite = 0;
    DWORD pipeMode;
    grpc_error* error;
    *ep = NULL;
    puts("Starting Client.....");

    clientHandle = CreateFile(TEXT(addr),
        GENERIC_READ | GENERIC_WRITE, // Read and Write Access
        0, //No sharing of file
        NULL, // Security
        OPEN_EXISTING, // Open existing pipe
        0, //Default Attrs
        NULL // No template file
        );
    
    if (clientHandle == INVALID_HANDLE_VALUE) {
        printf("Cannot create and connnect to named pipe at server end..");
        error = GRPC_WSA_ERROR(WSAGetLastError(), "WSASocket");
        goto failure;
    }else{
        puts("Connected to server successfully...");
        cd = (connection_details*)gpr_malloc(sizeof(connection_details));
        cd->on_done = on_done;
        cd->refs = 2;
        cd->endpoint = ep;
        cd->addr_name = addr;
        cd->channel_args = grpc_channel_args_copy(channel_args);
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
