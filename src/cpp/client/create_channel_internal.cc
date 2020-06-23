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

#include <memory>

#include <grpcpp/channel.h>
#include <src\core\ext\transport\inproc\inproc_transport.h>

struct grpc_channel;

namespace grpc {

std::shared_ptr<Channel> CreateChannelInternal(
    const grpc::string& host, grpc_channel* c_channel,
    std::vector<std::unique_ptr<
        ::grpc::experimental::ClientInterceptorFactoryInterface>>
        interceptor_creators) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  return std::shared_ptr<Channel>(
      new Channel(host, c_channel, std::move(interceptor_creators)));
}


grpc_channel* inproc_channel(grpc_server* server, grpc_channel_args* args,
  void*) {
  return grpc_inproc_channel_create(server, args, nullptr);
}

}  // namespace grpc
