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

#ifndef GRPC_CORE_EXT_TRANSPORT_DIFFPROC_NAMEDPIPE_TCP_WINDOWS_H
#define GRPC_CORE_EXT_TRANSPORT_DIFFPROC_NAMEDPIPE_TCP_WINDOWS_H
/*
    Named pipe Implementation
*/

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_WINSOCK_SOCKET
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/socket_windows.h"

/* Create a HANDLE given a handle.
 * Takes ownership of the handle.
 */
grpc_endpoint* grpc_namedpipe_create(HANDLE handle, grpc_channel_args* channel_args,
                               const char* peer_string);


#endif

#endif /* GRPC_CORE_EXT_TRANSPORT_DIFFPROC_NAMEDPIPE_TCP_WINDOWS_H */
