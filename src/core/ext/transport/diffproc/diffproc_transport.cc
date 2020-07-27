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

grpc_slice g_empty_slice;
grpc_slice g_fake_path_key;
grpc_slice g_fake_path_value;
grpc_slice g_fake_auth_key;
grpc_slice g_fake_auth_value;
grpc_slice g_fake_status_key;

static const grpc_transport_vtable* get_vtable(void);
static void log_metadata(const grpc_metadata_batch* md_batch, bool is_client, bool is_initial);

//Cancel and Close Stream
bool cancel_stream_locked(grpc_diffproc_stream* s, grpc_error* error);
void close_stream_locked(grpc_diffproc_transport* t, grpc_diffproc_stream* s, int close_reads,int close_writes, grpc_error* error);




//Writing Functions..
static void write_action(void* t, grpc_error* error);
static void write_action_end(void* t, grpc_error* error);
static void write_action_end_locked(void* t, grpc_error* error);


//Reading Functions..
static void read_action(void* t, grpc_error* error);
static void read_action_end(void* t, grpc_error* error);
static void read_action_locked(grpc_diffproc_transport* t);



//COTR
grpc_diffproc_transport::grpc_diffproc_transport(
    const grpc_channel_args* channel_args, grpc_endpoint* ep, bool is_client,
    grpc_resource_user* resource_user)
    : ep(ep),state_tracker(is_client ? "client_transport" : "server_transport",GRPC_CHANNEL_READY),is_client(is_client) 
{
  printf("\n%d :: %s :: %s :: %p\n", __LINE__, __func__, __FILE__, this);
  base.vtable = get_vtable();
  gpr_ref_init(&refs, 1);
  grpc_slice_buffer_init(&read_buffer);
  grpc_slice_buffer_init(&outbuf);
  if (is_client) {
    //grpc_slice_buffer_add(&outbuf,grpc_slice_from_copied_string("Diff proc Transport"));
   //grpc_diffproc_initiate_write(this);
  }
  
}

//DOTR
grpc_diffproc_transport::~grpc_diffproc_transport() {
  printf(" TRANSPORT DOTR ... \n");
  grpc_endpoint_destroy(ep);
  grpc_slice_buffer_destroy_internal(&outbuf);
  grpc_slice_buffer_destroy_internal(&read_buffer);
  //GRPC_ERROR_UNREF(closed_with_error);
  //gpr_free(peer_string);
}

//REF
void grpc_diffproc_transport::ref() {
  printf( "ref_transport: %p \n", this);
  gpr_ref(&refs);
}


//UNREF
void grpc_diffproc_transport::unref() {
  printf( "unref_transport: %p \n", this);
  if (!gpr_unref(&refs)) {
    return;
  }
  printf( "really_destroy_transport %p \n", this);
  this->~grpc_diffproc_transport();
  gpr_free(this);
}



void mdToBuffer(grpc_diffproc_stream* s,const grpc_metadata_batch* metadata, bool* markfilled, grpc_slice_buffer* outbuf) {
  grpc_slice_buffer_reset_and_unref(outbuf);
  if (markfilled != nullptr) {
    *markfilled = true;
  }
  grpc_slice path_slice = GRPC_MDVALUE(metadata->idx.named.path->md);
  grpc_slice_buffer_add(outbuf, grpc_slice_ref_internal(path_slice));
}


//grpc_slice bufferToMd( grpc_slice_buffer* read_buffer) {
//  for (size_t i = 0; i <  read_buffer->count; i++) {
//   printf(" %s \n", grpc_slice_to_c_string(read_buffer->slices[i]));
//    return read_buffer->slices[i];
//  } 
//
//}


grpc_slice bufferToMd(grpc_slice_buffer* slice_buffer) {
  char* payload_bytes =
      static_cast<char*>(gpr_malloc(slice_buffer->length + 1));
  size_t offset = 0;
  for (size_t i = 0; i < slice_buffer->count; ++i) {
    memcpy(payload_bytes + offset,
           GRPC_SLICE_START_PTR(slice_buffer->slices[i]),
           GRPC_SLICE_LENGTH(slice_buffer->slices[i]));
    offset += GRPC_SLICE_LENGTH(slice_buffer->slices[i]);
  }
  *(payload_bytes + offset) = '\0';
  return grpc_slice_from_static_string(payload_bytes);
}



grpc_error* fill_in_metadata(grpc_diffproc_stream* s,
                             const grpc_metadata_batch* metadata,
                             uint32_t flags, grpc_metadata_batch* out_md,
                             uint32_t* outflags, bool* markfilled) {
  //if (GRPC_TRACE_FLAG_ENABLED(grpc_diffproc_trace)) {
    log_metadata(metadata, s->t->is_client, outflags != nullptr);
 // }

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

//void op_state_machine_locked(grpc_diffproc_stream* s, grpc_error* error) {
//  // This function gets called when we have contents in the unprocessed reads
//  // Get what we want based on our ops wanted
//  // Schedule our appropriate closures
//  // and then return to ops_needed state if still needed
//  if (s->cancel_self_error != GRPC_ERROR_NONE) {
//    fail_helper_locked(s, GRPC_ERROR_REF(s->cancel_self_error));
//    goto done;
//  } else if (error != GRPC_ERROR_NONE) {
//    fail_helper_locked(s, GRPC_ERROR_REF(error));
//    goto done;
//  }
//
//  //Send Trailing Metadata && write buffer empty
//  if (s->send_trailing_md_op && !s->write_buffer_trailing_md_filled) {
//    grpc_metadata_batch* dest = &s->write_buffer_trailing_md;
//    fill_in_metadata(s,
//                     s->send_trailing_md_op->payload->send_trailing_metadata
//                         .send_trailing_metadata,
//                     0, dest, nullptr, &s->write_buffer_trailing_md_filled);
//    s->trailing_md_sent = true;
//  } 
//  ////Receive Trailing Metadata && Read buffer empty
//  else if (s->recv_trailing_md_op && !s->to_read_trailing_md_filled) {
//    s->trailing_md_recvd = true;
//    grpc_error* new_err =
//        fill_in_metadata(s, &s->to_read_trailing_md, 0,
//                         s->recv_trailing_md_op->payload->recv_trailing_metadata
//                             .recv_trailing_metadata,
//                         nullptr, nullptr);
//    //If client need to send complete status as this is last batch operation.
//    if (s->t->is_client) {
//        printf("op_state_machine %p scheduling trailing-md-on-complete %p", s,
//                 new_err);
//      grpc_core::ExecCtx::Run(
//          DEBUG_LOCATION,
//          s->recv_trailing_md_op->payload->recv_trailing_metadata
//              .recv_trailing_metadata_ready,
//          GRPC_ERROR_REF(new_err));
//      grpc_core::ExecCtx::Run(DEBUG_LOCATION,
//                              s->recv_trailing_md_op->on_complete,
//                              GRPC_ERROR_REF(new_err));
//      s->recv_trailing_md_op = nullptr;
//      //needs_close = s->trailing_md_sent;
//    } 
//    //Server so wait..
//    else {
//      printf("op_state_machine %p server needs to delay handling "
//          "trailing-md-on-complete %p",
//          s, new_err);
//    }
//  }
//
//
//
//done:
//  puts("Error Here ******");
//}

//void maybe_process_ops_locked(grpc_diffproc_stream* s, grpc_error* error) {
//  if (s && (error != GRPC_ERROR_NONE || s->ops_needed)) {
//    s->ops_needed = false;
//    op_state_machine_locked(s, error);
//  }
//}


void grpc_diffproc_stream_map_add(grpc_diffproc_transport* t, uint32_t id, void* value) {
  
  printf("Adding stream - ID [%d] from transport [%p] to stream_map.. \n", id, t, static_cast<grpc_diffproc_stream*>(value));
  if (t->stream_map.find(id) == t->stream_map.end()) t->stream_map[id] = static_cast<grpc_diffproc_stream*>(value);
  }



#ifndef NDEBUG
  void grpc_diffproc_stream_ref(grpc_diffproc_stream* s, const char* reason) {
    grpc_stream_ref(s->refcount, reason);
  }
  void grpc_diffproc_stream_unref(grpc_diffproc_stream* s, const char* reason) {
    grpc_stream_unref(s->refcount, reason);
  }
#else
  void grpc_diffproc_stream_ref(grpc_diffproc_stream* s) {
    grpc_stream_ref(s->refcount);
  }
  void grpc_diffproc_stream_unref(grpc_diffproc_stream* s) {
    grpc_stream_unref(s->refcount);
  }
#endif



grpc_diffproc_stream::Reffer::Reffer(grpc_diffproc_stream* s) {
    /* We reserve one 'active stream' that's dropped when the stream is
       read-closed. The others are for NP Streams that are
       actively reading */
    grpc_diffproc_stream_ref(s, "diffproc");
    s->t->ref();
  }

//STREAM COTR
grpc_diffproc_stream::grpc_diffproc_stream(grpc_diffproc_transport* t, grpc_stream_refcount* refcount,const void* server_data, grpc_core::Arena* arena)
      : t(t), refcount(refcount), arena(arena), reffer(this) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);

  printf(" STREAM COTR ...  :%p \n", this);
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
    puts("Client side...");

  } else {
    // Called from accept stream function of server.cc
    printf(" Transport : %p in server \n", t);
    puts("Server side...");
    id = static_cast<uint32_t>((uintptr_t)server_data);
    *t->accepting_stream = this;
    printf("init stream ID: [%p] \n", *t->accepting_stream);
    //grpc_diffproc_stream_map_add(t, id, this);
  }
}

//STREAM DOTR
grpc_diffproc_stream::~grpc_diffproc_stream() { 
  printf(" STREAM DOTR ...  :%p \n", this);
  GRPC_ERROR_UNREF(cancel_self_error);

  GPR_ASSERT(send_initial_metadata_finished == nullptr);
  GPR_ASSERT(send_trailing_metadata_finished == nullptr);
  GPR_ASSERT(recv_initial_metadata_ready == nullptr);
  GPR_ASSERT(recv_message_ready == nullptr);
  GPR_ASSERT(recv_trailing_metadata_finished == nullptr);

  t->unref();


  if (closure_at_destroy) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, closure_at_destroy,GRPC_ERROR_NONE);
  }
}


  int init_stream(grpc_transport * gt, grpc_stream * gs,
               grpc_stream_refcount * refcount, const void* server_data,
               grpc_core::Arena* arena) {
    printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
     GPR_TIMER_SCOPE("init_stream", 0);
     grpc_diffproc_transport* t = reinterpret_cast<grpc_diffproc_transport*>(gt);
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

   //Close Transport -- Shutdown endpoint called
   static void close_transport_locked(grpc_diffproc_transport* t, grpc_error* error) {
     printf( "close_transport %p %d \n", t, t->is_closed);
     t->closed_with_error = GRPC_ERROR_REF(error);
     t->state_tracker.SetState(GRPC_CHANNEL_SHUTDOWN, "close transport");
     if (!t->is_closed) {
       t->is_closed = true;
       /* Also end all streams on this transport */
       while (t->stream_list != nullptr) {
         // cancel_stream_locked also adjusts stream list
         cancel_stream_locked(
             t->stream_list,
             grpc_error_set_int(
                 GRPC_ERROR_CREATE_FROM_STATIC_STRING("Transport closed"),
                 GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE));
       }
       grpc_endpoint_shutdown(t->ep, GRPC_ERROR_REF(error));
     }
   }

   void destroy_stream(grpc_transport* /*gt*/, grpc_stream * gs,
                       grpc_closure * then_schedule_closure) {
     printf("In destroy stream ******************** \n");
     grpc_diffproc_stream* s = reinterpret_cast<grpc_diffproc_stream*>(gs);
     s->closure_at_destroy = then_schedule_closure;
     s->~grpc_diffproc_stream();
     
   }

   void destroy_transport(grpc_transport* gt) {
     grpc_diffproc_transport* t = reinterpret_cast<grpc_diffproc_transport*>(gt);
     printf("In destroy transport **************** \n");
     close_transport_locked(t, GRPC_ERROR_CREATE_FROM_STATIC_STRING("destroying transport"));
     t->unref();
   }


   void write_action_end(void* tp, grpc_error* error) {
     grpc_diffproc_transport* t = static_cast<grpc_diffproc_transport*>(tp);
     bool closed = false;
     if (error != GRPC_ERROR_NONE) {
       close_transport_locked(t, GRPC_ERROR_REF(error));
       closed = true;
     }
     t->unref();
   }

   //void read_action_end(void* tp, grpc_error* error) {
   //  grpc_diffproc_transport* t = static_cast<grpc_diffproc_transport*>(tp);
   //  grpc_error* err;

   //  err = GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
   //      "Read action closed", &error, 1);
   //  close_transport_locked(t, GRPC_ERROR_REF(error));
   //  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
   //  GRPC_ERROR_UNREF(error);
   //}

  static void log_metadata(const grpc_metadata_batch* md_batch,
                            bool is_client, bool is_initial) {
     printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
     for (grpc_linked_mdelem* md = md_batch->list.head; md != nullptr;
          md = md->next) {
       char* key = grpc_slice_to_c_string(GRPC_MDKEY(md->md));
       char* value = grpc_slice_to_c_string(GRPC_MDVALUE(md->md));
       printf("DIFFPROC :%s:%s: %s: %s \n",
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


bool cancel_stream_locked( grpc_diffproc_stream* stream, grpc_error* error) {
  bool ret = false;  // was the cancel accepted
  printf("cancel_stream %p with %s \n", stream, grpc_error_string(error));
  if (stream->cancel_self_error == GRPC_ERROR_NONE) {
    ret = true;
    stream->cancel_self_error = GRPC_ERROR_REF(error);
    // Send trailing md to other side.
    //maybe_process_ops_locked(stream, stream->cancel_self_error);
    stream->trailing_md_sent = true;
    grpc_metadata_batch cancel_md;
    grpc_metadata_batch_init(&cancel_md);

    if (!stream->t->is_client && stream->trailing_md_recvd &&
        stream->recv_trailing_md_op) {
      grpc_core::ExecCtx::Run(
          DEBUG_LOCATION,
          stream->recv_trailing_md_op->payload->recv_trailing_metadata
              .recv_trailing_metadata_ready,
          GRPC_ERROR_REF(stream->cancel_self_error));
    }
  }
  close_stream_locked(stream->t, stream, 1, 1, error);
  return ret;

}


//Write Buffer
void message_transfer_locked(grpc_diffproc_transport* t, grpc_diffproc_stream* sender) {
  size_t remaining =
      sender->send_message_op->payload->send_message.send_message->length();
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
  grpc_diffproc_initiate_write(t);
  sender->send_message_op = nullptr;
}

static void null_then_sched_closure(grpc_closure** closure) {
  grpc_closure* c = *closure;
  *closure = nullptr;
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, c, GRPC_ERROR_NONE);
}

//Read Buffer
void message_read_locked(grpc_diffproc_transport* t,
                             grpc_diffproc_stream* receiver) {

  receiver->recv_stream.Init(&t->read_buffer, 0);
  receiver->recv_message_op->payload->recv_message.recv_message->reset(
  receiver->recv_stream.get());

  //printf("message_transfer_locked %p scheduling message-ready", receiver);

  receiver->recv_message_op = nullptr;
}


void grpc_chttp2_complete_closure_step(grpc_diffproc_transport* t,
                                       grpc_diffproc_stream* /*s*/,
                                       grpc_closure** pclosure,
                                       grpc_error* error, const char* desc) {
  grpc_closure* closure = *pclosure;
  *pclosure = nullptr;
  if (closure == nullptr) {
    GRPC_ERROR_UNREF(error);
    return;
  }
  if (error != GRPC_ERROR_NONE) {
    if (closure->error_data.error == GRPC_ERROR_NONE) {
      closure->error_data.error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Error in DIFFPROC transport completing operation");
      closure->error_data.error = grpc_error_set_str(closure->error_data.error, GRPC_ERROR_STR_TARGET_ADDRESS,grpc_slice_from_copied_string(t->peer_string));
    }
    closure->error_data.error = grpc_error_add_child(closure->error_data.error, error);
  }
  //if (t->write_state == GRPC_CHTTP2_WRITE_STATE_IDLE) {
    // Using GRPC_CLOSURE_SCHED instead of GRPC_CLOSURE_RUN to avoid running
    // closures earlier than when it is safe to do so.
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, closure, closure->error_data.error);
 //}
}


void grpc_diffproc_fail_pending_writes(grpc_diffproc_transport* t,
                                     grpc_diffproc_stream* s, grpc_error* error) {
  s->send_initial_metadata = nullptr;
  grpc_chttp2_complete_closure_step(t, s, &s->send_initial_metadata_finished,
                                    GRPC_ERROR_REF(error),
                                    "send_initial_metadata_finished");

  s->send_trailing_metadata = nullptr;
  grpc_chttp2_complete_closure_step(t, s, &s->send_trailing_metadata_finished,
                                    GRPC_ERROR_REF(error),
                                    "send_trailing_metadata_finished");

}

void grpc_diffproc_maybe_complete_recv_message(grpc_diffproc_transport* /*t*/,grpc_diffproc_stream* s) {
  grpc_error* err = GRPC_ERROR_NONE;
  if (s->recv_message_ready != nullptr) {
    *s->recv_message = nullptr;
    if (err == GRPC_ERROR_NONE && *s->recv_message != nullptr) {
      null_then_sched_closure(&s->recv_message_ready);
    }
    GRPC_ERROR_UNREF(err);
  }
}

void grpc_diffproc_maybe_complete_recv_trailing_metadata(grpc_diffproc_transport* t, grpc_diffproc_stream* s) {
  grpc_diffproc_maybe_complete_recv_message(t, s);
  if (s->recv_trailing_metadata_finished != nullptr && s->read_closed && s->write_closed) {
    if (s->read_closed  && s->recv_trailing_metadata_finished != nullptr) {
      null_then_sched_closure(&s->recv_trailing_metadata_finished);
    }
  }
}

//Close Stream
void close_stream_locked(grpc_diffproc_transport* t, grpc_diffproc_stream* s, int close_reads, int close_writes, grpc_error* error) {
  if (s->read_closed && s->write_closed) {
    /* already closed */
    //grpc_chttp2_maybe_complete_recv_trailing_metadata(t, s);
    GRPC_ERROR_UNREF(error);
    return;
  }
  bool closed_read = false;
  bool became_closed = false;
  if (close_reads && !s->read_closed) {
    s->read_closed_error = GRPC_ERROR_REF(error);
    s->read_closed = true;
    closed_read = true;
  }
  if (close_writes && !s->write_closed) {
    s->write_closed_error = GRPC_ERROR_REF(error);
    s->write_closed = true;
    grpc_diffproc_fail_pending_writes(t, s, GRPC_ERROR_REF(error));
  }
  if (s->read_closed && s->write_closed) {
    if (!s->closed) {
      //  // Release the metadata that we would have written out

      if (s->listed) {
        grpc_diffproc_stream* p = s->stream_list_prev;
        grpc_diffproc_stream* n = s->stream_list_next;
        if (p != nullptr) {
          p->stream_list_next = n;
        } else {
          s->t->stream_list = n;
        }
        if (n != nullptr) {
          n->stream_list_prev = p;
        }
        s->listed = false;
        // grpc_diffproc_stream_unref(s,"close_stream:list");
      }

      s->closed = true;
    }
    became_closed = true;
  }
  if (closed_read) {
    if (s->recv_initial_metadata_ready != nullptr) {
      null_then_sched_closure(&s->recv_initial_metadata_ready);
    }
  }
  if (became_closed) {
    grpc_diffproc_maybe_complete_recv_trailing_metadata(t, s);
    grpc_diffproc_stream_unref(s, "diffproc");
  }
  GRPC_ERROR_UNREF(error);


}





static void perform_stream_op_locked(void* stream_op,
                                     grpc_error* /*error_ignored*/) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  bool needs_close = false;
  bool isSched = false;
  grpc_transport_stream_op_batch* op = static_cast<grpc_transport_stream_op_batch*>(stream_op);
  grpc_diffproc_stream* s = static_cast<grpc_diffproc_stream*>(op->handler_private.extra_arg);
  printf("Stream in perform_stream_op_locked :%p && Stream op batch : %p & ""Trasnport : %p \n",s, op, s->t);
  grpc_diffproc_transport* t = s->t;

  if (GRPC_TRACE_FLAG_ENABLED(grpc_diffproc_trace)) {
    if (op->send_initial_metadata) {
      log_metadata(op->payload->send_initial_metadata.send_initial_metadata, t->is_client, true);
    }
    if (op->send_trailing_metadata) {
      log_metadata(op->payload->send_trailing_metadata.send_trailing_metadata, t->is_client, false);
    }
  }

  grpc_diffproc_stream_ref(s, "perform_stream_op");

  grpc_closure* on_complete = op->on_complete;
  // on_complete will be null if and only if there are no send ops in the
  // batch.
  if (on_complete == nullptr) {
    on_complete = GRPC_CLOSURE_INIT(&op->handler_private.closure, do_nothing, nullptr, grpc_schedule_on_exec_ctx);
  }

  if (op->cancel_stream) {
    cancel_stream_locked(s, op->payload->cancel_stream.cancel_error);
  }

  //Send INITITAL MD
  if (op->send_initial_metadata) {
    printf("*************  SEND INIT MD ******************* :      %s \n",s->t->is_client?"CLT":"SRV");
    grpc_closure* send_initial_metadata_finished = op->on_complete;
    GPR_ASSERT(s->send_initial_metadata_finished == nullptr);
    s->send_initial_metadata = op->payload->send_initial_metadata.send_initial_metadata;
    const size_t metadata_size = grpc_metadata_batch_size(s->send_initial_metadata);
    if (t->is_client) {
      s->deadline = GPR_MIN(s->deadline, s->send_initial_metadata->deadline);
    }
    if (!s->write_closed) {
      if (t->is_client) { // Client--  Add path and authority
        bool markFilled = false;
        if (t->closed_with_error == GRPC_ERROR_NONE) {
          mdToBuffer(s, s->send_initial_metadata, &markFilled, &s->t->outbuf);
          grpc_diffproc_initiate_write(t);
        } else {
          cancel_stream_locked(s,grpc_error_set_int(GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING("Transport closed", &t->closed_with_error, 1)
            ,GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE));
        }
      } else { // Nothing just send null
        if (!(op->send_message)) {
          grpc_diffproc_initiate_write(t);
        }
      }
    } else {
      s->send_initial_metadata = nullptr;
      grpc_core::ExecCtx::Run(DEBUG_LOCATION, s->send_initial_metadata_finished, 
        GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING("Attempt to send initial metadata after stream was closed", &s->write_closed_error, 1));
    }
    if (op->payload->send_initial_metadata.peer_string != nullptr) {
      gpr_atm_rel_store(op->payload->send_initial_metadata.peer_string,
                        (gpr_atm)t->peer_string);
    }
  }

  // SEND MESSAGE ****************
  if (op->send_message) {
    printf("*************  SEND MSG *******************:    %s \n",
         s->t->is_client ? "CLT" : "SRV");
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
      s->send_message_op = op;
      message_transfer_locked(t, s);
      //Send message complete
      //if (!isSched) {
      //  isSched = true;
      //  grpc_core::ExecCtx::Run(DEBUG_LOCATION, s->send_message_finished,
      //                          GRPC_ERROR_NONE);
      //}
      //grpc_diffproc_initiate_write(t);
    }
  }

  //// TRAILING METADATA
  if (op->send_trailing_metadata) {
    puts("*************  SEND TRAIL MD *******************");
    GPR_ASSERT(s->send_trailing_metadata_finished == nullptr);
    s->send_trailing_metadata_finished = on_complete;
    s->send_trailing_metadata =
        op->payload->send_trailing_metadata.send_trailing_metadata;
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
      if (s->trailing_md_sent) {
        puts("Error -- Extra trailing handle data it later...");
      }
      else {
        if (!s->t->is_client) {
          grpc_slice_buffer_reset_and_unref_internal(&s->t->outbuf);
          grpc_slice grpc_status_slice = GRPC_MDVALUE(
              s->send_trailing_metadata->idx.named.grpc_status->md);
          grpc_slice_buffer_add(&s->t->outbuf,
                                grpc_slice_ref_internal(grpc_status_slice));
          grpc_diffproc_initiate_write(t);
        }
        s->trailing_md_sent = true;
        needs_close = s->trailing_md_sent;
        // Send trailing MD complete
        //puts("************* Send trailing MD complete ***************");
        
        /*if (!isSched) {
          isSched = true;
          grpc_core::ExecCtx::Run(DEBUG_LOCATION,
                                  s->send_trailing_metadata_finished,
                                  GRPC_ERROR_NONE);
        }*/
      //  grpc_core::ExecCtx::Run(DEBUG_LOCATION,
      //                          s->send_trailing_metadata_finished,
      //                          GRPC_ERROR_NONE);
      }
    }


  }

  // RECV INITIAL METADATA
  if (op->recv_initial_metadata) {
    printf("*************  RECV INIT MD *******************     :%s \n",
         s->t->is_client ? "CLT" : "SRV");
    GPR_ASSERT(s->recv_initial_metadata_ready == nullptr);
    s->recv_initial_metadata_ready = op->payload->recv_initial_metadata.recv_initial_metadata_ready; //closure
    s->recv_initial_metadata = op->payload->recv_initial_metadata.recv_initial_metadata;   //metadata
    /* IF SERVER INITIALIZE path and authority*/
    if(!s->t->is_client) {
      read_action_locked(s->t);
      printf("The read buffer length : %d \n", s->t->read_buffer.length);
      grpc_metadata_batch fake_md;
      grpc_metadata_batch_init(&fake_md);
      grpc_linked_mdelem* path_md = static_cast<grpc_linked_mdelem*>(s->arena->Alloc(sizeof(*path_md)));
      path_md->md = grpc_mdelem_from_slices(g_fake_path_key,bufferToMd(&s->t->read_buffer));
      GPR_ASSERT(grpc_metadata_batch_link_tail(&fake_md, path_md) == GRPC_ERROR_NONE);
      grpc_linked_mdelem* auth_md = static_cast<grpc_linked_mdelem*>(s->arena->Alloc(sizeof(*auth_md)));
      auth_md->md = grpc_mdelem_from_slices(g_fake_auth_key, g_fake_auth_value);
      GPR_ASSERT(grpc_metadata_batch_link_tail(&fake_md, auth_md) == GRPC_ERROR_NONE);
      fill_in_metadata(
          s, &fake_md, 0, s->recv_initial_metadata,
          op->payload->recv_initial_metadata.recv_flags,
          nullptr);
      grpc_metadata_batch_destroy(&fake_md);
    }

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

  //// Receive Message
  if (op->recv_message) {
    puts("*************  RECV MSG *******************");
    GPR_ASSERT(s->recv_message_ready == nullptr);
    s->recv_message_ready = op->payload->recv_message.recv_message_ready;
    s->recv_message_op = op;
    read_action_locked(t);
    message_read_locked(t, s);
    if (s->recv_message_ready != nullptr) {
      grpc_closure* c = s->recv_message_ready;
      s->recv_message_ready = nullptr;
      grpc_core::ExecCtx::Run(DEBUG_LOCATION, c, GRPC_ERROR_NONE);
    }
  }

  //// RECV TRAILING MD
  if (op->recv_trailing_metadata) {
    puts("*************  RECV TRAIL MD *******************");
    GPR_ASSERT(s->recv_trailing_metadata_finished == nullptr);
    s->recv_trailing_metadata_finished =
        op->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
    s->recv_trailing_metadata =
        op->payload->recv_trailing_metadata.recv_trailing_metadata;

    s->recv_trailing_md_op = op;
    if (s->trailing_md_recvd) {
      puts("Already received -- Implement goto done");
    } else {
      grpc_metadata_batch* dest = &s->to_read_trailing_md;
      bool* destfilled = &s->to_read_trailing_md_filled;
      s->trailing_md_recvd = true;
      //TODO  : Here to_read_trailing_md .. We need to copy from our read_buffer.. 
      if (s->t->is_client) {
        read_action_locked(s->t);
        printf("The read buffer length : %d \n", s->t->read_buffer.length);
        grpc_metadata_batch fake_md;
        grpc_metadata_batch_init(&fake_md);
        grpc_linked_mdelem* status_md = static_cast<grpc_linked_mdelem*>(
            s->arena->Alloc(sizeof(*status_md)));
        status_md->md = grpc_mdelem_from_slices(g_fake_status_key,
                                              bufferToMd(&s->t->read_buffer));
        GPR_ASSERT(grpc_metadata_batch_link_tail(&fake_md, status_md) ==
                   GRPC_ERROR_NONE);
        fill_in_metadata(s, &fake_md, 0, s->recv_trailing_metadata,nullptr,nullptr);
        grpc_metadata_batch_destroy(&fake_md);
      }
      s->to_read_trailing_md_filled = false;
    }
    if (s->t->is_client) {
      if (s->recv_trailing_metadata_finished != nullptr) {
        grpc_closure* c = s->recv_trailing_metadata_finished;
        s->recv_trailing_metadata_finished = nullptr;
        grpc_core::ExecCtx::Run(DEBUG_LOCATION, c, GRPC_ERROR_NONE);
      }
      grpc_core::ExecCtx::Run(DEBUG_LOCATION,
                              s->recv_trailing_md_op->on_complete,
                              GRPC_ERROR_REF(GRPC_ERROR_NONE));
      s->recv_trailing_md_op = nullptr;
    } else {
      if (s->recv_trailing_metadata_finished != nullptr) {
        grpc_closure* c = s->recv_trailing_metadata_finished;
        s->recv_trailing_metadata_finished = nullptr;
        grpc_core::ExecCtx::Run(DEBUG_LOCATION, c, GRPC_ERROR_NONE);
      }
    }
  }

  //if (on_complete != nullptr) {
  //  puts("*********** On COMPLETE CLOSURE ************");
  //  grpc_closure* closure = on_complete;
  //  on_complete = nullptr;
  //  grpc_core::ExecCtx::Run(DEBUG_LOCATION, closure, GRPC_ERROR_NONE);
  //}

  grpc_diffproc_stream_unref(s, "perform_stream_op");

}

//Perform Stream OP
  static void perform_stream_op(grpc_transport* gt, grpc_stream* gs,
                                 grpc_transport_stream_op_batch* op) {
     printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
     GPR_TIMER_SCOPE("perform_stream_op", 0);
    grpc_diffproc_transport* t = reinterpret_cast<grpc_diffproc_transport*>(gt);
     grpc_diffproc_stream* s = reinterpret_cast<grpc_diffproc_stream*>(gs);
    printf("Stream in perform_stream_op_locked :%p && Stream op batch : %p\n",s, op);

    if (op->send_initial_metadata) {
      log_metadata(op->payload->send_initial_metadata.send_initial_metadata,
                   s->t->is_client, true);
    }
    if (op->send_trailing_metadata) {
      log_metadata(op->payload->send_trailing_metadata.send_trailing_metadata,
                   s->t->is_client, false);
    }


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


  //Perform Transport OP
 void perform_transport_op(grpc_transport* gt, grpc_transport_op* op) {
     printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
     grpc_diffproc_transport* t = reinterpret_cast<grpc_diffproc_transport*>(gt);
    printf( "perform_transport_op %p %p \n", t, op);
     printf("PERFORM TRANSPORT OP:  :%s \n", t->is_client ? "CLT" : "SRV");
     char* msg = grpc_transport_op_string(op);

     gpr_free(msg);

     op->handler_private.extra_arg = gt;
     t->ref();
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

     if (op->disconnect_with_error != GRPC_ERROR_NONE) {
       close_transport_locked(t, op->disconnect_with_error);
       GRPC_ERROR_UNREF(op->disconnect_with_error);
     }

     t->unref();
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
     t->ref();
     grpc_endpoint_write(t->ep, &t->outbuf,GRPC_CLOSURE_INIT(&t->write_action_end_locked, write_action_end,t, grpc_schedule_on_exec_ctx),nullptr);
     //printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
   }

  void read_action_end(void* tp, grpc_error* error) {
     printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
     grpc_diffproc_transport* t = static_cast<grpc_diffproc_transport*>(tp);

     GRPC_ERROR_REF(error);

     grpc_error* err = error;
     if (err != GRPC_ERROR_NONE) {
       close_transport_locked(t, GRPC_ERROR_REF(error));
     } else {
       if (t->accept_stream_cb != nullptr && !t->is_client && !t->processed) {
         grpc_diffproc_stream* accepting = nullptr;
         GPR_ASSERT(t->accepting_stream == nullptr);
         t->accepting_stream = &accepting;
         t->accept_stream_cb(t->accept_stream_data, &t->base,(void*)(&t->accepting_stream));
         t->accepting_stream = nullptr;
         t->processed = 1;
       }
       printf("Read buf count after namedpipe read :%d \n",t->read_buffer.count);
     }
     t->unref();
   }

  void read_action_locked(grpc_diffproc_transport* t) {
     grpc_slice_buffer_reset_and_unref_internal(&t->read_buffer);
    t->ref();
     grpc_endpoint_read(
         t->ep, &t->read_buffer,
        GRPC_CLOSURE_INIT(&t->read_action_locked, read_action_end, t, grpc_schedule_on_exec_ctx),
         GRPC_ERROR_NONE);
  }


   void grpc_diffproc_transport_start_reading(
       grpc_transport* transport, grpc_slice_buffer* read_buffer) {
     printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
     grpc_diffproc_transport* t =
         reinterpret_cast<grpc_diffproc_transport*>(transport);
     t->ref();
     //if (read_buffer != nullptr) {
     //  grpc_slice_buffer_move_into(read_buffer, &t->read_buffer);
     //  gpr_free(read_buffer);
     //}
         //grpc_endpoint_read(
         //    t->ep, &t->read_buffer,
         //    GRPC_CLOSURE_INIT(&t->read_action_locked, read_action_locked, t,
         //                      grpc_schedule_on_exec_ctx),
         //    GRPC_ERROR_NONE);
     grpc_core::ExecCtx::Run(DEBUG_LOCATION,GRPC_CLOSURE_INIT(&t->read_action_locked, read_action_end, t,grpc_schedule_on_exec_ctx),GRPC_ERROR_NONE);
   }


 void grpc_diffproc_transport_init(void) {
     puts("Diff proc init");
     grpc_core::ExecCtx exec_ctx;
     g_empty_slice = grpc_core::ExternallyManagedSlice();

      
     grpc_slice key_tmp = grpc_slice_from_static_string(":path");
     g_fake_path_key = grpc_slice_intern(key_tmp);
     grpc_slice_unref_internal(key_tmp);

     grpc_slice status_tmp = grpc_slice_from_static_string("grpc-status");
     g_fake_status_key = grpc_slice_intern(status_tmp);
     grpc_slice_unref_internal(key_tmp);

     grpc_slice auth_tmp = grpc_slice_from_static_string(":authority");
     g_fake_auth_key = grpc_slice_intern(auth_tmp);
     grpc_slice_unref_internal(auth_tmp);

     g_fake_auth_value = grpc_slice_from_static_string("diffproc-fail");
 }

 void grpc_diffproc_transport_shutdown(void) {
   grpc_core::ExecCtx exec_ctx;
   grpc_slice_unref_internal(g_empty_slice);
   grpc_slice_unref_internal(g_fake_path_key);
   grpc_slice_unref_internal(g_fake_path_value);
   grpc_slice_unref_internal(g_fake_auth_key);
   grpc_slice_unref_internal(g_fake_auth_value);
   grpc_slice_unref_internal(g_fake_status_key);
 
 }







               
