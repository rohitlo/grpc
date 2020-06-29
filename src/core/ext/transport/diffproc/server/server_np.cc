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

#include <grpc/grpc.h>

#include <grpc/support/log.h>

#include "src/core/ext/transport/diffproc/server/server_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/server.h"


//GRPC API
int grpc_server_add_np_addr(grpc_server* server, const char* addr) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  grpc_core::ExecCtx exec_ctx;
  int port_num = 0;
  grpc_error* err = grpc_np_server_add_pipe(server, addr,grpc_channel_args_copy(grpc_server_get_channel_args(server)), &port_num);
  if (err != GRPC_ERROR_NONE) {
    const char* msg = grpc_error_string(err);
    gpr_log(GPR_ERROR, "%s", msg);

    GRPC_ERROR_UNREF(err);
  }
  return port_num;
}
