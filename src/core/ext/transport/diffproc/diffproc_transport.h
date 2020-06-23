/*
 *
 * Copyright 2017 gRPC authors.
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

#ifndef GRPC_CORE_EXT_TRANSPORT_diffproc_diffproc_TRANSPORT_H
#define GRPC_CORE_EXT_TRANSPORT_diffproc_diffproc_TRANSPORT_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/transport/transport_impl.h"

//grpc_channel* grpc_diffproc_channel_create(grpc_server* server,grpc_channel_args* args, void* reserved);

grpc_transport* diffproc_transport_create(const grpc_channel_args* args,bool is_client);

extern grpc_core::TraceFlag grpc_diffproc_trace;

void grpc_diffproc_transport_init(void);
void grpc_diffproc_transport_shutdown(void);

#endif /* GRPC_CORE_EXT_TRANSPORT_diffproc_diffproc_TRANSPORT_H */
