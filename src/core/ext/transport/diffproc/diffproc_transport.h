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
#pragma once
#ifndef GRPC_CORE_EXT_TRANSPORT_diffproc_diffproc_TRANSPORT_H
#define GRPC_CORE_EXT_TRANSPORT_diffproc_diffproc_TRANSPORT_H

#include <grpc/support/port_platform.h>
#include <src/core/lib/iomgr/endpoint.h>
#include "src/core/lib/transport/transport_impl.h"
#include  <stdio.h>

//grpc_channel* grpc_diffproc_channel_create(grpc_server* server,grpc_channel_args* args, void* reserved);

//grpc_transport* diffproc_transport_create(const grpc_channel_args* args,bool is_client);
grpc_transport* grpc_create_diffproc_transport(
    const grpc_channel_args* channel_args, grpc_endpoint* ep, bool is_client,
    grpc_resource_user* resource_user);

struct grpc_diffproc_transport {
  grpc_diffproc_transport(const grpc_channel_args* channel_args,
                          grpc_endpoint* ep, bool is_client,
                          grpc_resource_user* resource_user);

  ~grpc_diffproc_transport();

  void ref();

  void unref();

  grpc_transport base;
  gpr_refcount refs;
  bool is_client;
  char* peer_string;

  grpc_core::ConnectivityStateTracker state_tracker;
  void (*accept_stream_cb)(void* user_data, grpc_transport* transport,
                           const void* server_data);
  grpc_endpoint* ep;
  void* accept_stream_data;
  gpr_mu mu;
  bool is_closed = false;
  struct grpc_diffproc_stream* stream_list = nullptr;
  grpc_closure write_action_begin_locked;
  grpc_closure write_action;
  grpc_closure write_action_end_locked;
  BOOL writeState = 1;
  grpc_closure read_action_locked;
  /** incoming read bytes */
  grpc_slice_buffer read_buffer;

  /** data to write now */
  grpc_slice_buffer outbuf;
   };

struct grpc_diffproc_stream {
     grpc_diffproc_stream(grpc_diffproc_transport* t,
                          grpc_stream_refcount* refcount,
                          const void* server_data, grpc_core::Arena* arena);

     ~grpc_diffproc_stream();
     void ref(const char* reason);

     void unref(const char* reason);

     grpc_diffproc_transport* t;
     grpc_metadata_batch to_read_initial_md;
     uint32_t to_read_initial_md_flags = 0;
     bool to_read_initial_md_filled = false;
     grpc_metadata_batch to_read_trailing_md;
     bool to_read_trailing_md_filled = false;
     bool ops_needed = false;
     // Write buffer used only during gap at init time when client-side
     // stream is set up but server side stream is not yet set up
     grpc_metadata_batch write_buffer_initial_md;
     bool write_buffer_initial_md_filled = false;
     uint32_t write_buffer_initial_md_flags = 0;
     grpc_millis write_buffer_deadline = GRPC_MILLIS_INF_FUTURE;
     grpc_metadata_batch write_buffer_trailing_md;
     bool write_buffer_trailing_md_filled = false;
     grpc_error* write_buffer_cancel_error = GRPC_ERROR_NONE;

     bool other_side_closed = false;               // won't talk anymore
     bool write_buffer_other_side_closed = false;  // on hold
     grpc_stream_refcount* refs;
     grpc_closure* closure_at_destroy = nullptr;

     grpc_core::Arena* arena;

     grpc_transport_stream_op_batch* send_message_op = nullptr;
     grpc_transport_stream_op_batch* send_trailing_md_op = nullptr;
     grpc_transport_stream_op_batch* recv_initial_md_op = nullptr;
     grpc_transport_stream_op_batch* recv_message_op = nullptr;
     grpc_transport_stream_op_batch* recv_trailing_md_op = nullptr;

     grpc_slice_buffer recv_message;
     grpc_core::ManualConstructor<grpc_core::SliceBufferByteStream> recv_stream;
     bool recv_inited = false;

     bool initial_md_sent = false;
     bool trailing_md_sent = false;
     bool initial_md_recvd = false;
     bool trailing_md_recvd = false;

     bool closed = false;

     grpc_error* cancel_self_error = GRPC_ERROR_NONE;
     grpc_error* cancel_other_error = GRPC_ERROR_NONE;

     grpc_millis deadline = GRPC_MILLIS_INF_FUTURE;

       /** Is this stream closed for writing. */
     bool write_closed = false;
     /** Is this stream reading half-closed. */
     bool read_closed = false;
     /** Are all published incoming byte streams closed. */
     bool all_incoming_byte_streams_finished = false;
     /** Has this stream seen an error.
         If true, then pending incoming frames can be thrown away. */
     bool seen_error = false;
     /** Are we buffering writes on this stream? If yes, we won't become
        writable until there's enough queued up in the flow_controlled_buffer */
     bool write_buffering = false;

     /* have we sent or received the EOS bit? */
     bool eos_received = false;
     bool eos_sent = false;

     /** the error that resulted in this stream being read-closed */
     grpc_error* read_closed_error = GRPC_ERROR_NONE;
     /** the error that resulted in this stream being write-closed */
     grpc_error* write_closed_error = GRPC_ERROR_NONE;


       /** things the upper layers would like to send */
     grpc_metadata_batch* send_initial_metadata = nullptr;
     grpc_closure* send_initial_metadata_finished = nullptr;
     grpc_metadata_batch* send_trailing_metadata = nullptr;
     grpc_closure* send_trailing_metadata_finished = nullptr;

     bool listed = true;
     struct grpc_diffproc_stream* stream_list_prev;
     struct grpc_diffproc_stream* stream_list_next;
   };

void grpc_diffproc_initiate_write(grpc_diffproc_transport* t);

extern grpc_core::TraceFlag grpc_diffproc_trace;

void grpc_diffproc_transport_init(void);
void grpc_diffproc_transport_shutdown(void);
//bool cancel_stream_locked(diffproc_stream* s, grpc_error* error);
//void maybe_process_ops_locked(diffproc_stream* s, grpc_error* error);
//void op_state_machine_locked(diffproc_stream* s, grpc_error* error);
//void log_metadata(const grpc_metadata_batch* md_batch, bool is_client,
//                  bool is_initial);
//grpc_error* fill_in_metadata(diffproc_stream* s,
//                             const grpc_metadata_batch* metadata,
//                             uint32_t flags, grpc_metadata_batch* out_md,
//                             uint32_t* outflags, bool* markfilled);
void write_action_end(void* tp, grpc_error* error);

void grpc_diffproc_transport_start_reading(grpc_transport* transport,
                                           grpc_slice_buffer* read_buffer);
#endif /* GRPC_CORE_EXT_TRANSPORT_diffproc_diffproc_TRANSPORT_H */
