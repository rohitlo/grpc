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
#include <tchar.h>
#include <process.h>
#include <src\core\lib\iomgr\iocp_windows.h>


typedef struct grpc_namedpipe{
    grpc_endpoint base;
    HANDLE handle;
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
    int shutting_down  = 0;
    grpc_error* shutdown_error;
    char* peer_string;
}grpc_namedpipe;



static void namedpipe_ref(grpc_namedpipe* np) { gpr_ref(&np->refcount); }
static void namedpipe_unref(grpc_namedpipe* np) { gpr_unref(&np->refcount); }


static void on_read(void* npp, grpc_error* error) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
    grpc_namedpipe* np = (grpc_namedpipe*)npp;
    grpc_closure* cb = np->read_cb;
    gpr_mu_lock(&np->mu);
    cb = np->read_cb;
    np->read_cb = NULL;
    gpr_mu_unlock(&np->mu);
    namedpipe_unref(np);
    FlushFileBuffers(np->handle);
    DisconnectNamedPipe(np->handle);
    CloseHandle(np->handle);
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, GRPC_ERROR_NONE);
}

static void on_write(void* npp, grpc_error* error) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  grpc_namedpipe* np = (grpc_namedpipe*)npp;
  grpc_closure* cb;
  printf("\n%d :: %s :: %s :: %p :: %p\n", __LINE__, __func__, __FILE__,
         np->base, np->handle);
  gpr_mu_lock(&np->mu);
  cb = np->write_cb;
  np->write_cb = NULL;
  gpr_mu_unlock(&np->mu);
  FlushFileBuffers(np->handle);
  DisconnectNamedPipe(np->handle);
  CloseHandle(np->handle);
  namedpipe_unref(np);
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, error);
}


#define BUFSIZE 4096
static void win_read(grpc_endpoint* ep, grpc_slice_buffer* read_slices,
                     grpc_closure* cb, bool urgent) {
  printf("\n%d :: %s :: %s:: %d\n", __LINE__, __func__, __FILE__, getpid());
  grpc_namedpipe* np = (grpc_namedpipe*)ep;
  HANDLE handle = np->handle;
  int status;
  DWORD bytes_read = 0;
  DWORD dwErr;
  size_t i;
  TCHAR chRequest[BUFSIZE];
  int fSuccess;
 /* if (np->shutting_down) {
    grpc_core::ExecCtx::Run(
        DEBUG_LOCATION, cb,
        GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
            "TCP socket is shutting down", &np->shutdown_error, 1));
    return;
  }*/
  np->read_cb = cb;
  namedpipe_ref(np);
/* First let's try a synchronous, non-blocking read. */
  fSuccess = ReadFile(handle, chRequest, BUFSIZE * sizeof(TCHAR), &bytes_read, NULL);

  // The read operation completed successfully.

  if (fSuccess && bytes_read != 0) {
    puts("Read ops completed successfully...");
    printf("\n Read message  :%s\n", chRequest);
    on_write(np, GRPC_ERROR_NONE);
  } else {
    dwErr = GetLastError();
    if (!fSuccess && (dwErr == ERROR_IO_PENDING)) {
      puts("ERROR ops still pending...");
      return;
    }
  }

}


static void win_write(grpc_endpoint* ep, grpc_slice_buffer* slices,
                     grpc_closure* cb, void* arg) {
  printf("\n%d :: %s :: %s:: %d\n", __LINE__, __func__, __FILE__, getpid());
  grpc_namedpipe* np = (grpc_namedpipe*)ep;
  HANDLE handle = np->handle;
  int status;
  grpc_error* error = GRPC_ERROR_NONE;
  if (np->shutting_down) {
    grpc_core::ExecCtx::Run(
        DEBUG_LOCATION, cb,
        GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
            "Named Pipe is shutting down", &np->shutdown_error, 1));
    return;
  }

  np->write_cb = cb;

  LPCTSTR lpvMessage = TEXT("Hey this is the first time I am trying to use pipe \n");
  DWORD cbToWrite = (lstrlen(lpvMessage) + 1) * sizeof(TCHAR);
  DWORD cbWritten;
  _tprintf(TEXT("Sending %d byte message: \"%s\"\n"), cbToWrite, lpvMessage);
  status = WriteFile(handle,  // pipe handle
                       lpvMessage,    // message
                       cbToWrite,     // message length
                       &cbWritten,    // bytes written
                       NULL);
  if (!status) {
    _tprintf(TEXT("WriteFile to pipe failed. GLE=%d\n"), GetLastError());
    error = GRPC_WSA_ERROR(WSAGetLastError(), "PIPEMODE");
  }

  //printf("\nMessage sent to server, receiving reply as follows:\n");

  grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, GRPC_WSA_ERROR(WSAGetLastError(), "NPSEND"));
}


static void win_add_to_pollset(grpc_endpoint* ep, grpc_pollset* ps) {
  grpc_namedpipe* np;
  (void)ps;
  np = (grpc_namedpipe*)ep;
  grpc_iocp_add_socket((grpc_winsocket*)np->handle);

}

static void win_add_to_pollset_set(grpc_endpoint* ep, grpc_pollset_set* pss) {
  grpc_namedpipe* np;
  (void)pss;
  np = (grpc_namedpipe*)ep;
  grpc_iocp_add_socket((grpc_winsocket*)np->handle);
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
                                     const char* peer_string, BOOL isClient) {
  printf("\n%d :: %s :: %s\n",__LINE__,__func__, __FILE__); 
  HANDLE handle = hd;
  grpc_namedpipe* np = (grpc_namedpipe*)gpr_malloc(sizeof(grpc_namedpipe));
  memset(np, 0, sizeof(grpc_namedpipe));
 // printf("Size of vtable %d %p %p \n", sizeof(grpc_namedpipe), np, &np->base);
  np->base.vtable = &vtable;
  //printf("Size of vtable %d \n", sizeof(&vtable));
 // printf("Size of vtable %d %p %p\n", sizeof(grpc_namedpipe), np, &np->base);
  np->handle = hd;
  gpr_mu_init(&np->mu);
  gpr_ref_init(&np->refcount, 1);
  GRPC_CLOSURE_INIT(&np->on_read, on_read, np, grpc_schedule_on_exec_ctx);
  //on_read(np);
  GRPC_CLOSURE_INIT(&np->on_write, on_write, np, grpc_schedule_on_exec_ctx);
  np->peer_string = gpr_strdup("peer");
  grpc_slice_buffer_init(&np->last_read_buffer);
  //printf("\n%d :: %s :: %s :: %p :: %p :: %p\n", __LINE__, __func__, __FILE__, np, np->handle, &np->base);
  return &np->base;
}
#endif // GRPC_WINSOCK_SOCKET
