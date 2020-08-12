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
#include "src/core/lib/iomgr/timer.h"
#include <src\core\ext\transport\diffproc\namedpipe\namedpipe_windows.h>
#include <tchar.h>
#include <strsafe.h>
#include <src/core/ext/transport/diffproc/namedpipe_thread.h>
#define INSTANCES 10
#define BUFSIZE 8192
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
  //OVERLAPPED op;
  //BOOL fPendingIO;
  DWORD dwState;
  DWORD cbRead;
  DWORD cbToWrite;
  //TCHAR chReply[BUFSIZE];
  int outstanding_calls;
  HANDLE new_handle;
  //TCHAR chRequest[BUFSIZE] = {};

  grpc_thread_handle* np_handle;

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


  //HANDLE hEvents[INSTANCES];
  int active_ports;

  //Pipe storage in server--
  //grpc_pipeInstance* pipes[INSTANCES];
  int count;
  int pipesCount;
};



//static VOID DisconnectAndReconnect(grpc_np_server*, DWORD);
//static BOOL ConnectToNewClient(HANDLE, LPOVERLAPPED);
//static VOID GetAnswerToRequest(LPgrpc_piepInstance); 


//VOID GetAnswerToRequest(LPgrpc_piepInstance pipe) {
//  _tprintf(TEXT("[%d] %s\n"), pipe->handle, pipe->chRequest);
//  StringCchCopy(pipe->chReply, BUFSIZE, TEXT("Default answer from server"));
//  pipe->cbToWrite = (lstrlen(pipe->chReply) + 1) * sizeof(TCHAR);
//}


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

  /* Now that the accepts have been aborted, we can destroy the pipe hanlde. */
  while (s->head) {
    grpc_pipeInstance* sp = s->head;
    s->head = sp->next;
    sp->next = NULL;
    grpc_nphandle_destroy(sp->np_handle);
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
  grpc_closure_list_append(&s->shutdown_starting, shutdown_starting, GRPC_ERROR_NONE);
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
      grpc_nphandle_shutdown(sp->np_handle);
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

static HANDLE CreateInstance(const char* addr) {
  HANDLE hd = CreateNamedPipe(TEXT(addr),                  // pipe name
                       PIPE_ACCESS_DUPLEX |         // read/write access
                           FILE_FLAG_OVERLAPPED,    // overlapped mode
                       PIPE_TYPE_BYTE |          // message-type pipe
                           PIPE_READMODE_BYTE |  // message-read mode
                           PIPE_WAIT,               // blocking mode
                       PIPE_UNLIMITED_INSTANCES,                   // number of instances
                       BUFSIZE * sizeof(BYTE),      // output buffer size
                       BUFSIZE * sizeof(BYTE),      // input buffer size
                       0,                           // client time-out
                       NULL);  // default security attributes
  printf("Named pipe Instance created HANDLE :%p \n", hd);
  return hd;
}

//BOOL ConnectToNewClient(HANDLE hPipe, LPOVERLAPPED lpo) {
//  BOOL fConnected, fPendingIO = FALSE;
//
//  // Start an overlapped connection for this pipe instance.
//  fConnected = ConnectNamedPipe(hPipe, lpo);
//
//  // Overlapped ConnectNamedPipe should return zero.
//  if (fConnected) {
//    printf("ConnectNamedPipe failed with %d.\n", GetLastError());
//    return 0;
//  }
//  //else {
//  //  puts("Client Successfully connected...");
//  //}
//
//  switch (GetLastError()) {
//      // The overlapped connection in progress.
//    case ERROR_IO_PENDING:
//      puts("Pipe IO PENDING connected ");
//      fPendingIO = TRUE;
//      break;
//
//      // Client is already connected, so signal an event.
//
//    case ERROR_PIPE_CONNECTED:
//      puts("Pipe already connected ");
//      if (SetEvent(lpo->hEvent)) break;
//
//      // If an error occurs during the connect operation...
//    default: {
//      printf("ConnectNamedPipe failed with %d.\n", GetLastError());
//      return 0;
//    }
//  }
//
//  return fPendingIO;
//}

//DWORD WINAPI Ins(LPVOID lpvaram) {
//  puts("In instance thread");
//  return 1;
//}

/*Error callback at namedpipe connection*/
static void on_error(void* arg, grpc_error* error) {
  puts("error connectin got namepipe");
}

static void on_accept(void* arg, grpc_error* error);


// 5. START ACCEPT LOCKED FOR ASYNC INCOMING CONNECTIONS
static grpc_error* start_accept_locked(grpc_pipeInstance* pipeInstance) {
  //grpc_core::ExecCtx exec_ctx;
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  int connectSuccess = 0;
  grpc_error* error = GRPC_ERROR_NONE;
  DWORD dwThreadId = 0;
  HANDLE newPipeHandle = CreateInstance(pipeInstance->addr);
  if (newPipeHandle == INVALID_HANDLE_VALUE) {
    puts("Error creating new pipe Instance for upcoming connects");
    goto failure;
  }
  printf("Handle to listen to : %p \n", pipeInstance->np_handle->pipeHandle);


  //Initiating on_accept callback function to be called in thread process, when a client successfully connects to server.
  pipeInstance->np_handle->grpc_on_accept = on_accept;
  pipeInstance->np_handle->grpc_on_error = on_error;
  pipeInstance->np_handle->arg = pipeInstance;
  error = CreateThreadProcess(pipeInstance->np_handle);
  if (error != GRPC_ERROR_NONE) {
    puts("Some failure in creating thread");
    goto failure;
  }
  pipeInstance->new_handle = newPipeHandle;
  
  return error;

  failure:
    printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
    GPR_ASSERT(error != GRPC_ERROR_NONE);
    if (newPipeHandle != INVALID_HANDLE_VALUE) CloseHandle(newPipeHandle);
    return error;
  }


//VOID DisconnectAndReconnect(grpc_np_server* server, DWORD i) {
//  // Disconnect the pipe instance.
//
//  if (!DisconnectNamedPipe(server->pipes[i]->handle)) {
//    printf("DisconnectNamedPipe failed with %d.\n", GetLastError());
//  }
//
//  // Call a subroutine to connect to the new client.
//
//  server->pipes[i]->fPendingIO =
//      ConnectToNewClient(server->pipes[i]->handle, &server->pipes[i]->op);
//  printf("\n DisconnectAndReconnect :: Pendiong IO ? : %d\n",
//         server->pipes[i]->fPendingIO);
//  server->pipes[i]->dwState =
//      server->pipes[i]->fPendingIO ? CONNECTING_STATE                  :  // still connecting
//                                READING_STATE;                       // ready to read
//  printf("\n DisconnectAndReconnect :: DW State ? : %d\n",
//         server->pipes[i]->dwState);
//  
//} 



/* Event manager callback when reads are ready. */
static void on_accept(void* arg, grpc_error* error) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  grpc_pipeInstance* pipe = static_cast<grpc_pipeInstance*>(arg);
  HANDLE currHandle = pipe->handle;
  printf("Named pipe CURR ACCEPT HANDLE :%p \n", currHandle);
  grpc_endpoint* ep = NULL;
  gpr_mu_unlock(&pipe->server->mu);
  gpr_mu_lock(&pipe->server->mu);

  if (error != GRPC_ERROR_NONE) {
    const char* msg = grpc_error_string(error);
    gpr_log(GPR_INFO, "Skipping on_accept due to error: %s", msg);

    gpr_mu_unlock(&pipe->server->mu);
    return;
  }

  if (!pipe->shutting_down) {
    ep = grpc_namedpipe_create(pipe->np_handle,
                               pipe->server->channel_args,
                               "server", 0);
  } else {
    CloseHandle(currHandle);
  }

  if (ep) {
    grpc_np_server_acceptor* acceptor =
        (grpc_np_server_acceptor*)gpr_malloc(sizeof(*acceptor));
    acceptor->from_server = pipe->server;

    pipe->server->on_accept_cb(pipe->server->on_accept_cb_arg, ep, NULL, acceptor);
    pipe->handle = NULL;
  }

  pipe->handle = pipe->new_handle;
  pipe->np_handle = grpc_createHandle(pipe->new_handle, "Listener");
  printf("Named pipe NEXT ACCEPT HANDLE :%p \n", pipe->handle);
  GPR_ASSERT(GRPC_LOG_IF_ERROR("start_accept", start_accept_locked(pipe)));

  gpr_mu_unlock(&pipe->server->mu);
}




// 3. Future Implementation for IOCP Sockets
static grpc_error* add_pipe_to_server(grpc_np_server* s, HANDLE hd,
                                      const char* target_addr,
                                      grpc_pipeInstance** pipeInstance) {
  printf("\n%d :: %s :: %s\n",__LINE__,__func__, __FILE__);
  grpc_pipeInstance* sp = NULL;
  grpc_error* error = GRPC_ERROR_NONE;
  gpr_mu_lock(&s->mu);
  //s->pipes[s->pipesCount++] = sp;
  sp = (grpc_pipeInstance*)gpr_malloc(sizeof(grpc_pipeInstance));
  sp->next = NULL;
  if (s->head == NULL) {
    s->head = sp;
  } else {
    s->tail->next = sp;
  }
  s->tail = sp;
  sp->server = s;
  sp->addr = target_addr;
  sp->np_handle = grpc_createHandle(hd, "listener");
  sp->handle = hd;
  sp->shutting_down = 0;
  sp->outstanding_calls = 0;
  sp->new_handle = INVALID_HANDLE_VALUE;
  //GRPC_CLOSURE_INIT(&sp->on_accept, on_accept, sp, grpc_schedule_on_exec_ctx);
  GPR_ASSERT(sp->np_handle);
  gpr_mu_unlock(&s->mu);
  *pipeInstance = sp;
  return GRPC_ERROR_NONE;
}


//2. Pipe Creation for several Instances.
 grpc_error* grpc_server_np_add_port(grpc_np_server* s, const char* addr) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  grpc_pipeInstance* pipeInstance = NULL;
  grpc_thread_handle* tHandle =
      (grpc_thread_handle*)gpr_malloc(sizeof(grpc_thread_handle));
  grpc_error* error;
  HANDLE namedPipe = CreateInstance(addr);
  s->count++;
  if (namedPipe == INVALID_HANDLE_VALUE) {
      puts("Error");
      error = GRPC_WSA_ERROR(GetLastError(), "NamedPipe");
      goto done;
  }else{
    puts("Succesfully created pipe instance *****************");
    return error = add_pipe_to_server(s, namedPipe, addr, &pipeInstance);
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
     s->active_ports++;
   }
   
   gpr_mu_unlock(&s->mu);
 }


//#endif  // GRPC_WINSOCK_SOCKET
