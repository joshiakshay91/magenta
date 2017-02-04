// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <err.h>
#include <kernel/auto_lock.h>
#include <kernel/thread.h>
#include <lib/console.h>
#include <lib/user_copy.h>
#include <list.h>

#include <magenta/port_dispatcher.h>
#include <magenta/magenta.h>
#include <magenta/process_dispatcher.h>
#include <magenta/thread_dispatcher.h>
#include <magenta/user_copy.h>
#include <magenta/user_thread.h>

#include <platform.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

static mx_status_t object_unbind_exception_port(mx_handle_t obj_handle, bool debugger) {
    //TODO: check rights once appropriate right is determined

    if (obj_handle == MX_HANDLE_INVALID) {
        //TODO: handle for system exception
        if (debugger)
            return ERR_INVALID_ARGS;
        ResetSystemExceptionPort();
        return NO_ERROR;
    }

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<Dispatcher> dispatcher;
    auto status = up->GetDispatcher(obj_handle, &dispatcher);
    if (status != NO_ERROR)
        return status;

    auto process = DownCastDispatcher<ProcessDispatcher>(&dispatcher);
    if (process) {
        process->ResetExceptionPort(debugger);
        return NO_ERROR;
    }

    auto thread = DownCastDispatcher<ThreadDispatcher>(&dispatcher);
    if (thread) {
        if (debugger)
            return ERR_INVALID_ARGS;
        thread->ResetExceptionPort();
        return NO_ERROR;
    }

    return ERR_WRONG_TYPE;
}

static mx_status_t object_bind_exception_port(mx_handle_t obj_handle, mx_handle_t eport_handle, uint64_t key, bool debugger) {
    //TODO: check rights once appropriate right is determined
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PortDispatcher> ioport;
    mx_status_t status = up->GetDispatcher(eport_handle, &ioport);
    if (status != NO_ERROR)
        return status;

    mxtl::RefPtr<ExceptionPort> eport;
    status = ExceptionPort::Create(mxtl::move(ioport), key, &eport);
    if (status != NO_ERROR)
        return status;

    if (obj_handle == MX_HANDLE_INVALID) {
        //TODO: handle for system exception
        if (debugger)
            return ERR_INVALID_ARGS;
        return SetSystemExceptionPort(mxtl::move(eport));
    }

    mxtl::RefPtr<Dispatcher> dispatcher;
    status = up->GetDispatcher(obj_handle, &dispatcher);
    if (status != NO_ERROR)
        return status;

    auto process = DownCastDispatcher<ProcessDispatcher>(&dispatcher);
    if (process) {
        return process->SetExceptionPort(mxtl::move(eport), debugger);
    }

    auto thread = DownCastDispatcher<ThreadDispatcher>(&dispatcher);
    if (thread) {
        if (debugger)
            return ERR_INVALID_ARGS;
        return thread->SetExceptionPort(mxtl::move(eport));
    }

    return ERR_WRONG_TYPE;
}

mx_status_t sys_object_bind_exception_port(mx_handle_t obj_handle, mx_handle_t eport_handle,
                                           uint64_t key, uint32_t options) {
    LTRACE_ENTRY;

    if (options & ~MX_EXCEPTION_PORT_DEBUGGER)
        return ERR_INVALID_ARGS;
    bool debugger = (options & MX_EXCEPTION_PORT_DEBUGGER) != 0;

    if (eport_handle == MX_HANDLE_INVALID) {
        return object_unbind_exception_port(obj_handle, debugger);
    } else {
        return object_bind_exception_port(obj_handle, eport_handle, key, debugger);
    }
}

mx_status_t sys_task_resume(mx_handle_t handle, uint32_t options) {
    LTRACE_ENTRY;

    if (options & (~(MX_RESUME_EXCEPTION | MX_RESUME_NOT_HANDLED)))
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<Dispatcher> dispatcher;
    auto status = up->GetDispatcher(handle, &dispatcher);
    if (status != NO_ERROR)
        return status;

    auto thread = DownCastDispatcher<ThreadDispatcher>(&dispatcher);
    if (!thread)
        return ERR_WRONG_TYPE;

    if (options & MX_RESUME_EXCEPTION) {
        mx_exception_status_t estatus;
        if (options & MX_RESUME_NOT_HANDLED) {
            estatus = MX_EXCEPTION_STATUS_NOT_HANDLED;
        } else {
            estatus = MX_EXCEPTION_STATUS_RESUME;
        }
        return thread->thread()->MarkExceptionHandled(estatus);
    }

    //TODO: generic thread suspend/resume
    return ERR_NOT_SUPPORTED;
}