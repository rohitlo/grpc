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

#ifndef GRPC_CORE_LIB_IOMGR_TCP_CLIENT_H
#define GRPC_CORE_LIB_IOMGR_TCP_CLIENT_H

#include <grpc/support/port_platform.h>

#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/time.h>
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include <grpcpp\channel.h>
#include <src/core/ext/transport/diffproc/client_utils.h>


typedef void (*grpc_on_done)(void* arg, grpc_error* error);

typedef struct {
  grpc_closure* on_done;
  grpc_closure on_connect;
  HANDLE handle;
  int refs;
  gpr_mu mu;
  const char* addr_name;
  grpc_endpoint** endpoint;
  grpc_channel_args* channel_args;
  conndetails* clientsidedetails;
  grpc_on_done done;
} connection_details;
/*  */
void np_connect(grpc_closure* on_done, grpc_endpoint** ep,
                const grpc_channel_args* channel_args, const char* addr,
                conndetails* condetail, void* arg);



#endif /* GRPC_CORE_LIB_IOMGR_TCP_CLIENT_H */
