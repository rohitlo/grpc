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
static void log_metadata(const grpc_metadata_batch* md_batch, bool is_client,
                         bool is_initial);
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


grpc_error* fill_in_metadata(grpc_diffproc_stream* s,
                             const grpc_metadata_batch* metadata,
                             uint32_t flags, grpc_metadata_batch* out_md,
                             uint32_t* outflags, bool* markfilled) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_diffproc_trace)) {
    log_metadata(metadata, s->t->is_client, outflags != nullptr);
  }

  if (outflags != nullptr) {
    *outflags = flags;
  }
  if (markfilled != nullptr) {
    *markfilled = true;
  }
  grpc_error* error = GRPC_ERROR_NONE;
  for (grpc_linked_mdelem* elem = metadata->list.head;
       (elem != nullptr) && (error == GRPC_ERROR_NONE); elem = elem->next) {
    grpc_linked_mdelem* nelem =
        static_cast<grpc_linked_mdelem*>(s->arena->Alloc(sizeof(*nelem)));
    nelem->md =
        grpc_mdelem_from_slices(grpc_slice_intern(GRPC_MDKEY(elem->md)),
                                grpc_slice_intern(GRPC_MDVALUE(elem->md)));

    error = grpc_metadata_batch_link_tail(out_md, nelem);
  }
  return error;
}

static void fail_helper_locked(grpc_diffproc_stream* s, grpc_error*){}

void op_state_machine_locked(grpc_diffproc_stream* s, grpc_error* error) {
  // This function gets called when we have contents in the unprocessed reads
  // Get what we want based on our ops wanted
  // Schedule our appropriate closures
  // and then return to ops_needed state if still needed
  if (s->cancel_self_error != GRPC_ERROR_NONE) {
    fail_helper_locked(s, GRPC_ERROR_REF(s->cancel_self_error));
    goto done;
  } else if (error != GRPC_ERROR_NONE) {
    fail_helper_locked(s, GRPC_ERROR_REF(error));
    goto done;
  }




done:
  puts("Error Here ******");
}

void maybe_process_ops_locked(grpc_diffproc_stream* s, grpc_error* error) {
  if (s && (error != GRPC_ERROR_NONE || s->ops_needed)) {
    s->ops_needed = false;
    op_state_machine_locked(s, error);
  }
}

grpc_diffproc_stream::grpc_diffproc_stream(grpc_diffproc_transport* t, grpc_stream_refcount* refcount,const void* server_data, grpc_core::Arena* arena)
  : t(t), refs(refcount), arena(arena) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  ref("inproc_init_stream:init");
  ref("inproc_init_stream:list");

  grpc_metadata_batch_init(&to_read_initial_md);
  grpc_metadata_batch_init(&to_read_trailing_md);
  grpc_metadata_batch_init(&write_buffer_initial_md);
  grpc_metadata_batch_init(&write_buffer_trailing_md);

  stream_list_prev = nullptr;
  gpr_mu_unlock(&t->mu);
  gpr_mu_lock(&t->mu);
  stream_list_next = t->stream_list;
  if (t->stream_list) {
    t->stream_list->stream_list_prev = this;
  }
  t->stream_list = this;
  gpr_mu_unlock(&t->mu);

  if (!server_data) {
    t->ref();
    puts("Client side...");

  } else {
    t->ref();
    printf(" Transport : %p in server \n", t);
    puts("Server side...");
    grpc_diffproc_stream* cs = (grpc_diffproc_stream*)server_data;
    *t->accepting_stream = this;

    gpr_mu_lock(&t->mu);
    // Now transfer from the other side's write_buffer if any to the to_read
    // buffer
   /* if (cs->write_buffer_initial_md_filled) {
      fill_in_metadata(this, &cs->write_buffer_initial_md,
                       cs->write_buffer_initial_md_flags, &to_read_initial_md,
                       &to_read_initial_md_flags, &to_read_initial_md_filled);
      deadline = GPR_MIN(deadline, cs->write_buffer_deadline);
      grpc_metadata_batch_clear(&cs->write_buffer_initial_md);
      cs->write_buffer_initial_md_filled = false;
    }
    if (cs->write_buffer_trailing_md_filled) {
      fill_in_metadata(this, &cs->write_buffer_trailing_md, 0,
                       &to_read_trailing_md, nullptr,
                       &to_read_trailing_md_filled);
      grpc_metadata_batch_clear(&cs->write_buffer_trailing_md);
      cs->write_buffer_trailing_md_filled = false;
    }
    if (cs->write_buffer_cancel_error != GRPC_ERROR_NONE) {
      cancel_other_error = cs->write_buffer_cancel_error;
      cs->write_buffer_cancel_error = GRPC_ERROR_NONE;
      maybe_process_ops_locked(this, cancel_other_error);
    }*/

    gpr_mu_unlock(&t->mu);
  }
}

grpc_diffproc_stream::~grpc_diffproc_stream() {}
void grpc_diffproc_stream::ref(const char* reason) {
  DIFFPROC_LOG(GPR_INFO, "ref_stream %p %s", this, reason);
}





  int init_stream(grpc_transport * gt, grpc_stream * gs,
               grpc_stream_refcount * refcount, const void* server_data,
               grpc_core::Arena* arena) {
    printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
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

static void grpc_diffproc_list_add_streams(grpc_diffproc_transport*,
                                           grpc_diffproc_stream*) {}

static void startStreams(grpc_diffproc_transport*) {}

static void grpc_diffproc_cancel_stream(grpc_diffproc_transport*,
                                        grpc_diffproc_stream*, grpc_error*) {}

static void cancel_stream_locked(grpc_diffproc_stream* stream, grpc_error*) {}

// Call the on_complete closure associated with this stream_op_batch if
// this stream_op_batch is only one of the pending operations for this
// stream. This is called when one of the pending operations for the stream
// is done and about to be NULLed out
void complete_if_batch_end_locked(grpc_diffproc_stream* s, grpc_error* error,
                                  grpc_transport_stream_op_batch* op,
                                  const char* msg) {
  int is_sm = static_cast<int>(op == s->send_message_op);
  int is_stm = static_cast<int>(op == s->send_trailing_md_op);
  // TODO(vjpai): We should not consider the recv ops here, since they
  // have their own callbacks.  We should invoke a batch's on_complete
  // as soon as all of the batch's send ops are complete, even if there
  // are still recv ops pending.
  int is_rim = static_cast<int>(op == s->recv_initial_md_op);
  int is_rm = static_cast<int>(op == s->recv_message_op);
  int is_rtm = static_cast<int>(op == s->recv_trailing_md_op);

  if ((is_sm + is_stm + is_rim + is_rm + is_rtm) == 1) {
   printf( "%s %p %p %p", msg, s, op, error);
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, op->on_complete,
                            GRPC_ERROR_REF(error));
  }
}
void message_transfer_locked(grpc_diffproc_transport* t, grpc_diffproc_stream* sender) {
  size_t remaining =
      sender->send_message_op->payload->send_message.send_message->length();
  if (sender->recv_inited) {
    grpc_slice_buffer_destroy_internal(&t->outbuf);
  }
  grpc_slice_buffer_init(&t->outbuf);
  sender->recv_inited = true;
  do {
    grpc_slice message_slice;
    grpc_closure unused;
    GPR_ASSERT(
        sender->send_message_op->payload->send_message.send_message->Next(
            SIZE_MAX, &unused));
    grpc_error* error =
        sender->send_message_op->payload->send_message.send_message->Pull(
            &message_slice);
    if (error != GRPC_ERROR_NONE) {
      cancel_stream_locked(sender, GRPC_ERROR_REF(error));
      break;
    }
    GPR_ASSERT(error == GRPC_ERROR_NONE);
    remaining -= GRPC_SLICE_LENGTH(message_slice);
    grpc_slice_buffer_add(&t->outbuf, message_slice);
  } while (remaining > 0);
  sender->send_message_op->payload->send_message.send_message.reset();

  printf( "message_transfer_locked %p scheduling message-ready",
             sender);
  complete_if_batch_end_locked(sender, GRPC_ERROR_NONE, sender->send_message_op,"message_transfer scheduling sender on_complete");
  sender->send_message_op = nullptr;
}

void message_read_locked(grpc_diffproc_transport* t,
                             grpc_diffproc_stream* sender) {
  size_t remaining =
      sender->recv_message_op->payload->recv_message.recv_message->get()
           ->length();
  grpc_slice_buffer_init(&t->read_buffer);
  sender->read_intited = true;
  do {
    grpc_slice message_slice;
    grpc_closure unused;
    GPR_ASSERT(
        sender->recv_message_op->payload->recv_message.recv_message->get()->Next(
            SIZE_MAX, &unused));
    grpc_error* error =
        sender->recv_message_op->payload->recv_message.recv_message->get()
            ->Pull(
            &message_slice);
    if (error != GRPC_ERROR_NONE) {
      cancel_stream_locked(sender, GRPC_ERROR_REF(error));
      break;
    }
    GPR_ASSERT(error == GRPC_ERROR_NONE);
    remaining -= GRPC_SLICE_LENGTH(message_slice);
    grpc_slice_buffer_add(&t->read_buffer, message_slice);
  } while (remaining > 0);

  sender->recv_stream.Init(&sender->recv_message, 0);
  sender->recv_message_op->payload->recv_message.recv_message->reset(
      sender->recv_stream.get());
  printf("message_transfer_locked %p scheduling message-ready", sender);
  grpc_core::ExecCtx::Run(
      DEBUG_LOCATION,
      sender->recv_message_op->payload->recv_message.recv_message_ready,
      GRPC_ERROR_NONE);
  complete_if_batch_end_locked(
      sender, GRPC_ERROR_NONE, sender->recv_message_op,
      "message_transfer scheduling receiver on_complete");

  sender->recv_message_op = nullptr;
}




static void perform_stream_op_locked(void* stream_op,
                                     grpc_error* /*error_ignored*/) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  GPR_TIMER_SCOPE("perform_stream_op", 0);
  grpc_transport_stream_op_batch* op =
      static_cast<grpc_transport_stream_op_batch*>(stream_op);
  grpc_diffproc_stream* s =
      static_cast<grpc_diffproc_stream*>(op->handler_private.extra_arg);
  printf(
      "Stream in perform_stream_op_locked :%p && Stream op batch : %p & "
      "Trasnport : %p \n",
      s, op, s->t);
  grpc_diffproc_transport* t = s->t;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_diffproc_trace)) {
    if (op->send_initial_metadata) {
      log_metadata(op->payload->send_initial_metadata.send_initial_metadata,
                   t->is_client, true);
    }
    if (op->send_trailing_metadata) {
      log_metadata(op->payload->send_trailing_metadata.send_trailing_metadata,
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

  //Send INITITAL MD
  if (op->send_initial_metadata) {
    grpc_closure* send_initial_metadata_finished = op->on_complete;
    GPR_ASSERT(s->send_initial_metadata_finished == nullptr);
    // grpc_slice_buffer_init(&s->compressed_data_buffer);
    // s->send_initial_metadata_finished = add_closure_barrier(on_complete);
    s->send_initial_metadata =
        op->payload->send_initial_metadata.send_initial_metadata;
    const size_t metadata_size =
        grpc_metadata_batch_size(s->send_initial_metadata);
    if (t->is_client) {
      s->deadline = GPR_MIN(s->deadline, s->send_initial_metadata->deadline);
    }
    // if (contains_non_ok_status(s->send_initial_metadata)) {
    //  s->seen_error = true;
    //}
    if (!s->write_closed) {
      if (t->is_client) {
        if (t->closed_with_error == GRPC_ERROR_NONE) {
          // grpc_diffproc_list_add_streams(t, s);
          //             startStreams(t);
          puts("No error");
        } else {
          grpc_diffproc_cancel_stream(
              t, s,
              grpc_error_set_int(
                  GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                      "Transport closed", &t->closed_with_error, 1),
                  GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE));
        }
      } else {
        if (!(op->send_message &&
              (op->payload->send_message.send_message->flags() &
               GRPC_WRITE_BUFFER_HINT))) {
          grpc_diffproc_initiate_write(t);
        }
      }
    } else {
      s->send_initial_metadata = nullptr;
      grpc_core::ExecCtx::Run(
          DEBUG_LOCATION, s->send_initial_metadata_finished,
          GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
              "Attempt to send initial metadata after stream was closed",
              &s->write_closed_error, 1));
    }
    if (op->payload->send_initial_metadata.peer_string != nullptr) {
      gpr_atm_rel_store(op->payload->send_initial_metadata.peer_string,
                        (gpr_atm)t->peer_string);
    }
  }

  // SEND MESSAGE ****************
  if (op->send_message) {
    s->send_message_finished = op->on_complete;
    if (s->write_closed) {
      op->payload->send_message.stream_write_closed = true;
      // We should NOT return an error here, so as to avoid a cancel OP being
      // started. The surface layer will notice that the stream has been
      // closed for writes and fail the send message op.
      op->payload->send_message.send_message.reset();
      grpc_core::ExecCtx::Run(
          DEBUG_LOCATION, s->send_message_finished,
          GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
              "Attempt to send initial metadata after stream was closed",
              &s->write_closed_error, 1));

    } else {
      message_transfer_locked(t, s);
      grpc_diffproc_initiate_write(t);
    }
  }

  // TRAILING METADATA
  if (op->send_trailing_metadata) {
    GPR_ASSERT(s->send_trailing_metadata_finished == nullptr);
    s->send_trailing_metadata_finished = on_complete;
    s->send_trailing_metadata =
        op->payload->send_trailing_metadata.send_trailing_metadata;
    s->write_buffering = false;
    const size_t metadata_size =
        grpc_metadata_batch_size(s->send_trailing_metadata);
    if (s->write_closed) {
      s->send_trailing_metadata = nullptr;
      grpc_core::ExecCtx::Run(
          DEBUG_LOCATION, s->send_trailing_metadata_finished,
          grpc_metadata_batch_is_empty(
              op->payload->send_trailing_metadata.send_trailing_metadata)
              ? GRPC_ERROR_NONE
              : GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                    "Attempt to send trailing metadata after "
                    "stream was closed"));
    } else {
      grpc_diffproc_initiate_write(t);
    }
  }

  // RECV INITIAL METADATA
  if (op->recv_initial_metadata) {
    GPR_ASSERT(s->recv_initial_metadata_ready == nullptr);
    s->recv_initial_metadata_ready =
        op->payload->recv_initial_metadata.recv_initial_metadata_ready;
    s->recv_initial_metadata =
        op->payload->recv_initial_metadata.recv_initial_metadata;
    s->trailing_metadata_available =
        op->payload->recv_initial_metadata.trailing_metadata_available;
    if (op->payload->recv_initial_metadata.peer_string != nullptr) {
      gpr_atm_rel_store(op->payload->recv_initial_metadata.peer_string,
                        (gpr_atm)t->peer_string);
    }
    if (s->recv_initial_metadata_ready != nullptr) {
      grpc_closure* c = s->recv_initial_metadata_ready;
      s->recv_initial_metadata_ready = nullptr;
      grpc_core::ExecCtx::Run(DEBUG_LOCATION, c, GRPC_ERROR_NONE);
    }
  }

  // Receive Message
  if (op->recv_message) {
    size_t before = 0;
    GPR_ASSERT(s->recv_message_ready == nullptr);
    s->recv_message_ready = op->payload->recv_message.recv_message_ready;
    s->recv_message = op->payload->recv_message.recv_message;
    continue_read_action_locked(t);
    message_read_locked(t, s);
  }

  // RECV TRAILING MD
  if (op->recv_trailing_metadata) {

    //s->collecting_stats = op->payload->recv_trailing_metadata.collect_stats;
    GPR_ASSERT(s->recv_trailing_metadata_finished == nullptr);
    s->recv_trailing_metadata_finished =
        op->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
    s->recv_trailing_metadata =
        op->payload->recv_trailing_metadata.recv_trailing_metadata;
    s->final_metadata_requested = true;
    if (s->recv_trailing_metadata_finished != nullptr) {
      grpc_closure* c = s->recv_trailing_metadata_finished;
      s->recv_trailing_metadata_finished = nullptr;
      grpc_core::ExecCtx::Run(DEBUG_LOCATION, c, GRPC_ERROR_NONE);
    }
  }

  if (on_complete != nullptr) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_complete, GRPC_ERROR_NONE);
  }
}

  static void perform_stream_op(grpc_transport* gt, grpc_stream* gs,
                                 grpc_transport_stream_op_batch* op) {
     printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
     GPR_TIMER_SCOPE("perform_stream_op", 0);
    grpc_diffproc_transport* t = reinterpret_cast<grpc_diffproc_transport*>(gt);
     grpc_diffproc_stream* s = reinterpret_cast<grpc_diffproc_stream*>(gs);
    printf("Stream in perform_stream_op_locked :%p && Stream op batch : %p\n",s, op);
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

       char* str = grpc_transport_stream_op_batch_string(op);
       gpr_log(GPR_INFO, "perform_stream_op[s=%p]: %s", s, str);
       gpr_free(str);
     op->handler_private.extra_arg = gs;
     grpc_core::ExecCtx::Run(DEBUG_LOCATION,GRPC_CLOSURE_INIT(&op->handler_private.closure, perform_stream_op_locked, op, nullptr), GRPC_ERROR_NONE);
   }

    void perform_transport_op(grpc_transport* gt, grpc_transport_op* op) {
     printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
     grpc_diffproc_transport* t = reinterpret_cast<grpc_diffproc_transport*>(gt);
     DIFFPROC_LOG(GPR_INFO, "perform_transport_op %p %p", t, op);
     char* msg = grpc_transport_op_string(op);
     printf("perform_transport_op %p %p", t, op);
     printf("perform_transport_op[t=%p]: %s \n", t, msg);
     gpr_free(msg);

     op->handler_private.extra_arg = gt;
     gpr_mu_unlock(&t->mu);
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

  void read_action_locked(void* tp, grpc_error* error) {
     grpc_diffproc_transport* t = static_cast<grpc_diffproc_transport*>(tp);

     GRPC_ERROR_REF(error);

     grpc_error* err = error;
     if (err != GRPC_ERROR_NONE) {
       close_transport_locked(t, GRPC_ERROR_REF(error));
     } else {
       if (t->accept_stream_cb != nullptr) {
         grpc_diffproc_stream* accepting = nullptr;
         printf(" Transport : %p calling accept stream cb %p %p \n", t,
                t->accept_stream_cb, t->accept_stream_data);
         GPR_ASSERT(t->accepting_stream == nullptr);
         t->accepting_stream = &accepting;
         printf(
             "Stream in perform_stream_op_locked : %p\n", &t->accepting_stream);
         t->accept_stream_cb(t->accept_stream_data, &t->base,
                             (void*)(&t->accepting_stream));
         t->accepting_stream = nullptr;
       }
     }
   }

  void continue_read_action_locked(grpc_diffproc_transport* t) {
     grpc_endpoint_read(
         t->ep, &t->read_buffer,
         GRPC_CLOSURE_INIT(&t->read_action_locked, read_action_locked, t,
                           grpc_schedule_on_exec_ctx),
         GRPC_ERROR_NONE);
  }


   void grpc_diffproc_transport_start_reading(
       grpc_transport* transport, grpc_slice_buffer* read_buffer) {
     printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
     grpc_diffproc_transport* t =
         reinterpret_cast<grpc_diffproc_transport*>(transport);
         grpc_endpoint_read(
             t->ep, read_buffer,
             GRPC_CLOSURE_INIT(&t->read_action_locked, read_action_locked, t,
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







               
