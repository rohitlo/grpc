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

#include <grpc/impl/codegen/port_platform.h>

#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/diffproc/diffproc_channel_create.h"
#include "src/core/ext/transport/diffproc/transport/diffproc_transport.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/transport_impl.h"

// Cronet transport object
//typedef struct diffproc_transport {
  //grpc_transport base;  // must be first element in this structure
  //char* host;
//} diffproc_transport;


GRPCAPI grpc_channel* grpc_diffproc_channel_create(const char* target, const grpc_channel_args* args,
    void* reserved) {
    if (target == nullptr) {
        gpr_log(GPR_ERROR, "cannot create channel with NULL target name");
        return nullptr;
    }
    grpc_arg arg = grpc_channel_arg_string_create(const_cast<char*>(GRPC_ARG_SERVER_URI),
                                       const_cast<char*>(target));
    grpc_arg default_authority_arg;
    default_authority_arg.type = GRPC_ARG_STRING;
    default_authority_arg.key = (char*)GRPC_ARG_DEFAULT_AUTHORITY;
    default_authority_arg.value.string = (char*)"diffproc.authority";
    grpc_channel_args* client_args =
        grpc_channel_args_copy_and_add(args, &arg, 1);
    client_args= grpc_channel_args_copy_and_add(args, &default_authority_arg, 1);
    grpc_transport* transport =
        grpc_create_diffproc_transport(client_args, nullptr,1,nullptr);
    GPR_ASSERT(transport);
    grpc_channel* channel = grpc_channel_create(
        target, client_args, GRPC_CLIENT_CHANNEL, transport);
    grpc_channel_args_destroy(client_args);
    printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
    return channel;

}