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

#ifndef GRPC_CORE_LIB_IOMGR_WINDOWS_SOCKETS_WIN_H
#define GRPC_CORE_LIB_IOMGR_WINDOWS_SOCKETS_WIN_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"

#include <grpc/support/string_util.h>

#include "src/core/lib/iomgr/resolve_address.h"

void grpc_create_socketpair_if_unix(int sv[2]);

grpc_error* grpc_resolve_named_pipe_address(
    const char* name, grpc_resolved_addresses** addresses);

int grpc_is_named_pipe(const grpc_resolved_address* resolved_addr);

void grpc_unlink_if_named_pipe(
    const grpc_resolved_address* resolved_addr);

char* grpc_sockaddr_to_uri_unix_if_possible(
    const grpc_resolved_address* resolved_addr);

#endif /* GRPC_CORE_LIB_IOMGR_WINDOWS_SOCKETS_WIN_H */
