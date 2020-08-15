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

#ifndef GRPC_CORE_EXT_TRANSPORT_DIFFPROC_NAMEDPIPE_NAMEDPIPE_SERVER_H
#define GRPC_CORE_EXT_TRANSPORT_DIFFPROC_NAMEDPIPE_NAMEDPIPE_SERVER_H

#include <grpc/support/port_platform.h>

#include <grpc/grpc.h>
#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include <src/core/ext/transport/diffproc/namedpipe_thread.h>
/* Forward decl of grpc_np_server */
typedef struct grpc_np_server grpc_np_server;

typedef struct grpc_np_server_acceptor {
  /* grpc_np_server_cb functions share a ref on from_server that is valid
     until the function returns. */
  grpc_np_server* from_server;
  /* Indices that may be passed to grpc_np_server_port_fd(). */
  unsigned port_index;
  unsigned fd_index;
  /* Data when the connection is passed to np_server from external. */
  bool external_connection;
  int listener_fd;
  grpc_thread_handle* np_handle;
  grpc_byte_buffer* pending_data;
  grpc_channel_args* args;
} ggrpc_np_server_acceptor;


/* Called for newly connected NP connections.
   Takes ownership of acceptor. */
typedef void (*grpc_np_server_cb)(void* arg, grpc_endpoint* ep,
                                  grpc_pollset* accepting_pollset,
                                  grpc_np_server_acceptor* acceptor);



void grpc_np_server_start(grpc_np_server* s, grpc_pollset** pollset,
                            size_t pollset_count,
                            grpc_np_server_cb on_accept_cb,
                            void* on_accept_cb_arg);

grpc_error* grpc_namedpipe_create(grpc_closure* shutdown_complete,
                             const grpc_channel_args* args,
                             grpc_np_server** server);

grpc_error* grpc_server_np_add_port(grpc_np_server* s, const char* addr);

void grpc_np_server_shutdown_listeners(grpc_np_server* s);

void grpc_np_server_unref(grpc_np_server* s);

grpc_np_server* grpc_np_server_ref(grpc_np_server* s);

  #endif //GRPC_CORE_EXT_TRANSPORT_DIFFPROC_NAMEDPIPE_NAMEDPIPE_SERVER_H
