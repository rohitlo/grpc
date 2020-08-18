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

#include "src/core/ext/transport/diffproc/diffproc_transport.h"

#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <string.h>

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
  do {                                                  \
    if (GRPC_TRACE_FLAG_ENABLED(grpc_diffproc_trace)) { \
      gpr_log(__VA_ARGS__);                             \
    }                                                   \
  } while (0)

grpc_slice g_empty_slice;
grpc_slice g_fake_path_key;
grpc_slice g_fake_path_value;
grpc_slice g_fake_auth_key;
grpc_slice g_fake_auth_value;
grpc_slice g_fake_status_key;

static const grpc_transport_vtable* get_vtable(void);
static void log_metadata(const grpc_metadata_batch* md_batch, bool is_client,
                         bool is_initial);
void complete_if_batch_end_locked(grpc_diffproc_stream* s, grpc_error* error,
                                  grpc_transport_stream_op_batch* op,
                                  const char* msg);
void maybe_process_ops_locked(grpc_diffproc_stream* s, grpc_error* error);
    // Cancel and Close Stream
bool cancel_stream_locked(grpc_diffproc_stream* s, grpc_error* error);
void close_stream_locked(grpc_diffproc_transport* t, grpc_diffproc_stream* s,
                         int close_reads, int close_writes, grpc_error* error);
void message_transfer_locked(grpc_diffproc_transport* t,
                             grpc_diffproc_stream* sender);
void message_read_locked(grpc_diffproc_transport* t,
                         grpc_diffproc_stream* receiver);
    // Writing Functions..
static void write_action(void* t, grpc_error* error);
static void write_action_end(void* t, grpc_error* error);
static void write_action_end_locked(void* t, grpc_error* error);

// Reading Functions..
static void read_action(void* t, grpc_error* error);
static void read_action_end(void* t, grpc_error* error);
static void read_action_locked(grpc_diffproc_transport* t);

// COTR
grpc_diffproc_transport::grpc_diffproc_transport(
    const grpc_channel_args* channel_args, grpc_endpoint* ep, bool is_client,
    grpc_resource_user* resource_user)
    : ep(ep),
      state_tracker(is_client ? "client_transport" : "server_transport",
                    GRPC_CHANNEL_READY),
      is_client(is_client) {
  printf("\n%d :: %s :: %s :: %p\n", __LINE__, __func__, __FILE__, this);
  base.vtable = get_vtable();
  gpr_ref_init(&refs, 1);
  grpc_slice_buffer_init(&read_buffer);
  grpc_slice_buffer_init(&outbuf);
  if (is_client) {
     //grpc_slice_buffer_add(&outbuf,grpc_slice_from_copied_string("Diff proc transport"));
     //grpc_diffproc_initiate_write(this);
  }
}

// DOTR
grpc_diffproc_transport::~grpc_diffproc_transport() {
  printf(" TRANSPORT DOTR ... \n");
  grpc_endpoint_destroy(ep);
  grpc_slice_buffer_destroy_internal(&outbuf);
  grpc_slice_buffer_destroy_internal(&read_buffer);
  // GRPC_ERROR_UNREF(closed_with_error);
  // gpr_free(peer_string);
}

// REF
void grpc_diffproc_transport::ref() {
  printf("ref_transport: %p \n", this);
  gpr_ref(&refs);
}

// UNREF
void grpc_diffproc_transport::unref() {
  printf("unref_transport: %p \n", this);
  if (!gpr_unref(&refs)) {
    return;
  }
  DIFFPROC_LOG(GPR_INFO, "really_destroy_transport %p", this);
  printf("really_destroy_transport %p \n", this);
  this->~grpc_diffproc_transport();
  gpr_free(this);
}

//Metadata to Buffer
void mdToBuffer(grpc_diffproc_stream* s, const grpc_metadata_batch* metadata,
                bool* markfilled, grpc_slice_buffer* outbuf, bool isInitial) {
  grpc_slice_buffer_reset_and_unref(outbuf);
  if (markfilled != nullptr) {
    *markfilled = true;
  }
  //const auto& fields = metadata->idx.named;
  //if (fields.status != nullptr) {
  //  grpc_slice_buffer_add(outbuf, grpc_slice_ref_internal(GRPC_MDVALUE(fields.path->md)));
  //}
  //if (fields.grpc_message != nullptr) {
  //  grpc_slice_buffer_add(
  //      outbuf, grpc_slice_ref_internal(GRPC_MDVALUE(fields.grpc_message->md)));
  //}
  //if (fields.path != nullptr) {
  //  grpc_slice_buffer_add(
  //      outbuf, grpc_slice_ref_internal(GRPC_MDVALUE(fields.grpc_message->md)));
  //}


 // if (grpc_slice_eq(GRPC_MDKEY(metadata->idx.named.path->md),GRPC_MDKEY(metadata->idx.named.path->md)))
 if (s->t->is_client && isInitial) { //Client INIT MD
    grpc_slice path_slice = GRPC_MDVALUE(metadata->idx.named.path->md);
    grpc_slice_buffer_add(outbuf, grpc_slice_ref_internal(path_slice));

  } else if (!s->t->is_client && !isInitial) { //Server trailing MD---  Need -1 for server streaming indicating EOP.. Status -0 Mandatory field
    //grpc_slice msg_slice = grpc_slice_from_static_string("-1");
    //grpc_slice status_hdr = GRPC_MDKEY(metadata->idx.named.grpc_status->md);
    grpc_slice status_slice = GRPC_MDVALUE(metadata->idx.named.grpc_status->md);
    //grpc_slice msg_slice = GRPC_MDVALUE(metadata->idx.named.grpc_status->md);
    //grpc_slice msg_slice = grpc_slice_from_static_string("grpc-message 0");
    grpc_slice_buffer_add(outbuf,grpc_core::UnmanagedMemorySlice("0"));  // Message -2
    //grpc_slice_buffer_add(outbuf, GRPC_MDSTR_GRPC_STATUS);
    //size_t estimated_len = GRPC_SLICE_LENGTH(status_hdr)+GRPC_SLICE_LENGTH(status_slice) + GRPC_SLICE_LENGTH(msg_slice);
    //grpc_core::UnmanagedMemorySlice status_msg_slice(estimated_len+1);
    //char* write_ptr = reinterpret_cast<char*> GRPC_SLICE_START_PTR(status_msg_slice);
   // memcpy(write_ptr + 0, GRPC_SLICE_START_PTR(msg_slice), GRPC_SLICE_LENGTH(msg_slice));
    //int offset = GRPC_SLICE_LENGTH(msg_slice) + 1;
    //memcpy(write_ptr + offset, GRPC_SLICE_START_PTR(status_hdr),GRPC_SLICE_LENGTH(status_hdr));
    //offset += GRPC_SLICE_LENGTH(status_hdr) + 1;
    //memcpy(write_ptr+ offset, GRPC_SLICE_START_PTR(status_slice),GRPC_SLICE_LENGTH(status_slice));
    //grpc_slice_buffer_add(outbuf, grpc_slice_ref_internal(status_msg_slice));
    grpc_slice_buffer_add(outbuf, grpc_slice_ref_internal(status_slice)); // Status -1 
    puts("Here");

    } 
      else if (s->t->is_client && !isInitial) { //Client trailing MD
      grpc_slice status_slice = grpc_slice_from_static_string("-1"); // message = -1 -- End of batch perations -1
      grpc_slice_buffer_add(outbuf, grpc_slice_ref_internal(status_slice)); // status 0, 12, 14 -2 
    } 
     else if (!s->t->is_client && isInitial) { //Server Init MD
    grpc_slice status_slice = grpc_slice_from_static_string("-1");
    grpc_slice_buffer_add(outbuf, grpc_slice_ref_internal(status_slice));
  }
}


//Buffer to Metatdata
grpc_metadata_batch bufferToMd(grpc_slice_buffer* slice_buffer, bool& filled,
                               grpc_diffproc_stream* s, bool isInitial) {
  grpc_metadata_batch md;
  grpc_metadata_batch_init(&md);
  // Client INIT MD -- Receive status from server if any
  if (s->t->is_client && isInitial) {  
    char* status_bytes = static_cast<char*>(gpr_malloc(slice_buffer->length + 1));
    grpc_linked_mdelem* path_md =
        static_cast<grpc_linked_mdelem*>(s->arena->Alloc(sizeof(*path_md)));
    size_t offset = 0;
    memcpy(status_bytes + offset, GRPC_SLICE_START_PTR(slice_buffer->slices[0]),
           GRPC_SLICE_LENGTH(slice_buffer->slices[0]));
    offset += GRPC_SLICE_LENGTH(slice_buffer->slices[0]);
    *(status_bytes + offset) = '\0';
    // grpc_slice msg_slice = grpc_slice_from_static_buffer(slice_buffer,
    // GRPC_SLICE_LENGTH(GRPC_MDSTR_GRPC_MESSAGE));
    path_md->md = grpc_mdelem_from_slices(
        GRPC_MDSTR_GRPC_MESSAGE, grpc_slice_from_static_string(status_bytes));
    GPR_ASSERT(grpc_metadata_batch_link_tail(&md, path_md) == GRPC_ERROR_NONE);
  } 


  // Client trailing MD -- Receive server status and msg if any
  else if (s->t->is_client && !isInitial) {  
    //read_action_locked(s->t);
    //char* msg_bytes = static_cast<char*>(
    //    gpr_malloc(slice_buffer->length + 1));
    //grpc_linked_mdelem* msg_md =
    //    static_cast<grpc_linked_mdelem*>(s->arena->Alloc(sizeof(*msg_md)));
    //size_t offset = 0;
    //memcpy(msg_bytes + offset, GRPC_SLICE_START_PTR(slice_buffer->slices[0]),
    //       GRPC_SLICE_LENGTH(slice_buffer->slices[0]));
    //offset += GRPC_SLICE_LENGTH(slice_buffer->slices[0]);
    //*(msg_bytes + offset) = '\0';
    //// grpc_slice msg_slice = grpc_slice_from_static_buffer(slice_buffer,
    //// GRPC_SLICE_LENGTH(GRPC_MDSTR_GRPC_MESSAGE));
    //msg_md->md = grpc_mdelem_from_slices(
    //    GRPC_MDSTR_GRPC_MESSAGE, grpc_slice_from_static_string(msg_bytes));
    //GPR_ASSERT(grpc_metadata_batch_link_tail(&md, msg_md) == GRPC_ERROR_NONE);

    // ****************** STATUS *******************
    read_action_locked(s->t);
    char* status_bytes = static_cast<char*>(gpr_malloc(slice_buffer->length + 1));
    grpc_linked_mdelem* status_md =
        static_cast<grpc_linked_mdelem*>(s->arena->Alloc(sizeof(*status_md)));
    size_t offset1 = 0;
    memcpy(status_bytes + offset1,
           GRPC_SLICE_START_PTR(slice_buffer->slices[0]),
           GRPC_SLICE_LENGTH(slice_buffer->slices[0]));
    offset1 += GRPC_SLICE_LENGTH(slice_buffer->slices[0]);
    *(status_bytes + offset1) = '\0';
    // grpc_slice msg_slice = grpc_slice_from_static_buffer(slice_buffer,
    // GRPC_SLICE_LENGTH(GRPC_MDSTR_GRPC_MESSAGE));
    status_md->md = grpc_mdelem_from_slices(
        GRPC_MDSTR_GRPC_STATUS, grpc_slice_from_static_string(status_bytes));
    GPR_ASSERT(grpc_metadata_batch_link_tail(&md, status_md) ==GRPC_ERROR_NONE);

  } 


  // Server trailing MD -- Receive client status if any
  else if (!s->t->is_client && !isInitial) {  
    read_action_locked(s->t);
    char* status_bytes = static_cast<char*>(gpr_malloc(slice_buffer->length + 1));
    grpc_linked_mdelem* status_md =
        static_cast<grpc_linked_mdelem*>(s->arena->Alloc(sizeof(*status_md)));
    size_t offset = 0;
    memcpy(status_bytes + offset, GRPC_SLICE_START_PTR(slice_buffer->slices[0]),
           GRPC_SLICE_LENGTH(slice_buffer->slices[0]));
    offset += GRPC_SLICE_LENGTH(slice_buffer->slices[0]);
    *(status_bytes + offset) = '\0';
    // grpc_slice msg_slice = grpc_slice_from_static_buffer(slice_buffer,
    // GRPC_SLICE_LENGTH(GRPC_MDSTR_GRPC_MESSAGE));
    status_md->md = grpc_mdelem_from_slices(
        GRPC_MDSTR_GRPC_STATUS, grpc_slice_from_static_string(status_bytes));
    GPR_ASSERT(grpc_metadata_batch_link_tail(&md, status_md) ==
               GRPC_ERROR_NONE);

  } 
  // Server Init MD -- Receives path and authority
  else if (!s->t->is_client && isInitial) {  
    char* path_bytes = static_cast<char*>(gpr_malloc(slice_buffer->length + 1));
    grpc_linked_mdelem* status_md =
        static_cast<grpc_linked_mdelem*>(s->arena->Alloc(sizeof(*status_md)));
    size_t offset = 0;
    memcpy(path_bytes + offset, GRPC_SLICE_START_PTR(slice_buffer->slices[0]),
           GRPC_SLICE_LENGTH(slice_buffer->slices[0]));
    offset += GRPC_SLICE_LENGTH(slice_buffer->slices[0]);
    *(path_bytes + offset) = '\0';
    // grpc_slice msg_slice = grpc_slice_from_static_buffer(slice_buffer,
    // GRPC_SLICE_LENGTH(GRPC_MDSTR_GRPC_MESSAGE));
    status_md->md = grpc_mdelem_from_slices(
        GRPC_MDSTR_PATH, grpc_slice_from_static_string(path_bytes));
    GPR_ASSERT(grpc_metadata_batch_link_tail(&md, status_md) ==GRPC_ERROR_NONE);

    //Authority
    grpc_linked_mdelem* auth_md = static_cast<grpc_linked_mdelem*>(s->arena->Alloc(sizeof(*auth_md)));
    auth_md->md = grpc_mdelem_from_slices(g_fake_auth_key, g_fake_auth_value);
    GPR_ASSERT(grpc_metadata_batch_link_tail(&md, auth_md) == GRPC_ERROR_NONE);
  }
  filled = true;
  return md;
}








//  //grpc_slice_sub(path_slice, offset + 1, path_length);
//  if (!s->t->is_client && !isInitial) {
//
//  } else {
//    char* payload_bytes =
//        static_cast<char*>(gpr_malloc(slice_buffer->length + 1));
//    size_t offset = 0;
//     for (size_t i = 0; i < slice_buffer->count; ++i) {
//      memcpy(payload_bytes + offset,
//             GRPC_SLICE_START_PTR(slice_buffer->slices[i]),
//             GRPC_SLICE_LENGTH(slice_buffer->slices[i]));
//      offset += GRPC_SLICE_LENGTH(slice_buffer->slices[i]);
//    }
//     grpc_linked_mdelem* payload_md =
//        static_cast<grpc_linked_mdelem*>(s->arena->Alloc(sizeof(*payload_md)));
//  }
//
//  //size_t offset = 0;
//  //for (size_t i = 0; i < slice_buffer->count; ++i) {
//  ////  if (!GRPC_SLICE_IS_EMPTY(slice_buffer->slices[i])) {
//  ////    char* payload_bytes =
//  ////        static_cast<char*>(gpr_malloc(slice_buffer->length + 1));
//  ////    grpc_slice_eq(GRPC_MDKEY(metadata->idx.named.path->md),
//  ////                  GRPC_MDKEY(metadata->idx.named.path->md));
//  ////  }
//  ////}
//
//
//
//  //  memcpy(payload_bytes + offset,
//  //         GRPC_SLICE_START_PTR(slice_buffer->slices[i]),
//  //         GRPC_SLICE_LENGTH(slice_buffer->slices[i]));
//  //  offset += GRPC_SLICE_LENGTH(slice_buffer->slices[i]);
//  //}
//  //*(payload_bytes + offset) = '\0';
//  //filled = true;
//  //return grpc_slice_from_static_string(payload_bytes);
//}

grpc_error* fill_in_metadata(grpc_diffproc_stream* s,
                             const grpc_metadata_batch* metadata,
                             uint32_t flags, grpc_metadata_batch* out_md,
                             uint32_t* outflags, bool* markfilled) {
  // if (GRPC_TRACE_FLAG_ENABLED(grpc_diffproc_trace)) {
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

static void fail_helper_locked(grpc_diffproc_stream* s, grpc_error* error) {
  printf( "op_state_machine %p fail_helper \n", s);
  DIFFPROC_LOG(GPR_INFO, "op_state_machine %p fail_helper", s);

  if (!s->trailing_md_sent) {
    // Send trailing md to the other side indicating cancellation
    s->trailing_md_sent = true;
    grpc_metadata_batch fake_md;
    grpc_metadata_batch_init(&fake_md);
    grpc_metadata_batch* dest =  &s->write_buffer_trailing_md;
    bool* destfilled = &s->write_buffer_trailing_md_filled;
    fill_in_metadata(s, &fake_md, 0, dest, nullptr, destfilled);
    grpc_metadata_batch_destroy(&fake_md);

    if (s->t->ep) {
      maybe_process_ops_locked(s, error);
      if(s->write_buffer_cancel_error == GRPC_ERROR_NONE) {
      s->write_buffer_cancel_error = GRPC_ERROR_REF(error);
      }
    }
  }




  if (s->recv_initial_md_op) {
    grpc_error* err;
    if (!s->t->is_client) {
      // If this is a server, provide initial metadata with a path and authority
      // since it expects that as well as no error yet
      read_action_locked(s->t);
      s->to_read_initial_md_filled = false;
      grpc_metadata_batch fake_md =
          bufferToMd(&s->t->read_buffer, s->to_read_initial_md_filled, s, 1);
      fill_in_metadata(
          s, &fake_md, 0,
          s->recv_initial_md_op->payload->recv_initial_metadata
              .recv_initial_metadata,
          s->recv_initial_md_op->payload->recv_initial_metadata.recv_flags,
          nullptr);
      s->to_read_trailing_md_filled = false;
      grpc_metadata_batch_destroy(&fake_md);
      err = GRPC_ERROR_NONE;
    } else {
      err = GRPC_ERROR_REF(error);
    }
    if (s->recv_initial_md_op->payload->recv_initial_metadata
            .trailing_metadata_available != nullptr) {
      // Set to true unconditionally, because we're failing the call, so even
      // if we haven't actually seen the send_trailing_metadata op from the
      // other side, we're going to return trailing metadata anyway.
      *s->recv_initial_md_op->payload->recv_initial_metadata
           .trailing_metadata_available = true;
    }
    printf("fail_helper %p scheduling initial-metadata-ready %p %p", s,
               error, err);
    grpc_core::ExecCtx::Run(
        DEBUG_LOCATION,
        s->recv_initial_md_op->payload->recv_initial_metadata
            .recv_initial_metadata_ready,
        err);
    // Last use of err so no need to REF and then UNREF it

    complete_if_batch_end_locked(
        s, error, s->recv_initial_md_op,
        "fail_helper scheduling recv-initial-metadata-on-complete");
    s->recv_initial_md_op = nullptr;
  }


    if (s->recv_message_op) {
    DIFFPROC_LOG(GPR_INFO, "fail_helper %p scheduling message-ready %p", s,
               error);
    grpc_core::ExecCtx::Run(
        DEBUG_LOCATION,
        s->recv_message_op->payload->recv_message.recv_message_ready,
        GRPC_ERROR_REF(error));
    complete_if_batch_end_locked(
        s, error, s->recv_message_op,
        "fail_helper scheduling recv-message-on-complete");
    s->recv_message_op = nullptr;
  }

    if (s->send_message_op) {
    s->send_message_op->payload->send_message.send_message.reset();
    complete_if_batch_end_locked(
        s, error, s->send_message_op,
        "fail_helper scheduling send-message-on-complete");
    s->send_message_op = nullptr;
    }

    if (s->send_trailing_md_op) {
      complete_if_batch_end_locked(
          s, error, s->send_trailing_md_op,
          "fail_helper scheduling send-trailng-md-on-complete");
      s->send_trailing_md_op = nullptr;
    }

    if (s->recv_trailing_md_op) {
      DIFFPROC_LOG(GPR_INFO,
                 "fail_helper %p scheduling trailing-metadata-ready %p", s,
                 error);
      grpc_core::ExecCtx::Run(
          DEBUG_LOCATION,
          s->recv_trailing_md_op->payload->recv_trailing_metadata
              .recv_trailing_metadata_ready,
          GRPC_ERROR_REF(error));
      DIFFPROC_LOG(GPR_INFO,
                 "fail_helper %p scheduling trailing-md-on-complete %p", s,
                 error);
      complete_if_batch_end_locked(
          s, error, s->recv_trailing_md_op,
          "fail_helper scheduling recv-trailing-metadata-on-complete");
      s->recv_trailing_md_op = nullptr;
    }
    close_stream_locked(s->t, s, 1, 1, error);
    GRPC_ERROR_UNREF(error);
}

void op_state_machine_locked(grpc_diffproc_stream* s, grpc_error* error) {
  // This function gets called when we have contents in the unprocessed reads
  // Get what we want based on our ops wanted
  // Schedule our appropriate closures
  // and then return to ops_needed state if still needed

    grpc_error* new_err = GRPC_ERROR_NONE;

  bool needs_close = false;
  if (s->cancel_self_error != GRPC_ERROR_NONE) {
    fail_helper_locked(s, GRPC_ERROR_REF(s->cancel_self_error));
    goto done;
  } else if (error != GRPC_ERROR_NONE) {
    fail_helper_locked(s, GRPC_ERROR_REF(error));
    goto done;
  }

  if (s->send_message_op && s->t->ep) {
    printf("************** SEND MSG ********************: %s \n", s->t->is_client ? "CLT" : "SRV");
    //If enpoint is open.. we can send our message
    if (s->t->ep) {
      message_transfer_locked(s->t, s);
     // maybe_process_ops_locked(s, GRPC_ERROR_NONE);
    } else if (!s->t->is_client && s->trailing_md_sent) {
      // A server send will never be matched if the server already sent status
      s->send_message_op->payload->send_message.send_message.reset();
      complete_if_batch_end_locked(
          s, GRPC_ERROR_NONE, s->send_message_op,
          "op_state_machine scheduling send-message-on-complete");
      s->send_message_op = nullptr;
    }
  }
  // Pause a send trailing metadata if there is still an outstanding
  // send message unless we know that the send message will never get
  // matched to a receive. This happens on the client if the server has
  // already sent status or on the server if the client has requested
  // status
  // If send trailing md and 
  // If not send msg op (OR) (If client & already recvd trail md or trailing md is already filled) (OR)
  // If this is Server and client already recvd trailmd (or) client recv trailmd filled (or) client recv trailmdop
  if (s->send_trailing_md_op &&
        (!s->send_message_op ||
          (s->t->is_client &&  (s->trailing_md_recvd || s->to_read_trailing_md_filled)) ||
          (!s->t->is_client && s->t->ep && (s->trailing_md_sent))
        )
    ) {
    printf("************** SEND TRAIL MD ********************: %s \n",s->t->is_client ? "CLT" : "SRV");
    grpc_metadata_batch* dest = &s->write_buffer_trailing_md;
    bool* destfilled = &s->write_buffer_trailing_md_filled;
    if (*destfilled || s->trailing_md_sent) {
      // The buffer is already in use; that's an error!
      printf( "Extra trailing metadata %p \n", s);
      new_err = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Extra trailing metadata");
      fail_helper_locked(s, GRPC_ERROR_REF(new_err));
      goto done;
    } else {
      //If enpoint is open send trailing metadata through endpoint..
      if (s->t->ep) {
        s->sent_msg = true;
        fill_in_metadata(s,
                         s->send_trailing_md_op->payload->send_trailing_metadata
                             .send_trailing_metadata,
                         0, dest, nullptr, destfilled);
        mdToBuffer(s, dest, destfilled, &s->t->outbuf, 0);
        grpc_diffproc_initiate_write(s->t);
      }
      s->trailing_md_sent = true;
      // Server && trailing recvd && recv_op nt null
      if (!s->t->is_client && s->trailing_md_recvd && s->recv_trailing_md_op) { 
       printf("op_state_machine %p scheduling trailing-metadata-ready \n", s);
        grpc_core::ExecCtx::Run(
            DEBUG_LOCATION,
            s->recv_trailing_md_op->payload->recv_trailing_metadata
                .recv_trailing_metadata_ready,
            GRPC_ERROR_NONE);
        printf("op_state_machine %p scheduling trailing-md-on-complete \n", s);
        grpc_core::ExecCtx::Run(DEBUG_LOCATION, s->recv_trailing_md_op->on_complete,GRPC_ERROR_NONE);
        s->recv_trailing_md_op = nullptr;
        needs_close = true;
      }
    }
    //this was other-- changed to s
    //maybe_process_ops_locked(s, GRPC_ERROR_NONE);
    complete_if_batch_end_locked(
        s, GRPC_ERROR_NONE, s->send_trailing_md_op,
        "op_state_machine scheduling send-trailing-metadata-on-complete");
    s->send_trailing_md_op = nullptr;
  }

  //Receive INIT MD
  if (s->recv_initial_md_op) {
    printf("************** RECV INIT MD ********************: %s \n", s->t->is_client ? "CLT" : "SRV");
    if (s->initial_md_recvd) {
      new_err =
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Already recvd initial md");
      printf(
          "op_state_machine %p scheduling on_complete errors for already "
          "recvd initial md %p \n",
          s, new_err);
      fail_helper_locked(s, GRPC_ERROR_REF(new_err));
      goto done;
    }
    grpc_metadata_batch fake_md;
    grpc_metadata_batch_init(&fake_md);
    s->initial_md_recvd = true;
    if (s->t->is_client) {
      s->to_read_initial_md_filled = true;
    }
    if (!s->t->is_client) { // Server Recv INIT MD
      read_action_locked(s->t);
      fake_md = bufferToMd(&s->t->read_buffer, s->to_read_initial_md_filled, s, 1);
    }
    if (s->to_read_initial_md_filled) {
      s->initial_md_recvd = true;
      new_err = fill_in_metadata(
          s, &fake_md, s->to_read_initial_md_flags,
          s->recv_initial_md_op->payload->recv_initial_metadata
              .recv_initial_metadata,
          s->recv_initial_md_op->payload->recv_initial_metadata.recv_flags,
          nullptr);
      s->recv_initial_md_op->payload->recv_initial_metadata
          .recv_initial_metadata->deadline = s->deadline;
      //if (s->recv_initial_md_op->payload->recv_initial_metadata.trailing_metadata_available != nullptr) {
      //  *s->recv_initial_md_op->payload->recv_initial_metadata.trailing_metadata_available =
      //      (other != nullptr && other->send_trailing_md_op != nullptr);
      //}
      grpc_metadata_batch_destroy(&fake_md);
      s->to_read_initial_md_filled = false;
      printf("op_state_machine %p scheduling initial-metadata-ready %p \n", s,
                 new_err);
      grpc_core::ExecCtx::Run(
          DEBUG_LOCATION,
          s->recv_initial_md_op->payload->recv_initial_metadata
              .recv_initial_metadata_ready,
          GRPC_ERROR_REF(new_err));
      complete_if_batch_end_locked(
          s, new_err, s->recv_initial_md_op,
          "op_state_machine scheduling recv-initial-metadata-on-complete");
      s->recv_initial_md_op = nullptr;

      if (new_err != GRPC_ERROR_NONE) {
        printf("op_state_machine %p scheduling on_complete errors2 %p \n", s, new_err);
        fail_helper_locked(s, GRPC_ERROR_REF(new_err));
        goto done;
      }
    }
  }


  //Recv Message
  if ((s->recv_message_op && s->t->is_client && s->sent_msg) || (s->recv_message_op && !s->t->is_client)) {
    printf("************** RECV MSG ********: %s \n", s->t->is_client ? "CLT" : "SRV");
    if (s->t->ep) {
      //Read from buffer then copy it to recv message
      read_action_locked(s->t);
      message_read_locked(s->t, s);
      //This was other cahnged to S
      //maybe_process_ops_locked(s, GRPC_ERROR_NONE);
    }
  }

  if (s->recv_trailing_md_op) {
    printf("************** RECV TRAIL MD ********************: %s \n", s->t->is_client ? "CLT" : "SRV");
    if (s->trailing_md_recvd) {
      //Already recvd trail md
      new_err =
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Already recvd trailing md");
      printf(
          "op_state_machine %p scheduling on_complete errors for already "
          "recvd trailing md %p \n",
          s, new_err);
      fail_helper_locked(s, GRPC_ERROR_REF(new_err));
      goto done;
    }

    //In trailing MD..So message is already done.. So close the recv msg
    if (s->recv_message_op != nullptr) {
      // This message needs to be wrapped up because it will never be
      // satisfied
      *s->recv_message_op->payload->recv_message.recv_message = nullptr;
      printf("op_state_machine %p scheduling message-ready", s);
      grpc_core::ExecCtx::Run(
          DEBUG_LOCATION,
          s->recv_message_op->payload->recv_message.recv_message_ready,
          GRPC_ERROR_NONE);
      complete_if_batch_end_locked(
          s, new_err, s->recv_message_op,
          "op_state_machine scheduling recv-message-on-complete");
      s->recv_message_op = nullptr;
    }

    if ((s->trailing_md_sent || s->t->is_client) && s->send_message_op) {
      // Nothing further will try to receive from this stream, so finish off
      // any outstanding send_message op
      s->send_message_op->payload->send_message.send_message.reset();
      complete_if_batch_end_locked(
          s, new_err, s->send_message_op,
          "op_state_machine scheduling send-message-on-complete");
      s->send_message_op = nullptr;
    }

    //No trailing MD has been received yet.. Now its time to receive -- MAIN
    if (s->recv_trailing_md_op != nullptr) {
      if (s->t->is_client) { //Client Recv trail
        // We wanted trailing metadata and we got it
        s->trailing_md_recvd = true;
        grpc_metadata_batch_init(&s->to_read_trailing_md);
        s->to_read_trailing_md =
            bufferToMd(&s->t->read_buffer, s->to_read_trailing_md_filled, s, 0);
        /*grpc_linked_mdelem* status_md = static_cast<grpc_linked_mdelem*>(
            s->arena->Alloc(sizeof(*status_md)));
        status_md->md = grpc_mdelem_from_slices(
            g_fake_status_key,
            bufferToMd(&s->t->read_buffer, s->to_read_trailing_md_filled));
        GPR_ASSERT(grpc_metadata_batch_link_tail(&s->to_read_trailing_md,
                                                 status_md) == GRPC_ERROR_NONE);*/
        new_err = fill_in_metadata(
            s, &s->to_read_trailing_md, 0,
            s->recv_trailing_md_op->payload->recv_trailing_metadata
                .recv_trailing_metadata,
            nullptr, nullptr);
        grpc_metadata_batch_clear(&s->to_read_trailing_md);
        s->to_read_trailing_md_filled = false;
      }

      // We should schedule the recv_trailing_md_op completion if
      // 1. this stream is the client-side
      // 2. this stream is the server-side AND has already sent its trailing md
      //    (If the server hasn't already sent its trailing md, it doesn't have
      //     a final status, so don't mark this op complete)
      if (s->t->is_client || (!s->t->is_client && s->trailing_md_sent)) {
        printf("op_state_machine %p scheduling trailing-md-on-complete %p \n", s, new_err);
        grpc_core::ExecCtx::Run(
            DEBUG_LOCATION,
            s->recv_trailing_md_op->payload->recv_trailing_metadata
                .recv_trailing_metadata_ready,
            GRPC_ERROR_REF(new_err));
        grpc_core::ExecCtx::Run(DEBUG_LOCATION,
                                s->recv_trailing_md_op->on_complete,
                                GRPC_ERROR_REF(new_err));
        s->recv_trailing_md_op = nullptr;
        s->t->processed = 1;
        //needs_close = s->trailing_md_sent;
      } else {
       printf("op_state_machine %p server needs to delay handling trailing-md-on-complete %p \n",s, new_err);
      }
    } else {
      printf("op_state_machine %p has trailing md but not yet waiting for it", s);
    }
  }


  if (s->trailing_md_recvd && s->recv_message_op) {
    printf("************** RECVD TRAIL & NO CLOSE RECV MSG ******************** : ""%s \n",s->t->is_client ? "CLT" : "SRV");
    // No further message will come on this stream, so finish off the
    // recv_message_op
    printf( "op_state_machine %p scheduling message-ready \n", s);
    *s->recv_message_op->payload->recv_message.recv_message = nullptr;
    grpc_core::ExecCtx::Run(
        DEBUG_LOCATION,
        s->recv_message_op->payload->recv_message.recv_message_ready,
        GRPC_ERROR_NONE);
    complete_if_batch_end_locked(
        s, new_err, s->recv_message_op,
        "op_state_machine scheduling recv-message-on-complete");
    s->recv_message_op = nullptr;
  }

  if (s->trailing_md_recvd && (s->trailing_md_sent || s->t->is_client) && s->send_message_op) {
    printf("************** RECVD TRAIL & SENT TRAIL & NO CLOSE SEND MSG ""******************** : ""%s \n",s->t->is_client ? "CLT" : "SRV");
    // Nothing further will try to receive from this stream, so finish off
    // any outstanding send_message op
    s->send_message_op->payload->send_message.send_message.reset();
    complete_if_batch_end_locked(
        s, new_err, s->send_message_op,
        "op_state_machine scheduling send-message-on-complete");
    s->send_message_op = nullptr;
  }

  if (s->send_message_op || s->send_trailing_md_op || s->recv_initial_md_op ||
      s->recv_message_op || s->recv_trailing_md_op) {
    // Didn't get the item we wanted so we still need to get
    // rescheduled
    printf( "op_state_machine %p still needs closure %p %p %p %p %p \n", s,
        s->send_message_op, s->send_trailing_md_op, s->recv_initial_md_op,
        s->recv_message_op, s->recv_trailing_md_op);
    s->ops_toBeSent = true;
  }




done:
  if (needs_close) {
    puts(" *************** Cancel Stream called **************");
    cancel_stream_locked(s, new_err);
  }
}

void maybe_process_ops_locked(grpc_diffproc_stream* s, grpc_error* error) {
  //Check if endpoint is open and error -- In inproc this S is other for INIT MD
  if (s->t->ep && (error != GRPC_ERROR_NONE || s->ops_toBeSent)) {
    s->ops_toBeSent = false;
    op_state_machine_locked(s, error);
  }
}

void grpc_diffproc_stream_map_add(grpc_diffproc_transport* t, uint32_t id,
                                  void* value) {
  printf("Adding stream - ID [%d] from transport [%p] to stream_map.. \n", id,
         t, static_cast<grpc_diffproc_stream*>(value));
  if (t->stream_map.find(id) == t->stream_map.end())
    t->stream_map[id] = static_cast<grpc_diffproc_stream*>(value);
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

// STREAM COTR
grpc_diffproc_stream::grpc_diffproc_stream(grpc_diffproc_transport* t,
                                           grpc_stream_refcount* refcount,
                                           const void* server_data,
                                           grpc_core::Arena* arena)
    : t(t), refcount(refcount), arena(arena), reffer(this) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  grpc_metadata_batch_init(&to_read_initial_md);
  grpc_metadata_batch_init(&to_read_trailing_md);
  grpc_metadata_batch_init(&write_buffer_initial_md);
  grpc_metadata_batch_init(&write_buffer_trailing_md);
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
    // grpc_diffproc_stream_map_add(t, id, this);
  }
}

// STREAM DOTR
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
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, closure_at_destroy,
                            GRPC_ERROR_NONE);
  }
}

int init_stream(grpc_transport* gt, grpc_stream* gs,
                grpc_stream_refcount* refcount, const void* server_data,
                grpc_core::Arena* arena) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  GPR_TIMER_SCOPE("init_stream", 0);
  grpc_diffproc_transport* t = reinterpret_cast<grpc_diffproc_transport*>(gt);
  new (gs) grpc_diffproc_stream(t, refcount, server_data, arena);
  return 0;
}
void set_pollset(grpc_transport* /*gt*/, grpc_stream* /*gs*/, grpc_pollset*
                 /*pollset*/) {
  // Nothing to do here
}

void set_pollset_set(grpc_transport* /*gt*/, grpc_stream* /*gs*/,
                     grpc_pollset_set* /*pollset_set*/) {
  // Nothing to do here
}

grpc_endpoint* get_endpoint(grpc_transport* t) {
  return (reinterpret_cast<grpc_diffproc_transport*>(t))->ep;
}

// Close Transport -- Shutdown endpoint called
static void close_transport_locked(grpc_diffproc_transport* t,
                                   grpc_error* error) {
  printf("close_transport %p %d \n", t, t->is_closed);
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

void destroy_stream(grpc_transport* /*gt*/, grpc_stream* gs,
                    grpc_closure* then_schedule_closure) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  grpc_diffproc_stream* s = reinterpret_cast<grpc_diffproc_stream*>(gs);
  s->closure_at_destroy = then_schedule_closure;
  s->~grpc_diffproc_stream();
}

void destroy_transport(grpc_transport* gt) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  grpc_diffproc_transport* t = reinterpret_cast<grpc_diffproc_transport*>(gt);
  close_transport_locked(
      t, GRPC_ERROR_CREATE_FROM_STATIC_STRING("destroying transport"));
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

static void log_metadata(const grpc_metadata_batch* md_batch, bool is_client,
                         bool is_initial) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  for (grpc_linked_mdelem* md = md_batch->list.head; md != nullptr;
       md = md->next) {
    char* key = grpc_slice_to_c_string(GRPC_MDKEY(md->md));
    char* value = grpc_slice_to_c_string(GRPC_MDVALUE(md->md));
    printf("DIFFPROC :%s:%s: %s: %s \n", is_initial ? "HDR" : "TRL",
           is_client ? "CLI" : "SVR", key, value);
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

bool cancel_stream_locked(grpc_diffproc_stream* stream, grpc_error* error) {
  bool ret = false;  // was the cancel accepted
  printf("cancel_stream %p with %s \n", stream, grpc_error_string(error));
  if (stream->cancel_self_error == GRPC_ERROR_NONE) {
    ret = true;
    stream->cancel_self_error = GRPC_ERROR_REF(error);
    // Send trailing md to other side.
    maybe_process_ops_locked(stream, stream->cancel_self_error);
    stream->trailing_md_sent = true;
    grpc_metadata_batch cancel_md;
    grpc_metadata_batch_init(&cancel_md);

    // if we are a server and already received trailing md but
    // couldn't complete that because we hadn't yet sent out trailing
    // md, now's the chance
    if (!stream->t->is_client && stream->trailing_md_recvd &&
        stream->recv_trailing_md_op) {
      grpc_core::ExecCtx::Run(
          DEBUG_LOCATION,
          stream->recv_trailing_md_op->payload->recv_trailing_metadata
              .recv_trailing_metadata_ready,
          GRPC_ERROR_REF(stream->cancel_self_error));
      complete_if_batch_end_locked(
          stream, stream->cancel_self_error, stream->recv_trailing_md_op,
          "cancel_stream scheduling trailing-md-on-complete");
      stream->recv_trailing_md_op = nullptr;
    }
  }
  close_stream_locked(stream->t, stream, 1, 1, error);
  return ret;
}

// Write Buffer
void message_transfer_locked(grpc_diffproc_transport* t,
                             grpc_diffproc_stream* sender) {
  size_t remaining = sender->send_message_op->payload->send_message.send_message->length();
  printf("************ Remaining here in msg transfer locked *********** %d \n", remaining);
  grpc_slice_buffer_init(&t->outbuf);
  printf("Remainign  :%d",remaining);
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
    complete_if_batch_end_locked(
        sender, GRPC_ERROR_NONE, sender->send_message_op,
        "message_transfer scheduling sender on_complete");
    sender->send_message_op = nullptr;
}


void checkForEndMessage(grpc_slice_buffer buf) {
  char* bs = static_cast<char*> (gpr_malloc(sizeof(buf.length + 1)));
  size_t offset = 0;
  for (size_t i = 0; i < buf.count; ++i) {
    memcpy(bs + offset, GRPC_SLICE_START_PTR(buf.slices[i]), GRPC_SLICE_LENGTH(buf.slices[i]));
    offset += GRPC_SLICE_LENGTH(buf.slices[i]);
  }
  printf("First char in Message : %c", bs[0]);
}

// Read Buffer
void message_read_locked(grpc_diffproc_transport* t,
                         grpc_diffproc_stream* receiver) {
 // checkForEndMessage(t->read_buffer);
  receiver->recv_stream.Init(&t->read_buffer, 0);
  receiver->recv_message_op->payload->recv_message.recv_message->reset(
      receiver->recv_stream.get());

  // printf("message_transfer_locked %p scheduling message-ready", receiver);
  grpc_core::ExecCtx::Run(
      DEBUG_LOCATION,
      receiver->recv_message_op->payload->recv_message.recv_message_ready,
      GRPC_ERROR_NONE);
  complete_if_batch_end_locked(
      receiver, GRPC_ERROR_NONE, receiver->recv_message_op,
      "message_transfer scheduling receiver on_complete");
  receiver->recv_message_op = nullptr;
}

//static void null_then_sched_closure(grpc_closure** closure) {
//  grpc_closure* c = *closure;
//  *closure = nullptr;
//  grpc_core::ExecCtx::Run(DEBUG_LOCATION, c, GRPC_ERROR_NONE);
//}

//void grpc_chttp2_complete_closure_step(grpc_diffproc_transport* t,
//                                       grpc_diffproc_stream* /*s*/,
//                                       grpc_closure** pclosure,
//                                       grpc_error* error, const char* desc) {
//  grpc_closure* closure = *pclosure;
//  *pclosure = nullptr;
//  if (closure == nullptr) {
//    GRPC_ERROR_UNREF(error);
//    return;
//  }
//  if (error != GRPC_ERROR_NONE) {
//    if (closure->error_data.error == GRPC_ERROR_NONE) {
//      closure->error_data.error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
//          "Error in DIFFPROC transport completing operation");
//      closure->error_data.error = grpc_error_set_str(
//          closure->error_data.error, GRPC_ERROR_STR_TARGET_ADDRESS,
//          grpc_slice_from_copied_string(t->peer_string));
//    }
//    closure->error_data.error =
//        grpc_error_add_child(closure->error_data.error, error);
//  }
//  // if (t->write_state == GRPC_CHTTP2_WRITE_STATE_IDLE) {
//  // Using GRPC_CLOSURE_SCHED instead of GRPC_CLOSURE_RUN to avoid running
//  // closures earlier than when it is safe to do so.
//  grpc_core::ExecCtx::Run(DEBUG_LOCATION, closure, closure->error_data.error);
//  //}
//}

//void grpc_diffproc_fail_pending_writes(grpc_diffproc_transport* t,
//                                       grpc_diffproc_stream* s,
//                                       grpc_error* error) {
//  s->send_initial_metadata = nullptr;
//  grpc_chttp2_complete_closure_step(t, s, &s->send_initial_metadata_finished,
//                                    GRPC_ERROR_REF(error),
//                                    "send_initial_metadata_finished");
//
//  s->send_trailing_metadata = nullptr;
//  grpc_chttp2_complete_closure_step(t, s, &s->send_trailing_metadata_finished,
//                                    GRPC_ERROR_REF(error),
//                                    "send_trailing_metadata_finished");
//}

//void grpc_diffproc_maybe_complete_recv_message(grpc_diffproc_transport* /*t*/,
//                                               grpc_diffproc_stream* s) {
//  grpc_error* err = GRPC_ERROR_NONE;
//  if (s->recv_message_ready != nullptr) {
//    if (err == GRPC_ERROR_NONE) {
//      null_then_sched_closure(&s->recv_message_ready);
//    }
//    GRPC_ERROR_UNREF(err);
//  }
//}

//void grpc_diffproc_maybe_complete_recv_trailing_metadata(
//    grpc_diffproc_transport* t, grpc_diffproc_stream* s) {
//  grpc_diffproc_maybe_complete_recv_message(t, s);
//  if (s->recv_trailing_metadata_finished != nullptr) {
//    if (s->recv_trailing_metadata_finished != nullptr) {
//      null_then_sched_closure(&s->recv_trailing_metadata_finished);
//    }
//  }
//}

// Close Stream
void close_stream_locked(grpc_diffproc_transport* t, grpc_diffproc_stream* s,
                         int close_reads, int close_writes, grpc_error* error) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  //if (s->read_closed && s->write_closed) {
  //  /* already closed */
  //  // grpc_chttp2_maybe_complete_recv_trailing_metadata(t, s);
  //  GRPC_ERROR_UNREF(error);
  //  return;
  //}
  //bool closed_read = false;
  //bool became_closed = false;
  //if (close_reads && !s->read_closed) {
  //  s->read_closed_error = GRPC_ERROR_REF(error);
  //  s->read_closed = true;
  //  closed_read = true;
  //}
  //if (close_writes && !s->write_closed) {
  //  s->write_closed_error = GRPC_ERROR_REF(error);
  //  s->write_closed = true;
  //  grpc_diffproc_fail_pending_writes(t, s, GRPC_ERROR_REF(error));
  //}
  //if (s->read_closed && s->write_closed) {
  //  if (!s->closed) {
  //    //  // Release the metadata that we would have written out

  //    if (s->listed) {
  //      grpc_diffproc_stream* p = s->stream_list_prev;
  //      grpc_diffproc_stream* n = s->stream_list_next;
  //      if (p != nullptr) {
  //        p->stream_list_next = n;
  //      } else {
  //        s->t->stream_list = n;
  //      }
  //      if (n != nullptr) {
  //        n->stream_list_prev = p;
  //      }
  //      s->listed = false;
  //      // grpc_diffproc_stream_unref(s,"close_stream:list");
  //    }

  //    s->closed = true;
  //  }
  //  became_closed = true;
  //}
  //if (closed_read) {
  //  if (s->recv_initial_metadata_ready != nullptr) {
  //    null_then_sched_closure(&s->recv_initial_metadata_ready);
  //  }
  //}
  //if (became_closed) {
  //  grpc_diffproc_maybe_complete_recv_trailing_metadata(t, s);
  //  grpc_diffproc_stream_unref(s, "diffproc");
  //}

  if (!s->closed) {
    // Release the metadata that we would have written out
    grpc_metadata_batch_destroy(&s->write_buffer_initial_md);
    grpc_metadata_batch_destroy(&s->write_buffer_trailing_md);
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
    }
    s->closed = true;
    grpc_diffproc_stream_unref(s, "close_stream");
  }
  GRPC_ERROR_UNREF(error);
}


// Perform Stream OP
static void perform_stream_op(grpc_transport* gt, grpc_stream* gs,
                              grpc_transport_stream_op_batch* op) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  grpc_diffproc_stream* st = reinterpret_cast<grpc_diffproc_stream*>(gs);
  gpr_mu* mu = &st->t->mu;
  gpr_mu_unlock(mu);

  //Logging init MD and Trail MD
  if (op->send_initial_metadata) {
    log_metadata(op->payload->send_initial_metadata.send_initial_metadata,
                 st->t->is_client, true);
  }
  if (op->send_trailing_metadata) {
    log_metadata(op->payload->send_trailing_metadata.send_trailing_metadata,
                 st->t->is_client, false);
  }

  grpc_error* error = GRPC_ERROR_NONE;
  grpc_closure* on_complete = op->on_complete;

  //If on_complete is null... Do_nothing
  if (on_complete == nullptr) {
    on_complete = GRPC_CLOSURE_INIT(&op->handler_private.closure, do_nothing,
                                    nullptr, grpc_schedule_on_exec_ctx);
  }

  //Check if cancel stream.. else log current batch operation..
  if (op->cancel_stream) {
    // Call cancel_stream_locked without ref'ing the cancel_error because
    // this function is responsible to make sure that that field gets unref'ed
    cancel_stream_locked(st, op->payload->cancel_stream.cancel_error);
    // this op can complete without an error
  } else if (st->cancel_self_error != GRPC_ERROR_NONE) {
    // already self-canceled so still give it an error
    error = GRPC_ERROR_REF(st->cancel_self_error);
  } else {
    printf( "perform_stream_op %p %s%s%s%s%s%s%s \n", st,
               st->t->is_client ? "client" : "server",
               op->send_initial_metadata ? " send_initial_metadata" : "",
               op->send_message ? " send_message" : "",
               op->send_trailing_metadata ? " send_trailing_metadata" : "",
               op->recv_initial_metadata ? " recv_initial_metadata" : "",
               op->recv_message ? " recv_message" : "",
               op->recv_trailing_metadata ? " recv_trailing_metadata" : "");
  }

   if (error == GRPC_ERROR_NONE && (op->send_initial_metadata || op->send_trailing_metadata)) {
    if (op->send_initial_metadata)
      printf("************** SEND INIT MD ********************: %s \n",
             st->t->is_client ? "CLT" : "SRV");
    if (op->send_trailing_metadata)
      printf("************** SEND TRAIL MD ********************: %s \n",
             st->t->is_client ? "CLT" : "SRV");
    if (st->t->is_closed) {
      error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Endpoint already shutdown");
    }
    // Send INIT MD
    if (error == GRPC_ERROR_NONE && op->send_initial_metadata) {
      grpc_metadata_batch* dest = &st->write_buffer_initial_md;
      uint32_t* destflags = &st->write_buffer_initial_md_flags;
      bool* destfilled = &st->write_buffer_initial_md_filled;

      // Either if destfilled or initial MD already sent..
      if (*destfilled || st->initial_md_sent) {
        // The buffer is already in use; that's an error!
        printf("Extra initial metadata %p \n", st);
        error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Extra initial metadata");
      } else {
        // This fills write md with init MD
        fill_in_metadata(
            st, op->payload->send_initial_metadata.send_initial_metadata,
            op->payload->send_initial_metadata.send_initial_metadata_flags,
            dest, destflags, destfilled);
        // This is client.. calculate deadline
        if (st->t->is_client) {
          grpc_millis* dl = &st->write_buffer_deadline;
          *dl = GPR_MIN(*dl, op->payload->send_initial_metadata.send_initial_metadata->deadline);
          mdToBuffer(st, dest, destfilled, &st->t->outbuf, 1);
          grpc_diffproc_initiate_write(st->t);
          st->initial_md_sent = true;
        }
      }
      // Done filling MD.. Now call process ops.. to perform next set of ops..
      maybe_process_ops_locked(st, error);
    }
  }

   if (error == GRPC_ERROR_NONE &&
      (op->send_message || op->send_trailing_metadata ||
       op->recv_initial_metadata || op->recv_message ||
       op->recv_trailing_metadata)) {
      // Mark ops that need to be processed by the state machine
      if (op->send_message) {
        st->send_message_op = op;
      }
      if (op->send_trailing_metadata) {
        st->send_trailing_md_op = op;
      }
      if (op->recv_initial_metadata) {
        st->recv_initial_md_op = op;
      }
      if (op->recv_message) {
        st->recv_message_op = op;
      }
      if (op->recv_trailing_metadata) {
        st->recv_trailing_md_op = op;
      }
      // We want to initiate the state machine if:
      // 1. We want to send a message and the endpoint is open
      // 2. We want to send trailing metadata and there isn't an unmatched send
      // msg
      //    or the ep is open
      // 3. We want initial metadata and the other side has sent it
      // 4. We want to receive a message and there is a message ready and send ops already completed
      // 5. There is trailing metadata, even if nothing specifically wants
      //    that because that can shut down the receive message as well
      if ((op->send_message && st->t->ep) ||
          (op->send_trailing_metadata && (!st->send_message_op || (st->t->ep))) ||
          (op->recv_initial_metadata) || (op->recv_message && st->t->ep && st->t->is_client && st->sent_msg) || (op->recv_message && st->t->ep && !st->t->is_client)  ||
          (st->trailing_md_recvd) || op->recv_trailing_metadata) {
        op_state_machine_locked(st, error);
      } else {
        st->ops_toBeSent = true;
      }
   }
   else {
     if (error != GRPC_ERROR_NONE) {
       // Consume any send message that was sent here but that we are not
       // pushing through ep
       if (op->send_message) {
         op->payload->send_message.send_message.reset();
       }
       // Schedule op's closures that we didn't push to op state machine
       if (op->recv_initial_metadata) {
         if (op->payload->recv_initial_metadata.trailing_metadata_available !=
             nullptr) {
           // Set to true unconditionally, because we're failing the call, so
           // even if we haven't actually seen the send_trailing_metadata op
           // from the other side, we're going to return trailing metadata
           // anyway.
           *op->payload->recv_initial_metadata.trailing_metadata_available =
               true;
         }
        printf(
             "perform_stream_op error %p scheduling initial-metadata-ready %p \n",
             st, error);
         grpc_core::ExecCtx::Run(
             DEBUG_LOCATION,
             op->payload->recv_initial_metadata.recv_initial_metadata_ready,
             GRPC_ERROR_REF(error));
       }
       if (op->recv_message) {
         printf(
             "perform_stream_op error %p scheduling recv message-ready %p \n", st,
             error);
         grpc_core::ExecCtx::Run(DEBUG_LOCATION,
                                 op->payload->recv_message.recv_message_ready,
                                 GRPC_ERROR_REF(error));
       }
       if (op->recv_trailing_metadata) {
         printf(
             "perform_stream_op error %p scheduling trailing-metadata-ready %p \n",
             st, error);
         grpc_core::ExecCtx::Run(
             DEBUG_LOCATION,
             op->payload->recv_trailing_metadata.recv_trailing_metadata_ready,
             GRPC_ERROR_REF(error));
       }
     }
     printf( "perform_stream_op %p scheduling on_complete %p \n", st,
                error);
     grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_complete, GRPC_ERROR_REF(error));
   }
   gpr_mu_unlock(mu);
   GRPC_ERROR_UNREF(error);

}

// Perform Transport OP
void perform_transport_op(grpc_transport* gt, grpc_transport_op* op) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  grpc_diffproc_transport* t = reinterpret_cast<grpc_diffproc_transport*>(gt);
  printf("perform_transport_op %p %p \n", t, op);
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
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, op->on_consumed, GRPC_ERROR_NONE);
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
                                               get_endpoint};

static const grpc_transport_vtable* get_vtable(void) {
  return &diffproc_vtable;
}
grpc_transport* grpc_create_diffproc_transport(
    const grpc_channel_args* channel_args, grpc_endpoint* ep, bool is_client,
    grpc_resource_user* resource_user) {
  auto t =
      new grpc_diffproc_transport(channel_args, ep, is_client, resource_user);
  return &t->base;
}

void grpc_diffproc_initiate_write(grpc_diffproc_transport* t) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  t->ref();
  grpc_endpoint_write(
      t->ep, &t->outbuf,
      GRPC_CLOSURE_INIT(&t->write_action_end_locked, write_action_end, t,
                        grpc_schedule_on_exec_ctx),
      nullptr);
  // printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
}


void start_stream(grpc_diffproc_transport* t) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  if (t->accept_stream_cb != nullptr && !t->is_client && t->processed) {
    grpc_diffproc_stream* accepting = nullptr;
    GPR_ASSERT(t->accepting_stream == nullptr);
    t->accepting_stream = &accepting;
    t->accept_stream_cb(t->accept_stream_data, &t->base,
                        (void*)(&t->accepting_stream));
    t->accepting_stream = nullptr;
    t->processed = 0;
  }
}



void read_action_end(void* tp, grpc_error* error) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  grpc_diffproc_transport* t = static_cast<grpc_diffproc_transport*>(tp);

  GRPC_ERROR_REF(error);
    grpc_error* err = error;
    if (err != GRPC_ERROR_NONE) {
      close_transport_locked(t, GRPC_ERROR_REF(error));
    } else {

      printf("Read buf count after namedpipe read :%d \n",
             t->read_buffer.count);
    }
    t->unref();

}

void read_action_locked(grpc_diffproc_transport* t) {
  grpc_slice_buffer_reset_and_unref_internal(&t->read_buffer);
  t->ref();
  grpc_endpoint_read(t->ep, &t->read_buffer,
                     GRPC_CLOSURE_INIT(&t->read_action_locked, read_action_end,
                                       t, grpc_schedule_on_exec_ctx),
                     GRPC_ERROR_NONE);
}

void grpc_diffproc_transport_start_reading(grpc_transport* transport,
                                           grpc_slice_buffer* read_buffer) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  grpc_diffproc_transport* t =
      reinterpret_cast<grpc_diffproc_transport*>(transport);
  //t->ref();
  // if (read_buffer != nullptr) {
  //  grpc_slice_buffer_move_into(read_buffer, &t->read_buffer);
  //  gpr_free(read_buffer);
  //}
   //grpc_endpoint_read(
   //   t->ep, read_buffer,
   //                  GRPC_CLOSURE_INIT(&t->read_action_locked, read_action_end,
   //                                    t,
   //                     grpc_schedule_on_exec_ctx),
   //   GRPC_ERROR_NONE);
  start_stream(t);
  //grpc_core::ExecCtx::Run(
  //    DEBUG_LOCATION,
  //    GRPC_CLOSURE_INIT(&t->read_action_locked, read_action_end, t,
  //                      grpc_schedule_on_exec_ctx),
  //    GRPC_ERROR_NONE);
}


/*   BATCH HELPERS*/



//--------- Complete Function 
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
    printf( "%s %p %p %p \n", msg, s, op, error);
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, op->on_complete,
                            GRPC_ERROR_REF(error));
  }
}





/*   INIT and SHUTDOWN*/

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
