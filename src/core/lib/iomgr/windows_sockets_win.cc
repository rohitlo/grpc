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
#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"
#ifdef GRPC_HAVE_NAMED_PIPE

#include "src/core/lib/iomgr/sockaddr.h"

#include <string.h>

#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include <winsock.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/useful.h"



grpc_error* grpc_resolve_named_pipe_address(const char* name,
                                             grpc_resolved_addresses** addrs) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  //struct sockaddr_un* un;
  struct sockaddr* wn;
  if (strlen(name) > 20) {
    char* err_msg;
    grpc_error* err;
    gpr_asprintf(&err_msg,"Path name should not have more than %" PRIuPTR " characters.",14);
    err = GRPC_ERROR_CREATE_FROM_COPIED_STRING(err_msg);
    gpr_free(err_msg);
    return err;
  }
  *addrs = static_cast<grpc_resolved_addresses*>(
      gpr_malloc(sizeof(grpc_resolved_addresses)));
  (*addrs)->naddrs = 1;
  (*addrs)->addrs = static_cast<grpc_resolved_address*>(gpr_malloc(sizeof(grpc_resolved_address)));
  wn = reinterpret_cast<struct sockaddr*>((*addrs)->addrs->addr);
  printf("\n %p", wn);
  wn->sa_family = AF_UNSPEC;
  strncpy(wn->sa_data, name, sizeof(wn->sa_data));
  printf("\n %s\n", wn->sa_data);
  (*addrs)->addrs->len =
      static_cast<socklen_t>(strlen(wn->sa_data) + sizeof(wn->sa_family) + 1);
  return GRPC_ERROR_NONE;
}

//int grpc_is_unix_socket(const grpc_resolved_address* resolved_addr) {
//  const grpc_sockaddr* addr =
//      reinterpret_cast<const grpc_sockaddr*>(resolved_addr->addr);
//  return addr->sa_family == AF_UNIX;
//}


#endif
