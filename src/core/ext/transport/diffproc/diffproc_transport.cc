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

#include <grpc/support/port_platform.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <string.h>
#include "src/core/ext/transport/diffproc/diffproc_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/surface/server.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/transport_impl.h"

#define diffproc_LOG(...)                               \
  do {                                                \
    if (GRPC_TRACE_FLAG_ENABLED(grpc_diffproc_trace)) { \
      gpr_log(__VA_ARGS__);                           \
    }                                                 \
  } while (0)



static const grpc_transport_vtable* get_vtable(void);
//COTR
grpc_diffproc_transport::grpc_diffproc_transport(
    const grpc_channel_args* channel_args, grpc_endpoint* ep, bool is_client,
    grpc_resource_user* resource_user)
    : ep(ep),state_tracker(is_client ? "client_transport" : "server_transport",GRPC_CHANNEL_READY),is_client(is_client) 
{
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  base.vtable = get_vtable();
  gpr_ref_init(&refs, 1);
  grpc_slice_buffer_init(&read_buffer);
  grpc_slice_buffer_init(&outbuf);
  if (is_client) {
    grpc_slice_buffer_add(&outbuf,grpc_slice_from_copied_string("Diff proc Transport"));
    grpc_diffproc_initiate_write(this);
  }
  
}

//DOTR
grpc_diffproc_transport::~grpc_diffproc_transport() {}

//REF
void grpc_diffproc_transport::ref() {
  //diffproc_LOG(GPR_INFO, "ref_transport %p", this);
  gpr_ref(&refs);
}


//UNREF
void grpc_diffproc_transport::unref() {
  diffproc_LOG(GPR_INFO, "unref_transport %p", this);
  if (!gpr_unref(&refs)) {
    return;
  }
  diffproc_LOG(GPR_INFO, "really_destroy_transport %p", this);
  this->~grpc_diffproc_transport();
  gpr_free(this);
}


grpc_diffproc_stream::grpc_diffproc_stream(grpc_diffproc_transport* t, grpc_stream_refcount* refcount,const void* server_data, grpc_core::Arena* arena)
  : t(t), refs(refcount), arena(arena) {
  if (server_data) {
    puts("Server Data");
  }
}

grpc_diffproc_stream::~grpc_diffproc_stream() {}
void grpc_diffproc_stream::ref(const char* reason) {
  diffproc_LOG(GPR_INFO, "ref_stream %p %s", this, reason);
}





  int init_stream(grpc_transport * gt, grpc_stream * gs,
               grpc_stream_refcount * refcount, const void* server_data,
               grpc_core::Arena* arena) {
     GPR_TIMER_SCOPE("init_stream", 0);
     grpc_diffproc_transport* t =
         reinterpret_cast<grpc_diffproc_transport*>(gt);
     new (gs) grpc_diffproc_stream(t, refcount, server_data, arena);
     return 0;
   }
   void set_pollset(grpc_transport* /*gt*/, grpc_stream* /*gs*/, grpc_pollset *
                    /*pollset*/) {
     // Nothing to do here
   }

   void set_pollset_set( grpc_transport* /*gt*/, grpc_stream* /*gs*/, grpc_pollset_set* /*pollset_set*/) {
     // Nothing to do here
   }

   grpc_endpoint* get_endpoint(grpc_transport * t) {
     return (reinterpret_cast<grpc_diffproc_transport*>(t))->ep;
   }
   

   void destroy_stream(grpc_transport* /*gt*/, grpc_stream * gs,
                       grpc_closure * then_schedule_closure) {
     grpc_diffproc_stream* s = reinterpret_cast<grpc_diffproc_stream*>(gs);

     puts("In destroy stream");
   }

   void destroy_transport(grpc_transport * gt) {
     grpc_diffproc_transport* t = reinterpret_cast<grpc_diffproc_transport*>(gt);

     puts("In destroy transport");
     t->unref();
   }


   void write_action_end(void* tp, grpc_error* error) {
     grpc_diffproc_transport* t = static_cast<grpc_diffproc_transport*>(tp);
     puts("Nice ok");
   }

   void read_action_end(void* tp, grpc_error* error) {
     grpc_diffproc_transport* t = static_cast<grpc_diffproc_transport*>(tp);
     fpending = 0;

     puts("Nice ok");
   }

  static void perform_stream_op(grpc_transport* gt, grpc_stream* gs,
                                 grpc_transport_stream_op_batch* op) {
     GPR_TIMER_SCOPE("perform_stream_op", 0);
     grpc_diffproc_transport* t =
         reinterpret_cast<grpc_diffproc_transport*>(gt);

     grpc_closure* on_complete = op->on_complete;
     grpc_diffproc_stream* s = reinterpret_cast<grpc_diffproc_stream*>(gs);
     grpc_diffproc_initiate_write(t);
     printf("In perform_stream_op");
     grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_complete, GRPC_ERROR_NONE);
   }

    void perform_transport_op(grpc_transport* gt, grpc_transport_op* op) {
     printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
     grpc_diffproc_transport* t = reinterpret_cast<grpc_diffproc_transport*>(gt);
   }


    
   const grpc_transport_vtable diffproc_vtable = {sizeof(grpc_diffproc_stream),
                                                "diffproc",
                                                init_stream,
                                                set_pollset,
                                                set_pollset_set,
                                                perform_stream_op,
                                                perform_transport_op,
                                                destroy_stream,
                                                destroy_transport,
                                                get_endpoint
                                                };

   static const grpc_transport_vtable* get_vtable(void) {
     return &diffproc_vtable;
   }
   grpc_transport* grpc_create_diffproc_transport(
       const grpc_channel_args* channel_args, grpc_endpoint* ep, bool is_client,
       grpc_resource_user* resource_user) {
     printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
     printf("\n Endpoint ptr : %p and handle : %p \n ", ep);
     auto t = new grpc_diffproc_transport(channel_args, ep, is_client,
                                          resource_user);
     return &t->base;
   }


   void grpc_diffproc_initiate_write(grpc_diffproc_transport* t) {
     printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
     //printf("\n Endpoint ptr : %p and handle : %p \n ", t->ep);
       grpc_endpoint_write(t->ep, &t->outbuf,GRPC_CLOSURE_INIT(&t->write_action_end_locked, write_action_end,t, grpc_schedule_on_exec_ctx),nullptr);
     //printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
   }

   void grpc_diffproc_transport_start_reading(
       grpc_transport* transport, grpc_slice_buffer* read_buffer) {
     printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
     grpc_diffproc_transport* t =
         reinterpret_cast<grpc_diffproc_transport*>(transport);
         grpc_endpoint_read(
             t->ep, read_buffer,
             GRPC_CLOSURE_INIT(&t->read_action_locked, read_action_end, t,
                               grpc_schedule_on_exec_ctx),
             GRPC_ERROR_NONE);

     

   }


 void grpc_diffproc_transport_init(void) {
     grpc_core::ExecCtx exec_ctx;
     g_empty_slice = grpc_core::ExternallyManagedSlice();

     grpc_slice key_tmp = grpc_slice_from_static_string(":path");
     g_fake_path_key = grpc_slice_intern(key_tmp);
     grpc_slice_unref_internal(key_tmp);

     g_fake_path_value = grpc_slice_from_static_string("/");

     grpc_slice auth_tmp = grpc_slice_from_static_string(":authority");
     g_fake_auth_key = grpc_slice_intern(auth_tmp);
     grpc_slice_unref_internal(auth_tmp);

     g_fake_auth_value = grpc_slice_from_static_string("inproc-fail");
 }

 void grpc_diffproc_transport_shutdown(void) {
   grpc_core::ExecCtx exec_ctx;
   grpc_slice_unref_internal(g_empty_slice);
   grpc_slice_unref_internal(g_fake_path_key);
   grpc_slice_unref_internal(g_fake_path_value);
   grpc_slice_unref_internal(g_fake_auth_key);
   grpc_slice_unref_internal(g_fake_auth_value);
 
 }







               
