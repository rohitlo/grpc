/*
 *
 * Copyright 2016 gRPC authors.
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

#ifndef GRPC_CORE_EXT_TRANSPORT_DIFFPROC_DIFFPROC_CHANNEL_CREATE_H
#define GRPC_CORE_EXT_TRANSPORT_DIFFPROC_DIFFPROC_CHANNEL_CREATE_H

#include <grpc/support/port_platform.h>

#include <grpc/grpc.h>
#include "src/core/lib/iomgr/endpoint.h"
#include <src\core\lib\transport\transport.h>
#ifdef __cplusplus
extern "C" {
#endif

struct conndetails {
  grpc_endpoint* endpoint;
  HANDLE hd;
  grpc_transport* transport;
  grpc_channel_args* args;
};

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_EXT_TRANSPORT_DIFFPROC_DIFFPROC_CHANNEL_CREATE_H \
        */
