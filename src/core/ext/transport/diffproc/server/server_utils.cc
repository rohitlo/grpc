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

//#include "src/core/ext/transport/chttp2/server/chttp2_server.h"

#include <inttypes.h>
#include <limits.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

#include "absl/strings/str_format.h"

#include "src/core/ext/transport/diffproc/diffproc_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/server.h"
#include <src\core\ext\transport\diffproc\namedpipe\namedpipe_server.h>


//Server-State
typedef struct {
  grpc_server* server;
  grpc_np_server* np_server;
  const grpc_channel_args* args;
  gpr_mu mu;
  bool shutdown;
  grpc_closure grpc_np_server_shutdown_complete;
  grpc_closure* server_destroy_listener_done;

  grpc_core::RefCountedPtr<grpc_core::channelz::ListenSocketNode>
      channelz_listen_socket;
  BOOL pendingOp = 0;
} server_state;


//Server-Connection State
typedef struct {
  gpr_refcount refs;
  server_state* svr_state;
  grpc_pollset* accepting_pollset;
  grpc_np_server_acceptor* acceptor;
  // State for enforcing handshake timeout on receiving HTTP/2 settings.
  grpc_diffproc_transport* transport;
  grpc_millis deadline;
  grpc_pollset_set* interested_parties;
} server_connection_state;

//Accept callback sent to pipe server
static void on_accept(void* arg, grpc_endpoint* np,
                      grpc_pollset* accepting_pollset,
                      grpc_np_server_acceptor* acceptor) {
  printf("\n%d :: %s :: %s\n",__LINE__,__func__, __FILE__);
  server_state* state = static_cast<server_state*>(arg);
  grpc_slice_buffer* read_buffer = nullptr;
  gpr_mu_lock(&state->mu);    
   if (state->shutdown) {
    gpr_mu_unlock(&state->mu);
    grpc_endpoint_shutdown(np, GRPC_ERROR_NONE);
    grpc_endpoint_destroy(np);
    gpr_free(acceptor);
    return;
  }  
  grpc_np_server_ref(state->np_server);
  gpr_mu_unlock(&state->mu);
  //grpc_core::ExecCtx::Get()->Now();      
  if (np != nullptr) {
      puts("Endpoint is not null...  server_utils.cc");
    const char* args_to_remove[] = {GRPC_ARG_MAX_CONNECTION_IDLE_MS,
                                    GRPC_ARG_MAX_CONNECTION_AGE_MS};
    const grpc_channel_args* server_args = grpc_channel_args_copy_and_remove(grpc_server_get_channel_args(state->server),
                                      args_to_remove,
                                      GPR_ARRAY_SIZE(args_to_remove));
    grpc_transport* transport =
        grpc_create_diffproc_transport(server_args, np, false, nullptr);
    grpc_server_setup_transport(state->server, transport, nullptr, server_args,
                                nullptr, nullptr);
    //grpc_get_transport_state
      // Use notify_on_receive_settings callback to enforce the
      // handshake deadline.
    //if (state->pendingOp == 1) {
      grpc_diffproc_transport_start_reading(transport, read_buffer);
    //}
    grpc_channel_args_destroy(server_args);
    }   
}


/* Server callback: start listening on our ports */
static void server_start_listener(grpc_server* /*server*/, void* arg,
                                  grpc_pollset** pollsets,
                                  size_t pollset_count) {
  printf("\n%d :: %s :: %s\n",__LINE__,__func__, __FILE__);
  server_state* state = static_cast<server_state*>(arg);
  gpr_mu_lock(&state->mu);
  state->shutdown = false;
  gpr_mu_unlock(&state->mu);
  grpc_np_server_start(state->np_server, pollsets, pollset_count, on_accept, state);
}


static void grpc_np_server_shutdown_complete(void* arg, grpc_error* error) {
  printf("Shutdown");
}

/* Server callback: destroy the tcp listener (so we don't generate further
   callbacks) */
static void server_destroy_listener(grpc_server* /*server*/, void* arg,
                                    grpc_closure* destroy_done) {
  server_state* state = static_cast<server_state*>(arg);
  puts("Destroy listener");
  gpr_mu_lock(&state->mu);
  state->shutdown = true;
  state->server_destroy_listener_done = destroy_done;
  grpc_np_server* np_server = state->np_server;
  gpr_mu_unlock(&state->mu);
  grpc_np_server_shutdown_listeners(np_server);
  grpc_np_server_unref(np_server);
}


//Adds a pipe to server at np_server_windows
grpc_error* grpc_np_server_add_pipe(grpc_server* server, const char* addr,
                                    grpc_channel_args* args, int* port) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  grpc_np_server* np_server = nullptr;
  size_t i;
  size_t count = 0;
  int port_temp = 1;
  grpc_error* err = GRPC_ERROR_NONE;
  server_state* state = nullptr;
  grpc_error** errors = nullptr;
  const grpc_arg* arg = nullptr;

  *port = -1;

  state = static_cast<server_state*>(gpr_zalloc(sizeof(*state)));
  GRPC_CLOSURE_INIT(&state->grpc_np_server_shutdown_complete,
                    grpc_np_server_shutdown_complete, state,
                    grpc_schedule_on_exec_ctx);
  err = grpc_namedpipe_create(&state->grpc_np_server_shutdown_complete, args, &np_server);
  if (err != GRPC_ERROR_NONE) {
    goto error;
  }
  puts("\n No error creating server... \n");
  state->server = server;
  state->np_server = np_server;
  state->args = args;
  state->shutdown = true;
  gpr_mu_init(&state->mu);
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  errors = static_cast<grpc_error**>(gpr_malloc(sizeof(*errors) * 4));
  for (i = 0; i < 4; i++) {
    errors[i] = grpc_server_np_add_port(np_server, addr);
    if (errors[i] == GRPC_ERROR_NONE) {
      count++;
    }
  }
  /* Register with the server only upon success */
  grpc_server_add_listener(server, state, server_start_listener,server_destroy_listener,nullptr);
  goto done;

/* Error path: cleanup and return */
error:
  GPR_ASSERT(err != GRPC_ERROR_NONE);
  if (np_server) {
    puts("Unref here");
  } else {
    grpc_channel_args_destroy(args);
    gpr_free(state);
  }

done:
  if (errors != nullptr) {
    for (i = 0; i < 4; i++) {
      GRPC_ERROR_UNREF(errors[i]);
    }
    gpr_free(errors);
  }
  return err;
}
