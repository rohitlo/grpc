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

#define DIFFPROC_LOG(...)                               \
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
    //grpc_diffproc_initiate_write(this);
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
  DIFFPROC_LOG(GPR_INFO, "unref_transport %p", this);
  if (!gpr_unref(&refs)) {
    return;
  }
  DIFFPROC_LOG(GPR_INFO, "really_destroy_transport %p", this);
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
  DIFFPROC_LOG(GPR_INFO, "ref_stream %p %s", this, reason);
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
   static void close_transport_locked(grpc_diffproc_transport* t,
                                      grpc_error* error) {
     grpc_endpoint_shutdown(t->ep, GRPC_ERROR_REF(error));
   }

   void destroy_stream(grpc_transport* /*gt*/, grpc_stream * gs,
                       grpc_closure * then_schedule_closure) {
     grpc_diffproc_stream* s = reinterpret_cast<grpc_diffproc_stream*>(gs);

     puts("In destroy stream");
   }

   void destroy_transport(grpc_transport* gt) {
     grpc_diffproc_transport* t = reinterpret_cast<grpc_diffproc_transport*>(gt);
     puts("In destroy transport");
     close_transport_locked(
         t, GRPC_ERROR_CREATE_FROM_STATIC_STRING("Destory transport called"));
     t->unref();
   }


   void write_action_end(void* tp, grpc_error* error) {
     grpc_diffproc_transport* t = static_cast<grpc_diffproc_transport*>(tp);
     grpc_error* err;
     err = GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING("Write action closed",
                                                            &error, 1);
     //close_transport_locked(t, GRPC_ERROR_REF(error));
     printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
     GRPC_ERROR_UNREF(error);
   }

   void read_action_end(void* tp, grpc_error* error) {
     grpc_diffproc_transport* t = static_cast<grpc_diffproc_transport*>(tp);
     grpc_error* err;

     err = GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
         "Read action closed", &error, 1);
     close_transport_locked(t, GRPC_ERROR_REF(error));
     printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
     GRPC_ERROR_UNREF(error);
   }

  static void log_metadata(const grpc_metadata_batch* md_batch,
                            bool is_client, bool is_initial) {
     for (grpc_linked_mdelem* md = md_batch->list.head; md != nullptr;
          md = md->next) {
       char* key = grpc_slice_to_c_string(GRPC_MDKEY(md->md));
       char* value = grpc_slice_to_c_string(GRPC_MDVALUE(md->md));
       gpr_log(GPR_INFO, "DIFFPROC :%s:%s: %s: %s",
               is_initial ? "HDR" : "TRL", is_client ? "CLI" : "SVR", key,
               value);
       gpr_free(key);
       gpr_free(value);
     }
   }

static void do_nothing(void* arg, grpc_error*) {}


static bool contains_non_ok_status(grpc_metadata_batch* batch) {
  if (batch->idx.named.grpc_status != nullptr) {
    return !grpc_mdelem_static_value_eq(batch->idx.named.grpc_status->md,
                                        GRPC_MDELEM_GRPC_STATUS_0);
  }
  return false;
}

static void cancel_stream_locked(grpc_diffproc_stream* stream, grpc_error*) {}

static void perform_stream_op_locked(void* stream_op,
                                        grpc_error* /*error_ignored*/) {
     GPR_TIMER_SCOPE("perform_stream_op", 0);
  grpc_transport_stream_op_batch* op = static_cast<grpc_transport_stream_op_batch*>(stream_op);
  grpc_diffproc_stream* s = static_cast<grpc_diffproc_stream*>(op->handler_private.extra_arg);
  grpc_diffproc_transport* t = s->t;
     if (GRPC_TRACE_FLAG_ENABLED(grpc_diffproc_trace)) {
       if (op->send_initial_metadata) {
         log_metadata(op->payload->send_initial_metadata.send_initial_metadata,
                     t->is_client, true);
       }
       if (op->send_trailing_metadata) {
         log_metadata(
             op->payload->send_trailing_metadata.send_trailing_metadata,
             t->is_client, false);
       }
     }
     grpc_closure* on_complete = op->on_complete;
     // on_complete will be null if and only if there are no send ops in the
     // batch.
     if (on_complete == nullptr) {
       on_complete = GRPC_CLOSURE_INIT(&op->handler_private.closure, do_nothing,
                                       nullptr, grpc_schedule_on_exec_ctx);
     }

     if (op->cancel_stream) {
       cancel_stream_locked(s, op->payload->cancel_stream.cancel_error);
     }

     if (op->send_initial_metadata) {
       GPR_ASSERT(s->send_initial_metadata_finished == nullptr);

     }
     //grpc_slice_buffer_init(&s->compressed_data_buffer);
    // s->send_initial_metadata_finished = add_closure_barrier(on_complete);
     //s->send_initial_metadata = op->payload->send_initial_metadata.send_initial_metadata;
     const size_t metadata_size = grpc_metadata_batch_size(s->send_initial_metadata);
     if (t->is_client) {
       s->deadline = GPR_MIN(s->deadline, s->send_initial_metadata->deadline);
     }
     if (contains_non_ok_status(s->send_initial_metadata)) {
       s->seen_error = true;
     }






     //grpc_closure* on_complete = op->on_complete;
    // grpc_diffproc_stream* s = reinterpret_cast<grpc_diffproc_stream*>(gs);
     grpc_diffproc_initiate_write(t);
     printf("In perform_stream_op");
     grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_complete, GRPC_ERROR_NONE);
   }



  static void perform_stream_op(grpc_transport* gt, grpc_stream* gs,
                                 grpc_transport_stream_op_batch* op) {
     GPR_TIMER_SCOPE("perform_stream_op", 0);
    grpc_diffproc_transport* t = reinterpret_cast<grpc_diffproc_transport*>(gt);
     grpc_diffproc_stream* s = reinterpret_cast<grpc_diffproc_stream*>(gs);

     if (!t->is_client) {
       if (op->send_initial_metadata) {
         grpc_millis deadline =
             op->payload->send_initial_metadata.send_initial_metadata->deadline;
         GPR_ASSERT(deadline == GRPC_MILLIS_INF_FUTURE);
       }
       if (op->send_trailing_metadata) {
         grpc_millis deadline = op->payload->send_trailing_metadata
                                    .send_trailing_metadata->deadline;
         GPR_ASSERT(deadline == GRPC_MILLIS_INF_FUTURE);
       }
     }

     if (GRPC_TRACE_FLAG_ENABLED(grpc_diffproc_trace)) {
       char* str = grpc_transport_stream_op_batch_string(op);
       gpr_log(GPR_INFO, "perform_stream_op[s=%p]: %s", s, str);
       gpr_free(str);
     }
     op->handler_private.extra_arg = gs;
     grpc_core::ExecCtx::Run(DEBUG_LOCATION,GRPC_CLOSURE_INIT(&op->handler_private.closure, perform_stream_op_locked, op, nullptr), GRPC_ERROR_NONE);
   }

    void perform_transport_op(grpc_transport* gt, grpc_transport_op* op) {
     printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
     grpc_diffproc_transport* t = reinterpret_cast<grpc_diffproc_transport*>(gt);
     DIFFPROC_LOG(GPR_INFO, "perform_transport_op %p %p", t, op);
     gpr_mu_lock(&t->mu);
     if (op->start_connectivity_watch != nullptr) {
       t->state_tracker.AddWatcher(op->start_connectivity_watch_state,
                                   std::move(op->start_connectivity_watch));
     }
     if (op->stop_connectivity_watch != nullptr) {
       t->state_tracker.RemoveWatcher(op->stop_connectivity_watch);
     }
     if (op->set_accept_stream) {
       printf("setting accept stream \n");
       t->accept_stream_cb = op->set_accept_stream_fn;
       t->accept_stream_data = op->set_accept_stream_user_data;
     }
     if (op->on_consumed) {
       grpc_core::ExecCtx::Run(DEBUG_LOCATION, op->on_consumed,
                               GRPC_ERROR_NONE);
     }

   
     if (op->goaway_error != GRPC_ERROR_NONE) {
       close_transport_locked(t, op->goaway_error);
       GRPC_ERROR_UNREF(op->goaway_error);
     }
     if (op->disconnect_with_error != GRPC_ERROR_NONE) {
       close_transport_locked(t, op->disconnect_with_error);
       GRPC_ERROR_UNREF(op->disconnect_with_error);
     }

     gpr_mu_unlock(&t->mu);
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
   grpc_transport* grpc_create_diffproc_transport(const grpc_channel_args* channel_args, grpc_endpoint* ep, bool is_client, grpc_resource_user* resource_user) {
     auto t = new grpc_diffproc_transport(channel_args, ep, is_client, resource_user);
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
   /*  g_empty_slice = grpc_core::ExternallyManagedSlice();


     grpc_slice key_tmp = grpc_slice_from_static_string(":path");
     g_fake_path_key = grpc_slice_intern(key_tmp);
     grpc_slice_unref_internal(key_tmp);

     g_fake_path_value = grpc_slice_from_static_string("/");

     grpc_slice auth_tmp = grpc_slice_from_static_string(":authority");
     g_fake_auth_key = grpc_slice_intern(auth_tmp);
     grpc_slice_unref_internal(auth_tmp);

     g_fake_auth_value = grpc_slice_from_static_string("inproc-fail");*/
 }

 void grpc_diffproc_transport_shutdown(void) {
   grpc_core::ExecCtx exec_ctx;

 
 }







               
