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
#include <src\core\ext\transport\diffproc\namedpipe_thread.h>


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

    int readError = 0;
    int writeError = 0;

    int bytes_read;
    int bytes_written;

    grpc_thread_handle* threadHandle;
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
  cb = np->read_cb;
  GRPC_ERROR_REF(error);
  printf(" ********** BYTES READ :%d ************** \n", np->bytes_read);
  if (error == GRPC_ERROR_NONE) {
      if (np->readError != 0 && !np->shutting_down) { //If Read Error 
        char* utf8_message = gpr_format_message(np->readError);
        error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(utf8_message);
        gpr_free(utf8_message);
        grpc_slice_buffer_reset_and_unref_internal(np->read_slices);
      } else {
        if (np->bytes_read != 0 && !np->shutting_down) { //No error print bytes read
          GPR_ASSERT((size_t)np->bytes_read <=np->read_slices->length);
          if (static_cast<size_t>(np->bytes_read) != np->read_slices->length) {
            grpc_slice_buffer_trim_end(np->read_slices,np->read_slices->length - static_cast<size_t>(np->bytes_read),&np->last_read_buffer);
          }
          GPR_ASSERT((size_t)np->bytes_read == np->read_slices->length);

         // if (grpc_tcp_trace.enabled()) {
            size_t i;
            for (i = 0; i < np->read_slices->count; i++) {
              char* dump = grpc_dump_slice(np->read_slices->slices[i],
                                           GPR_DUMP_HEX | GPR_DUMP_ASCII);
              printf("READ %p (peer=%s): %s", np, np->peer_string,
                      dump);
              gpr_free(dump);
            }
   
          //}
        } 
        //else if (np->bytes_read == 0 && !np->shutting_down) {
        //  np->read_slices->length = 0;
        //}
        else { //bytes_read ==0 then end of stream or shutting down close stream
          //if (grpc_tcp_trace.enabled()) {
            gpr_log(GPR_INFO, "NP:%p unref read_slice", np);
          //}
          grpc_slice_buffer_reset_and_unref_internal(np->read_slices);
          error =
              np->shutting_down
                  ? GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                        "NP stream shutting down", &np->shutdown_error, 1)
                  : GRPC_ERROR_CREATE_FROM_STATIC_STRING("End of NP stream");
        }
      }
    }
    
    //dFlushFileBuffers(np->threadHandle->pipeHandle);
    //printf( " ***************  DIsconnectiong hanlde read complete : %p *******************", np->threadHandle->pipeHandle);
    //DisconnectNamedPipe(np->threadHandle->pipeHandle);
    //CloseHandle(np->handle);
    //grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, GRPC_ERROR_NONE);
    np->read_cb = NULL;
    namedpipe_unref(np);
    //grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, error);
    cb->cb(cb->cb_arg, error);
}

static void on_write(void* npp, grpc_error* error) {
  printf("\n%d :: %s :: %s\n", __LINE__, __func__, __FILE__);
  grpc_namedpipe* np = (grpc_namedpipe*)npp;
  grpc_closure* cb;

  GRPC_ERROR_REF(error);

  gpr_mu_lock(&np->mu);
  cb = np->write_cb;
  np->write_cb = NULL;
  gpr_mu_unlock(&np->mu);

  if (error == GRPC_ERROR_NONE) {
    if (np->writeError != 0) {
      error = GRPC_WSA_ERROR(np->writeError, "NPWrite");
    } else {
      GPR_ASSERT(np->bytes_written == np->write_slices->length);
    }
  }
  //puts("B4 flush file ");
  //FlushFileBuffers(np->threadHandle->pipeHandle);
  namedpipe_unref(np);
  //puts("A4 Unref & flush file ");
  //grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, error);
  cb->cb(cb->cb_arg, error);
}


#define BUFSIZE 4096
#define DEFAULT_TARGET_READ_SIZE 8192
#define MAX_WSABUF_COUNT 16
static void win_read(grpc_endpoint* ep, grpc_slice_buffer* read_slices,
                     grpc_closure* cb, bool urgent) {
  printf("\n%d :: %s :: %s:: %d\n", __LINE__, __func__, __FILE__, getpid());
  grpc_namedpipe* np = (grpc_namedpipe*)ep;
  HANDLE handle = np->threadHandle->pipeHandle;
  int ops = np->threadHandle->read_info.numOfOps;
  printf(" ****************************  WIN READ HANDLE : %p ", handle);
  int status;
  DWORD bytes_read = 0;
  DWORD dwErr;
  size_t i;
  TCHAR chRequest[BUFSIZE];
  int fSuccess;
  if (np->shutting_down) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb,GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING("Np HANDLE is shutting down", &np->shutdown_error, 1));
    return;
  }
  np->read_cb = cb;
  np->bytes_read = 0;
  /*  ---------- Buffer Read Try ---------- */

  WSABUF buffers[MAX_WSABUF_COUNT];
  np->read_cb = cb;
  np->read_slices = read_slices;
  grpc_slice_buffer_reset_and_unref_internal(read_slices);
  grpc_slice_buffer_swap(read_slices, &np->last_read_buffer);

  if (np->read_slices->length < DEFAULT_TARGET_READ_SIZE / 2 &&
      np->read_slices->count < MAX_WSABUF_COUNT) {
    // TODO(jtattermusch): slice should be allocated using resource quota
    grpc_slice_buffer_add(np->read_slices,
                          GRPC_SLICE_MALLOC(DEFAULT_TARGET_READ_SIZE));
  }
  
  char* buffer = (char*)GRPC_SLICE_START_PTR(np->read_slices->slices[0]);
  DWORD bufLen =  GRPC_SLICE_LENGTH(np->read_slices->slices[0]);
  GPR_ASSERT(np->read_slices->count <= MAX_WSABUF_COUNT);
  for (i = 0; i < np->read_slices->count; i++) {
    buffers[i].len = (ULONG)GRPC_SLICE_LENGTH(
        np->read_slices->slices[i]);  // we know slice size fits in 32bit.
    buffers[i].buf = (char*)GRPC_SLICE_START_PTR(np->read_slices->slices[i]);
  }
  namedpipe_ref(np);

  printf(" Count in read :%d \n", np->read_slices->count);
  i = 0;
 // for (i = 0; i < np->read_slices->count; i++) {
    puts("******************* IN READ LOOP ********************");
    DWORD bytesAvail = 0;
    if (!PeekNamedPipe(handle, NULL, 0, NULL, &bytesAvail, NULL)) {
      puts("Failed to call PeekNamedPipe");
    }
    printf("*********** BYTES Avail ************ %d\n", bytesAvail);
    // Read client requests from the pipe. This simplistic code only allows
    // messages up to BUFSIZE characters in length.
    fSuccess = ReadFile(handle,                     // handle to pipe
                        buffer,                     // buffer to receive data
                        bufLen,  // size of buffer
                        &bytes_read,                // number of bytes read
                        NULL);                      // not overlapped I/O
    np->threadHandle->read_info.numOfOps++;
    int lastError = GetLastError();
    printf("LastError in Read %d & fSuccess: %d \n", lastError, fSuccess);
    if (fSuccess == 0) {
      np->readError = lastError;
     // break;
    } else {
      buffer[bytes_read] = '\0';
      np->bytes_read = bytes_read;
      //continue;
    }

  //}
  //  if (!fSuccess || bytes_read == 0) {
  //    if (GetLastError() == ERROR_BROKEN_PIPE) {
  //      _tprintf(TEXT("InstanceThread: client disconnected.\n"));
  //    } else {
  //      _tprintf(TEXT("InstanceThread ReadFile failed, GLE=%d.\n"),GetLastError());
  //    }
  //    break;
  //  } else {
  //    //buffers[i].buf[bytes_read] = '\0';
  //    printf("Read message :%s and bytes read: %d \n", buffers[i].buf, bytes_read);
  //    np->bytes_read += bytes_read;
  //  }
  //}
  printf("np->readerror :%d \n", np->readError);
  if (np->readError == 0) on_read(np, GRPC_ERROR_NONE);
  else on_read(np, GRPC_WSA_ERROR(np->readError, "Read"));








///* First let's try a synchronous, non-blocking read. */
//  fSuccess = ReadFile(handle, chRequest, BUFSIZE * sizeof(TCHAR), &bytes_read, NULL);
//
//  // The read operation completed successfully.
//
//  if (fSuccess && bytes_read != 0) {
//    puts("Read ops completed successfully...");
//    printf("\n Read message  :%s\n", chRequest);
//    on_read(np, GRPC_ERROR_NONE);
//  } else {
//    dwErr = GetLastError();
//    if (!fSuccess && (dwErr == ERROR_IO_PENDING)) {
//      puts("ERROR ops still pending...");
//      return;
//    }
//  }

}


static void win_write(grpc_endpoint* ep, grpc_slice_buffer* slices, grpc_closure* cb, void* arg) {
  printf("\n%d :: %s :: %s:: %d\n", __LINE__, __func__, __FILE__, getpid());
  grpc_namedpipe* np = (grpc_namedpipe*)ep;
  HANDLE handle = np->threadHandle->pipeHandle;
  printf(" ****************************  WIN WRITE HANDLE : %p ", handle);
  int status;
  grpc_error* error = GRPC_ERROR_NONE;

  /* ------ BUFFER WRITE TRY ------ */
  WSABUF local_buffers[16];
  WSABUF* allocated = NULL;
  WSABUF* buffers = local_buffers;
  size_t len;
  DWORD cbWritten;
  size_t i;
  printf(" ************* To write in pipe :%d ************ \n", slices->count);
  for (i = 0; i < slices->count; i++) {
    char* data = grpc_dump_slice(slices->slices[i], GPR_DUMP_HEX | GPR_DUMP_ASCII);
    printf("WRITE %p (peer=%s): %s \n", np, np->peer_string, data);
    gpr_free(data);
  }
  if (np->shutting_down) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb,GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING("Named Pipe is shutting down", &np->shutdown_error, 1));
    return;
  }
  np->write_cb = cb;
  np->write_slices = slices;
  np->bytes_written = 0;
  GPR_ASSERT(np->write_slices->count <= UINT_MAX);
  if (np->write_slices->count > GPR_ARRAY_SIZE(local_buffers)) {
    buffers = (WSABUF*)gpr_malloc(sizeof(WSABUF) * np->write_slices->count);
    allocated = buffers;
  }
  for (i = 0; i < np->write_slices->count; i++) {
    puts("Count ++");
    len = GRPC_SLICE_LENGTH(np->write_slices->slices[i]);
    GPR_ASSERT(len <= ULONG_MAX);
    buffers[i].len = (ULONG)len;
    buffers[i].buf = (char*)GRPC_SLICE_START_PTR(np->write_slices->slices[i]);
  }

  namedpipe_ref(np);

  for (int i = 0; i < np->write_slices->count; i++) {
    status = WriteFile(handle,                 // pipe handle
                       buffers[i].buf,         // message
                       (DWORD)buffers[i].len,  // message length
                       &cbWritten,             // bytes written
                       NULL);
    int err = GetLastError();
    if (status == 1) {
      puts("Successfully wrote to server end pipe handle....");
      np->bytes_written += cbWritten;
    }
    else {
      _tprintf(TEXT("WriteFile to pipe failed. GLE=%d\n"), GetLastError());
      np->writeError = err;
      break;
    }
  }
  if (np->writeError == 0) on_write(np, GRPC_ERROR_NONE);
  else on_write(np, error);



  //LPCTSTR lpvMessage = TEXT("Hey this is the first time I am trying to use pipe \n");
  //DWORD cbToWrite = (lstrlen(lpvMessage) + 1) * sizeof(TCHAR);
  //DWORD cbWritten;
  //_tprintf(TEXT("Sending %d byte message: \"%s\"\n"), cbToWrite, lpvMessage);
  //status = WriteFile(handle,  // pipe handle
  //                     lpvMessage,    // message
  //                     cbToWrite,     // message length
  //                     &cbWritten,    // bytes written
  //                     NULL);
  //if (!status) {
  //  _tprintf(TEXT("WriteFile to pipe failed. GLE=%d\n"), GetLastError());
  //  error = GRPC_WSA_ERROR(WSAGetLastError(), "PIPEMODE");
  //} else {
  //  puts("Successfully wrote to server end pipe handle....");
  //  on_write(np, GRPC_ERROR_NONE);
  //}

  //printf("\nMessage sent to server, receiving reply as follows:\n");

  //grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, GRPC_WSA_ERROR(WSAGetLastError(), "NPSEND"));
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
  printf("\n%d :: %s :: %s \n", __LINE__, __func__, __FILE__);
  grpc_namedpipe* np = (grpc_namedpipe*)ep;
  gpr_mu_lock(&np->mu);
  /* At that point, what may happen is that we're already inside the IOCP
     callback. See the comments in on_read and on_write. */
  if (!np->shutting_down) {
    np->shutting_down = 1;
  } else {
    GRPC_ERROR_UNREF(why);
  }
  grpc_nphandle_shutdown(np->threadHandle);
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


grpc_endpoint* grpc_namedpipe_create(grpc_thread_handle* thread,
                                     grpc_channel_args* channel_args,
                                     const char* peer_string, BOOL isClient) {
  printf("\n%d :: %s :: %s\n",__LINE__,__func__, __FILE__); 
  HANDLE handle = thread->pipeHandle;
  grpc_namedpipe* np = (grpc_namedpipe*)gpr_malloc(sizeof(grpc_namedpipe));
  memset(np, 0, sizeof(grpc_namedpipe));
 // printf("Size of vtable %d %p %p \n", sizeof(grpc_namedpipe), np, &np->base);
  np->base.vtable = &vtable;
  //printf("Size of vtable %d \n", sizeof(&vtable));
 // printf("Size of vtable %d %p %p\n", sizeof(grpc_namedpipe), np, &np->base);
  np->threadHandle = thread;
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
