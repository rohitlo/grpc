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

typedef struct grpc_np_listener grpc_np_listener;
struct grpc_np_listener {
  //HANDLE handle;
  /* The listener HANDLE. */
  HANDLE handle;
  /* The actual TCP port number. */
  const char* addr;
  grpc_np_server* server;
  int shutting_down;
  /* closure for socket notification of accept being ready */
  grpc_closure on_accept;
};

/* the overall server */
struct grpc_np_server {
  gpr_refcount refs;
  /* Called whenever accept() succeeds on a server port. Sends success msg to calling function*/
  grpc_np_server_cb on_accept_cb;
  void* on_accept_cb_arg;

  gpr_mu mu;

  /* linked list of server ports */
  grpc_np_listener* main;

  /* List of closures passed to shutdown_starting_add(). */
  grpc_closure_list shutdown_starting;

  /* shutdown callback */
  grpc_closure* shutdown_complete;

  grpc_channel_args* channel_args;


  int active_ports;
};


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
  s->on_accept_cb = NULL;
  s->on_accept_cb_arg = NULL;
  s->main = NULL;
  s->shutdown_starting.head = NULL;
  s->shutdown_starting.tail = NULL;
  s->shutdown_complete = shutdown_complete;
  *server = s;
  return GRPC_ERROR_NONE;
}

// 5.
static grpc_error* start_accept_locked(grpc_np_listener* lst) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  HANDLE hd = INVALID_HANDLE_VALUE;
  BOOL connect_pipe;
  DWORD bytes_received = 0;
  grpc_error* error = GRPC_ERROR_NONE;

  if (lst->shutting_down) {
    return GRPC_ERROR_NONE;
  }
  connect_pipe = ConnectNamedPipe(lst->handle, NULL);
  if (connect_pipe == FALSE) {
    puts("Error cannot connect to server");
    CloseHandle(lst->handle);
    error = GRPC_WSA_ERROR(WSAGetLastError(), "Unable to connect pipe");
    return error;
  } else {
    puts("Connection Success");
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, &lst->on_accept, GRPC_ERROR_NONE);
    return error;
  }
}




/* Event manager callback when reads are ready. */
static void on_accept(void* arg, grpc_error* error) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  grpc_np_listener* sp = (grpc_np_listener*)arg;
  HANDLE cPipe = sp->handle;
  grpc_endpoint* ep = NULL;
  DWORD transfered_bytes;
  DWORD flags;
  BOOL connect_pipe;
  const char* peer_name_string = "";

  int err;

  gpr_mu_lock(&sp->server->mu);

  /* The general mechanism for shutting down is to queue abortion calls. While
     this is necessary in the read/write case, it's useless for the accept
     case. We only need to adjust the pending callback count */
  if (error != GRPC_ERROR_NONE) {
    const char* msg = grpc_error_string(error);
    gpr_log(GPR_INFO, "Skipping on_accept due to error: %s", msg);

    gpr_mu_unlock(&sp->server->mu);
    return;
  }
  ep = grpc_namedpipe_create(sp->handle, sp->server->channel_args,
                             peer_name_string);

  /* The only time we should call our callback, is where we successfully
     managed to accept a connection, and created an endpoint. */
  if (ep) {
    // Create acceptor.
    grpc_np_server_acceptor* acceptor =
        (grpc_np_server_acceptor*)gpr_malloc(sizeof(*acceptor));
    acceptor->from_server = sp->server;
    acceptor->fd_index = 0;
    acceptor->external_connection = false;
    sp->server->on_accept_cb(sp->server->on_accept_cb_arg, ep, NULL, acceptor);
  }
  /* As we were notified from the IOCP of one and exactly one accept,
     the former socked we created has now either been destroy or assigned
     to the new connection. We need to create a new one for the next
     connection. */
  GPR_ASSERT(GRPC_LOG_IF_ERROR("start_accept", start_accept_locked(sp)));
  gpr_mu_unlock(&sp->server->mu);
}



// 3. Future Implementation for IOCP Sockets
static grpc_error* add_pipe_to_server(grpc_np_server* s, HANDLE hd,
                                      const char* target_addr,
                                      grpc_np_listener** listener) {
  printf("\n%d :: %s :: %s\n",__LINE__,__func__, __FILE__);
  grpc_np_listener* sp = NULL;
  gpr_mu_lock(&s->mu);
  GPR_ASSERT(!s->on_accept_cb && "must add ports before starting server");

  sp = (grpc_np_listener*)gpr_malloc(sizeof(grpc_np_listener));
  s->main = sp;
  sp->server = s;
  sp->handle = hd;
  sp->shutting_down = 0;
  sp->addr = target_addr;
  GRPC_CLOSURE_INIT(&sp->on_accept, on_accept, sp, grpc_schedule_on_exec_ctx);
  gpr_mu_unlock(&s->mu);
  *listener = sp;
  return GRPC_ERROR_NONE;
}


//2. 
 grpc_error* grpc_server_np_add_port(grpc_np_server* s,const char* addr){
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  grpc_np_listener* sp = NULL;
  HANDLE namedPipe;
  grpc_error* error;
  int BUFSIZE = 1023;
  namedPipe = CreateNamedPipe(TEXT(addr), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                      PIPE_UNLIMITED_INSTANCES, BUFSIZE, BUFSIZE, 0, NULL);
    if (namedPipe == INVALID_HANDLE_VALUE) {
        puts("Error");
        error = GRPC_WSA_ERROR(WSAGetLastError(), "WSASocket");
        goto done;
    }else{
      return error = add_pipe_to_server(s, namedPipe, addr, &sp);
    }

done:
    if (error != GRPC_ERROR_NONE) {
        grpc_error* error_out = GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
            "Failed to add port to server", &error, 1);
        GRPC_ERROR_UNREF(error);
        error = error_out;
  } else {
    GPR_ASSERT(sp != NULL);
  }
  return error;

 }


//4. 
void grpc_np_server_start(grpc_np_server* s, grpc_pollset** pollset,size_t pollset_count,
                             grpc_np_server_cb on_accept_cb,
                             void* on_accept_cb_arg) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  grpc_np_listener* sp = s->main;
  GPR_ASSERT(on_accept_cb);
  gpr_mu_lock(&s->mu);
  GPR_ASSERT(!s->on_accept_cb);
  s->on_accept_cb = on_accept_cb;
  s->on_accept_cb_arg = on_accept_cb_arg;
  GPR_ASSERT(GRPC_LOG_IF_ERROR("start_accept", start_accept_locked(sp)));
  s->active_ports++;
  gpr_mu_unlock(&s->mu);
}

static void destroy_server(void* arg, grpc_error* error) {
    printf("\n%d :: %s :: %s\n",__LINE__,__func__, __FILE__);
  grpc_np_server* s = (grpc_np_server*)arg;
    grpc_np_listener* sp = s->main;
  /* Now that the accepts have been aborted, we can destroy the sockets.
     The IOCP won't get notified on these, so we can flag them as already
     closed by the system. */
   gpr_free(sp);
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


static void tcp_server_destroy(grpc_np_server* s) {
  grpc_np_listener* sp  =s->main;
  gpr_mu_lock(&s->mu);

  /* First, shutdown all fd's. This will queue abortion calls for all
     of the pending accepts due to the normal operation mechanism. */
  if (s->active_ports == 0) {
    finish_shutdown_locked(s);
  } else {
      sp->shutting_down = 1;
  }
  gpr_mu_unlock(&s->mu);
}

void grpc_np_server_shutdown_listeners(grpc_np_server* s) {}

void grpc_np_server_unref(grpc_np_server* s) {
  if (gpr_unref(&s->refs)) {
    grpc_np_server_shutdown_listeners(s);
    gpr_mu_lock(&s->mu);
    grpc_core::ExecCtx::RunList(DEBUG_LOCATION, &s->shutdown_starting);
    gpr_mu_unlock(&s->mu);
    tcp_server_destroy(s);
  }
}


 grpc_np_server* grpc_np_server_ref(grpc_np_server* s) {
  gpr_ref_non_zero(&s->refs);
  return s;
}




//#endif  // GRPC_WINSOCK_SOCKET
