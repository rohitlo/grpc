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

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_WINSOCK_SOCKET

#include <limits.h>

#include "src/core/lib/iomgr/sockaddr_windows.h"

#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/log_windows.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/socket_windows.h"
#include "src/core/ext/transport/diffproc/namedpipe/namedpipe_windows.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"

typedef struct grpc_namedpipe{
    HANDLE handle;
    grpc_endpoint base;
    grpc_closure on_read;
    grpc_closure on_write;
    gpr_refcount refcount;
    grpc_closure* read_cb;
    grpc_closure* write_cb;
    /* garbage after the last read */
    grpc_slice_buffer last_read_buffer;

    grpc_slice_buffer* write_slices;
    grpc_slice_buffer* read_slices;

    gpr_mu mu;
    int shutting_down;
    grpc_error* shutdown_error;
    char* peer_string;
}grpc_namedpipe;

static void namedpipe_ref(grpc_namedpipe* np) { gpr_ref(&np->refcount); }
static void namedpipe_unref(grpc_namedpipe* np) { gpr_unref(&np->refcount); }


static void on_read(void* npp, grpc_error* error) {
    grpc_namedpipe* np = (grpc_namedpipe*)npp;
    grpc_closure* cb = np->read_cb;
    gpr_mu_lock(&np->mu);
    cb = np->read_cb;
    np->read_cb = NULL;
    gpr_mu_unlock(&np->mu);
    namedpipe_unref(np);
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, error);
}

#define DEFAULT_TARGET_READ_SIZE 8192
#define MAX_WSABUF_COUNT 16
static void win_read(grpc_endpoint* ep, grpc_slice_buffer* read_slices,
                     grpc_closure* cb, bool urgent) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  grpc_namedpipe* np = (grpc_namedpipe*)ep;
  HANDLE handle = np->handle;
  int status;
  DWORD bytes_read = 0;
  DWORD flags = 0;
  char readBuf[1023];
  WSABUF buffers[MAX_WSABUF_COUNT];
  size_t i;


  int BUFSIZE = 1023;
  if (np->shutting_down) {
    grpc_core::ExecCtx::Run(
        DEBUG_LOCATION, cb,
        GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
            "TCP socket is shutting down", &np->shutdown_error, 1));
    return;
  }

  np->read_cb = cb;
  namedpipe_ref(np);
/* First let's try a synchronous, non-blocking read. */
  status = ReadFile(handle, readBuf, BUFSIZE, &bytes_read, 0);
    /* Did we get data immediately ? Yay. */
    if (status == 0) {
    printf("Read is %p and %d", buffers, bytes_read);
        puts("Successfully read");
        grpc_core::ExecCtx::Run(DEBUG_LOCATION, &np->on_read, GRPC_ERROR_NONE);
        return;
  }else{
      puts("Error");
  }
}

static void on_write(void* npp, grpc_error* error) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  grpc_namedpipe* np = (grpc_namedpipe*)npp; 
  grpc_closure* cb;
  gpr_mu_lock(&np->mu);
  cb = np->write_cb;
  np->write_cb = NULL;
  gpr_mu_unlock(&np->mu);
  namedpipe_unref(np);
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, error);
}

static void win_write(grpc_endpoint* ep, grpc_slice_buffer* slices,
                     grpc_closure* cb, void* arg) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  grpc_namedpipe* np = (grpc_namedpipe*)ep;
  HANDLE handle = np->handle;
  int status;
  DWORD bytes_write = 0;
  DWORD flags = 0;
  DWORD pipeMode;
  WSABUF local_buffers[MAX_WSABUF_COUNT];
  WSABUF* allocated = NULL;
  WSABUF* buffers = local_buffers;


   size_t i;
  for (i = 0; i < slices->count; i++) {
    char* data =
        grpc_dump_slice(slices->slices[i], GPR_DUMP_HEX | GPR_DUMP_ASCII);
    printf( "WRITE %p (peer=%s): %s", np, np->peer_string, data);
    gpr_free(data);
  }



  ////WSABUF buffers[MAX_WSABUF_COUNT];
  char writeBuffer[] = "Hey Server, How are you? \n";
  DWORD writeBufferSize = strlen(writeBuffer);

  if (np->shutting_down) {
    grpc_core::ExecCtx::Run(
        DEBUG_LOCATION, cb,
        GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
            "Named Pipe is shutting down", &np->shutdown_error, 1));
    return;
  }

  np->write_cb = cb;
  puts("line 182");
      /* First let's try a synchronous, non-blocking read. */
 // for (size_t i = 0; i < buffers->len; i++) {
  status = WriteFile(handle, writeBuffer, writeBufferSize,
                       &bytes_write, 0);

    /* Did we get data immediately ? Yay. */
    if (status == 0) {
      printf("Successfully wrote %d ", bytes_write);
      grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb,
                              GRPC_WSA_ERROR(WSAGetLastError(), "WSASend"));
      return;
    } else {
      puts("Error");
    }
  //}
}


static void win_add_to_pollset(grpc_endpoint* ep, grpc_pollset* ps) {
  grpc_namedpipe* np;
  (void)ps;
  np = (grpc_namedpipe*)ep;

}

static void win_add_to_pollset_set(grpc_endpoint* ep, grpc_pollset_set* pss) {
  grpc_namedpipe* np;
  (void)pss;
  np = (grpc_namedpipe*)ep;
}

static void win_delete_from_pollset_set(grpc_endpoint* ep,
                                        grpc_pollset_set* pss) {}

/*  */
static void win_shutdown(grpc_endpoint* ep, grpc_error* why) {
  grpc_namedpipe* np = (grpc_namedpipe*)ep;
  gpr_mu_lock(&np->mu);
  /* At that point, what may happen is that we're already inside the IOCP
     callback. See the comments in on_read and on_write. */
  if (!np->shutting_down) {
    np->shutting_down = 1;
  } else {
    GRPC_ERROR_UNREF(why);
  }
  gpr_mu_unlock(&np->mu);
}

static void win_destroy(grpc_endpoint* ep) {
  grpc_namedpipe* np = (grpc_namedpipe*)ep;
  grpc_slice_buffer_reset_and_unref_internal(&np->last_read_buffer);
  namedpipe_unref(np);
}

static char* win_get_peer(grpc_endpoint* ep) {
  grpc_namedpipe* np = (grpc_namedpipe*)ep;
  return gpr_strdup(np->peer_string);
}

static grpc_resource_user* win_get_resource_user(grpc_endpoint* ep) {
  grpc_namedpipe* np = (grpc_namedpipe*)ep;
  return nullptr;
}

static int win_get_fd(grpc_endpoint* ep) { return -1; }

static bool win_can_track_err(grpc_endpoint* ep) { return false; }

static grpc_endpoint_vtable vtable = {win_read,
                                      win_write,
                                      win_add_to_pollset,
                                      win_add_to_pollset_set,
                                      win_delete_from_pollset_set,
                                      win_shutdown,
                                      win_destroy,
                                      win_get_resource_user,
                                      win_get_peer,
                                      win_get_fd,
                                      win_can_track_err};


grpc_endpoint* grpc_namedpipe_create(HANDLE hd,grpc_channel_args* channel_args,
                                     const char* peer_string) {
  printf("\n%d :: %s :: %s\n",__LINE__,__func__, __FILE__);                            
  HANDLE handle = nullptr;
  grpc_namedpipe* np = (grpc_namedpipe*)gpr_malloc(sizeof(grpc_namedpipe));
  memset(np, 0, sizeof(grpc_namedpipe));
  np->base.vtable = &vtable;
  np->handle = hd;
  gpr_mu_init(&np->mu);
  gpr_ref_init(&np->refcount, 1);
  GRPC_CLOSURE_INIT(&np->on_read, on_read, np, grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&np->on_write, on_write, np, grpc_schedule_on_exec_ctx);
  np->peer_string = gpr_strdup(peer_string);
  grpc_slice_buffer_init(&np->last_read_buffer);
  return &np->base;
}
#endif // GRPC_WINSOCK_SOCKET
