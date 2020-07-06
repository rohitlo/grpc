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

//#ifdef GRPC_WINSOCK_SOCKET

#include "src/core/lib/iomgr/sockaddr_windows.h"

#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/log_windows.h>

#include "src/core/ext/transport/diffproc/namedpipe/namedpipe_server.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/iocp_windows.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/socket_windows.h"
#include "src/core/lib/iomgr/timer.h"
#include <src\core\ext\transport\diffproc\namedpipe\namedpipe_windows.h>
#include <tchar.h>
#include <strsafe.h>
#define INSTANCES 4
#define BUFSIZE 4096
//#define BUFSIZE 1023;
typedef struct grpc_pipeInstance {
  //HANDLE handle;
  /* The listener HANDLE. */
  HANDLE handle;
  const char* addr;
  grpc_np_server* server;
  int shutting_down;
  /* closure for socket notification of accept being ready */
  grpc_closure on_accept;
  OVERLAPPED op;
  BOOL fPendingIO;
  DWORD dwState;
  DWORD cbRead;
  DWORD cbToWrite;
  TCHAR chReply[BUFSIZE];
  int outstanding_calls;
  HANDLE new_handle;
  TCHAR chRequest[BUFSIZE] = {};

  struct grpc_pipeInstance* next;
} grpc_pipeInstance, *LPgrpc_piepInstance;

/* the overall server */
struct grpc_np_server {
  gpr_refcount refs;
  /* Called whenever accept() succeeds on a server port. Sends success msg to calling function*/
  grpc_np_server_cb on_accept_cb;
  void* on_accept_cb_arg;

  gpr_mu mu;

  /* linked list of server ports */
  grpc_pipeInstance* head;
  grpc_pipeInstance* tail;

  /* List of closures passed to shutdown_starting_add(). */
  grpc_closure_list shutdown_starting;

  /* shutdown callback */
  grpc_closure* shutdown_complete;

  grpc_channel_args* channel_args;


  HANDLE hEvents[INSTANCES];
  int active_ports;

  //Pipe storage in server--
  grpc_pipeInstance* pipes[INSTANCES];
  int count;
  int pipesCount;
};



static VOID DisconnectAndReconnect(grpc_np_server*, DWORD);
static BOOL ConnectToNewClient(HANDLE, LPOVERLAPPED);
static VOID GetAnswerToRequest(LPgrpc_piepInstance); 


VOID GetAnswerToRequest(LPgrpc_piepInstance pipe) {
  _tprintf(TEXT("[%d] %s\n"), pipe->handle, pipe->chRequest);
  StringCchCopy(pipe->chReply, BUFSIZE, TEXT("Default answer from server"));
  pipe->cbToWrite = (lstrlen(pipe->chReply) + 1) * sizeof(TCHAR);
}


/* Public function. Allocates the proper data structures to hold a
   grpc_pipe_server. */

   // 1.
grpc_error* grpc_namedpipe_create(grpc_closure* shutdown_complete,
                                     const grpc_channel_args* args,
                                     grpc_np_server** server) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  grpc_np_server* s = (grpc_np_server*)gpr_malloc(sizeof(grpc_np_server));
  s->channel_args = grpc_channel_args_copy(args);
  gpr_ref_init(&s->refs, 1);
  gpr_mu_init(&s->mu);
  s->active_ports = 0;
  s->count = 0;
  s->pipesCount = 0;
  //s->pipes = static_cast<grpc_pipeInstance**>(gpr_malloc(sizeof(*s->pipes) * 4));
  s->on_accept_cb = NULL;
  s->on_accept_cb_arg = NULL;
  s->head = NULL;
  s->tail = NULL;
  s->shutdown_starting.head = NULL;
  s->shutdown_starting.tail = NULL;
  s->shutdown_complete = shutdown_complete;
  *server = s;
  return GRPC_ERROR_NONE;
}

static void destroy_server(void* arg, grpc_error* error) {
  grpc_np_server* s = (grpc_np_server*)arg;

  /* Now that the accepts have been aborted, we can destroy the sockets.
     The IOCP won't get notified on these, so we can flag them as already
     closed by the system. */
  while (s->head) {
    grpc_pipeInstance* sp = s->head;
    s->head = sp->next;
    sp->next = NULL;
    grpc_winsocket_destroy((grpc_winsocket*)sp->handle);
    gpr_free(sp);
  }
  grpc_channel_args_destroy(s->channel_args);
  gpr_mu_destroy(&s->mu);
  gpr_free(s);
}

static void finish_shutdown_locked(grpc_np_server* s) {
  if (s->shutdown_complete != NULL) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, s->shutdown_complete,
                            GRPC_ERROR_NONE);
  }

  grpc_core::ExecCtx::Run(
      DEBUG_LOCATION,
      GRPC_CLOSURE_CREATE(destroy_server, s, grpc_schedule_on_exec_ctx),
      GRPC_ERROR_NONE);
}


 grpc_np_server* grpc_np_server_ref(grpc_np_server* s) {
  gpr_ref_non_zero(&s->refs);
  return s;
}

static void np_server_shutdown_starting_add(grpc_np_server* s,
                                             grpc_closure* shutdown_starting) {
  gpr_mu_lock(&s->mu);
  grpc_closure_list_append(&s->shutdown_starting, shutdown_starting,
                           GRPC_ERROR_NONE);
  gpr_mu_unlock(&s->mu);
}

static void np_server_destroy(grpc_np_server* s) {
  grpc_pipeInstance* sp;
  gpr_mu_lock(&s->mu);

  /* First, shutdown all fd's. This will queue abortion calls for all
     of the pending accepts due to the normal operation mechanism. */
  if (s->active_ports == 0) {
    finish_shutdown_locked(s);
  } else {
    for (sp = s->head; sp; sp = sp->next) {
      sp->shutting_down = 1;
      grpc_winsocket_shutdown((grpc_winsocket*)sp->handle);
    }
  }
  gpr_mu_unlock(&s->mu);
}

 void grpc_np_server_unref(grpc_np_server* s) {
  if (gpr_unref(&s->refs)) {
    grpc_np_server_shutdown_listeners(s);
    gpr_mu_lock(&s->mu);
    grpc_core::ExecCtx::RunList(DEBUG_LOCATION, &s->shutdown_starting);
    gpr_mu_unlock(&s->mu);
    np_server_destroy(s);
  }
}

void grpc_np_server_shutdown_listeners(grpc_np_server* s) {}

static void decrement_active_ports_and_notify_locked(grpc_pipeInstance* sp) {
  sp->shutting_down = 0;
  GPR_ASSERT(sp->server->active_ports > 0);
  if (0 == --sp->server->active_ports) {
    finish_shutdown_locked(sp->server);
  }
}


BOOL ConnectToNewClient(HANDLE hPipe, LPOVERLAPPED lpo) {
  BOOL fConnected, fPendingIO = FALSE;

  // Start an overlapped connection for this pipe instance.
  fConnected = ConnectNamedPipe(hPipe, lpo);

  // Overlapped ConnectNamedPipe should return zero.
  if (fConnected) {
    printf("ConnectNamedPipe failed with %d.\n", GetLastError());
    return 0;
  }
  //else {
  //  puts("Client Successfully connected...");
  //}

  switch (GetLastError()) {
      // The overlapped connection in progress.
    case ERROR_IO_PENDING:
      puts("Pipe IO PENDING connected ");
      fPendingIO = TRUE;
      break;

      // Client is already connected, so signal an event.

    case ERROR_PIPE_CONNECTED:
      puts("Pipe already connected ");
      if (SetEvent(lpo->hEvent)) break;

      // If an error occurs during the connect operation...
    default: {
      printf("ConnectNamedPipe failed with %d.\n", GetLastError());
      return 0;
    }
  }

  return fPendingIO;
}


#define CONNECTING_STATE 0
#define READING_STATE 1
#define WRITING_STATE 2
// 5. START ACCEPT LOCKED FOR ASYNC INCOMING CONNECTIONS
static grpc_error* start_accept_locked(grpc_pipeInstance* pipeInstance) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  if (pipeInstance->shutting_down) {
    return GRPC_ERROR_NONE;
  }

  pipeInstance->fPendingIO =
      ConnectToNewClient(pipeInstance->handle, &pipeInstance->op);
  printf("\n start_accept_locked :: Pendiong IO ? : %d\n",
         pipeInstance->fPendingIO);
  pipeInstance->dwState = pipeInstance->fPendingIO ? CONNECTING_STATE :  // still connecting
                 READING_STATE; // Ready to read
  printf("\n start_accept_locked :: Pendiong IO ? : %d\n",
         pipeInstance->dwState);
  return GRPC_ERROR_NONE;
}


VOID DisconnectAndReconnect(grpc_np_server* server, DWORD i) {
  // Disconnect the pipe instance.

  if (!DisconnectNamedPipe(server->pipes[i]->handle)) {
    printf("DisconnectNamedPipe failed with %d.\n", GetLastError());
  }

  // Call a subroutine to connect to the new client.

  server->pipes[i]->fPendingIO =
      ConnectToNewClient(server->pipes[i]->handle, &server->pipes[i]->op);
  printf("\n DisconnectAndReconnect :: Pendiong IO ? : %d\n",
         server->pipes[i]->fPendingIO);
  server->pipes[i]->dwState =
      server->pipes[i]->fPendingIO ? CONNECTING_STATE                  :  // still connecting
                                READING_STATE;                       // ready to read
  printf("\n DisconnectAndReconnect :: DW State ? : %d\n",
         server->pipes[i]->dwState);
  
} 

/* Event manager callback when reads are ready. */
static void on_accept(void* arg, grpc_error* error) {
  grpc_np_server* server = (grpc_np_server*)arg;
  grpc_endpoint* ep = NULL;
  DWORD i, dwWait, cbRet, dwErr;
  BOOL fSuccess; 
  
  while (1) {
    // Wait for the event object to be signaled, indicating
    // completion of an overlapped read, write, or
    // connect operation. 
     
      DWORD dwWait = WaitForMultipleObjects(4,  // number of event objects
                                    server->hEvents,    // array of event objects
                                    FALSE,      // does not wait for all
                                    INFINITE);  // waits indefinitely 

    // dwWait shows which pipe completed the operation.

        i = dwWait - WAIT_OBJECT_0;  // determines which pipe
        if (i < 0 || i > (4 - 1)) {
          printf("Index out of range.\n");
          break;
        }
        

        // Get the result if the operation was pending.
        
        if (server->pipes[i]->fPendingIO) {
          fSuccess =
              GetOverlappedResult(server->pipes[i]->handle,  // handle to pipe
                                  &server->pipes[i]->op,  // OVERLAPPED structure
                                  &cbRet,             // bytes transferred
                                  FALSE);             // do not wait

          switch (server->pipes[i]->dwState) {
              // Pending connect operation
            case CONNECTING_STATE:
              puts("pending Connecting state");
              if (!fSuccess) {
                printf("Error %d.\n", GetLastError());
              }
              server->pipes[i]->dwState = READING_STATE;
              break;

              // Pending read operation
            case READING_STATE:
              puts("pending Read state");
              if (!fSuccess || cbRet == 0) {
                DisconnectAndReconnect(server, i);
                continue;
              }
              //_tprintf(TEXT("[%d] %s\n"), server->pipes[i]->handle,
              //         server->pipes[i]->chRequest);
              //printf("\n Read message  :%s\n", server->pipes[i]->chRequest);
              server->pipes[i]->cbRead = cbRet;
              server->pipes[i]->dwState = WRITING_STATE;
              break;

              // Pending write operation
            case WRITING_STATE:
              puts("Pending write state");
              if (!fSuccess || cbRet != server->pipes[i]->cbToWrite) {
                DisconnectAndReconnect(server, i);
                continue;
              }
              server->pipes[i]->dwState = READING_STATE;
              /*break*/;
            
            default: {
              printf("Invalid pipe state.\n");
              return;
            }
          }
        }

        // The pipe state determines which operation to do next.
        switch (server->pipes[i]->dwState) {
            // READING_STATE:
            // The pipe instance is connected to the client
            // and is ready to read a request from the client.

        case READING_STATE:
            puts("current read state");

            /* Endpoint Creation */

            ep = grpc_namedpipe_create(server->pipes[i]->handle,
                                       server->channel_args, "server", 0);

            if (ep) {
              // Create acceptor.
              grpc_np_server_acceptor* acceptor =
                  (grpc_np_server_acceptor*)gpr_malloc(sizeof(*acceptor));
              acceptor->from_server = server;
              acceptor->external_connection = false;
              server->on_accept_cb(server->on_accept_cb_arg, ep, NULL, acceptor);
              puts("Success creating endpoint **********************");
              server->pipes[i]->fPendingIO = FALSE;
              server->pipes[i]->dwState = WRITING_STATE;
              continue;
            }
            /* Endpoint Creation end */
            else {
              puts("Error Creating endpoint");
              puts("ERROR ops still pending...");
              server->pipes[i]->fPendingIO = TRUE;
              continue;
            }
            DisconnectAndReconnect(server, i);
            break;
        default: {
            printf("Invalid pipe state.\n");
            return ;
          }
        }
  }


}




// 3. Future Implementation for IOCP Sockets
static grpc_error* add_pipe_to_server(grpc_np_server* s, HANDLE hd,
                                      const char* target_addr,
                                      grpc_pipeInstance* pipeInstance) {
  printf("\n%d :: %s :: %s\n",__LINE__,__func__, __FILE__);
  grpc_pipeInstance* sp = pipeInstance;
  grpc_error* error = GRPC_ERROR_NONE;
  gpr_mu_lock(&s->mu);
  s->pipes[s->pipesCount++] = sp;
  sp->next = NULL;
  if (s->head == NULL) {
    s->head = sp;
  } else {
    s->tail->next = sp;
  }
  s->tail = sp;
  sp->server = s;
  sp->handle = hd;
  sp->shutting_down = 0;
  sp->outstanding_calls = 0;
  //DWORD FileSize = ftell(myfile); 
  sp->new_handle = INVALID_HANDLE_VALUE;
  //GRPC_CLOSURE_INIT(&sp->on_accept, on_accept, s, grpc_schedule_on_exec_ctx);
  GPR_ASSERT(sp->handle);
  gpr_mu_unlock(&s->mu);
  pipeInstance = sp;
  printf("\n%p : \n", pipeInstance);
  return GRPC_ERROR_NONE;
}


//2. Pipe Creation for several Instances.
 grpc_error* grpc_server_np_add_port(grpc_np_server* s, const char* addr) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
   grpc_pipeInstance* pipeInstance = (grpc_pipeInstance*)gpr_malloc(sizeof(grpc_pipeInstance));
  HANDLE namedPipe;
  grpc_error* error;
  //int BUFSIZE = 1023;
  s->hEvents[s->count] = CreateEvent(NULL,   // default security attribute
                                  TRUE,   // manual-reset event
                                  TRUE,   // initial state = signaled
                                  NULL);  // unnamed event object 

   if (s->hEvents[s->count] == NULL) {
    printf("CreateEvent failed with %d.\n", GetLastError());
     return GRPC_WSA_ERROR(GetLastError(), "Event Creation Failed");
  }

   pipeInstance->op.hEvent = s->hEvents[s->count];
  pipeInstance->op.Offset = 2048;
   pipeInstance->op.OffsetHigh = 2048;

  namedPipe =
       CreateNamedPipe(TEXT(addr),                  // pipe name
                              PIPE_ACCESS_DUPLEX |         // read/write access
                              FILE_FLAG_OVERLAPPED,    // overlapped mode
                              PIPE_TYPE_MESSAGE |          // message-type pipe
                              PIPE_READMODE_MESSAGE |  // message-read mode
                              PIPE_WAIT,               // blocking mode
                              4,                // number of instances
                              BUFSIZE * sizeof(BYTE),  // output buffer size
                              BUFSIZE * sizeof(BYTE),  // input buffer size
                              0,             // client time-out
                              NULL);  // default security attributes 

    s->count++;
    if (namedPipe == INVALID_HANDLE_VALUE) {
        puts("Error");
        error = GRPC_WSA_ERROR(WSAGetLastError(), "NamedPipe");
        goto done;
    }else{

      return error = add_pipe_to_server(s, namedPipe, addr, pipeInstance);
    }

done:
    if (error != GRPC_ERROR_NONE) {
        grpc_error* error_out = GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
            "Failed to add port to server", &error, 1);
        GRPC_ERROR_UNREF(error);
        error = error_out;
  } else {
    GPR_ASSERT(pipeInstance != NULL);
  }
  return error;

 }






//4. 
void grpc_np_server_start(grpc_np_server* s, grpc_pollset** pollset,size_t pollset_count,
                             grpc_np_server_cb on_accept_cb,
                             void* on_accept_cb_arg) {
   printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
   grpc_pipeInstance* sp = NULL;
   GPR_ASSERT(on_accept_cb);
   gpr_mu_lock(&s->mu);
   GPR_ASSERT(!s->on_accept_cb);
   GPR_ASSERT(s->active_ports == 0);
   s->on_accept_cb = on_accept_cb;
   s->on_accept_cb_arg = on_accept_cb_arg;
   int i=0;
   for (sp = s->head; sp; sp = sp->next) {
     GPR_ASSERT(GRPC_LOG_IF_ERROR("start_accept", start_accept_locked(sp)));
     //s->hEvents[i++] = sp->hEvent;
     s->active_ports++;
   }
   on_accept(s, GRPC_ERROR_NONE);
   gpr_mu_unlock(&s->mu);
 }





//#endif  // GRPC_WINSOCK_SOCKET
