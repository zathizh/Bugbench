/* ====================================================================
 * The Apache Software License, Version 1.1
 *
 * Copyright (c) 2000-2003 The Apache Software Foundation.  All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. The end-user documentation included with the redistribution,
 *    if any, must include the following acknowledgment:
 *       "This product includes software developed by the
 *        Apache Software Foundation (http://www.apache.org/)."
 *    Alternately, this acknowledgment may appear in the software itself,
 *    if and wherever such third-party acknowledgments normally appear.
 *
 * 4. The names "Apache" and "Apache Software Foundation" must
 *    not be used to endorse or promote products derived from this
 *    software without prior written permission. For written
 *    permission, please contact apache@apache.org.
 *
 * 5. Products derived from this software may not be called "Apache",
 *    nor may "Apache" appear in their name, without prior written
 *    permission of the Apache Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE APACHE SOFTWARE FOUNDATION OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * ====================================================================
 *
 * This software consists of voluntary contributions made by many
 * individuals on behalf of the Apache Software Foundation.  For more
 * information on the Apache Software Foundation, please see
 * <http://www.apache.org/>.
 */

#include "apr.h"
#include "apr_private.h"
#include "apr_general.h"
#include "apr_strings.h"
#include "win32/apr_arch_thread_mutex.h"
#include "win32/apr_arch_thread_cond.h"
#include "apr_portable.h"

static apr_status_t thread_cond_cleanup(void *data)
{
    apr_thread_cond_t *cond = data;
    CloseHandle(cond->mutex);
    CloseHandle(cond->event);
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_thread_cond_create(apr_thread_cond_t **cond,
                                                 apr_pool_t *pool)
{
    *cond = apr_palloc(pool, sizeof(**cond));
    (*cond)->pool = pool;
    (*cond)->event = CreateEvent(NULL, TRUE, FALSE, NULL);
    (*cond)->mutex = CreateMutex(NULL, FALSE, NULL);
    (*cond)->signal_all = 0;
    (*cond)->num_waiting = 0;
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_thread_cond_wait(apr_thread_cond_t *cond,
                                               apr_thread_mutex_t *mutex)
{
    DWORD res;

    while (1) {
        res = WaitForSingleObject(cond->mutex, INFINITE);
        if (res != WAIT_OBJECT_0) {
            return apr_get_os_error();
        }
        cond->num_waiting++;
        ReleaseMutex(cond->mutex);

        apr_thread_mutex_unlock(mutex);
        res = WaitForSingleObject(cond->event, INFINITE);
        cond->num_waiting--;
        if (res != WAIT_OBJECT_0) {
            apr_status_t rv = apr_get_os_error();
            ReleaseMutex(cond->mutex);
            return rv;
        }
        if (cond->signal_all) {
            if (cond->num_waiting == 0) {
                ResetEvent(cond->event);
            }
            break;
        }
        if (cond->signalled) {
            cond->signalled = 0;
            ResetEvent(cond->event);
            break;
        }
        ReleaseMutex(cond->mutex);
    }
    apr_thread_mutex_lock(mutex);
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_thread_cond_timedwait(apr_thread_cond_t *cond,
                                                    apr_thread_mutex_t *mutex,
                                                    apr_interval_time_t timeout)
{
    DWORD res;
    DWORD timeout_ms = (DWORD) apr_time_as_msec(timeout);

    while (1) {
        res = WaitForSingleObject(cond->mutex, timeout_ms);
        if (res != WAIT_OBJECT_0) {
            if (res == WAIT_TIMEOUT) {
                return APR_TIMEUP;
            }
            return apr_get_os_error();
        }
        cond->num_waiting++;
        ReleaseMutex(cond->mutex);

        apr_thread_mutex_unlock(mutex);
        res = WaitForSingleObject(cond->event, timeout_ms);
        cond->num_waiting--;
        if (res != WAIT_OBJECT_0) {
            apr_status_t rv = apr_get_os_error();
            ReleaseMutex(cond->mutex);
            apr_thread_mutex_lock(mutex);
            if (res == WAIT_TIMEOUT) {
                return APR_TIMEUP;
            }
            return apr_get_os_error();
        }
        if (cond->signal_all) {
            if (cond->num_waiting == 0) {
                ResetEvent(cond->event);
            }
            break;
        }
        if (cond->signalled) {
            cond->signalled = 0;
            ResetEvent(cond->event);
            break;
        }
        ReleaseMutex(cond->mutex);
    }
    apr_thread_mutex_lock(mutex);
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_thread_cond_signal(apr_thread_cond_t *cond)
{
    apr_status_t rv = APR_SUCCESS;
    DWORD res;

    res = WaitForSingleObject(cond->mutex, INFINITE);
    if (res != WAIT_OBJECT_0) {
        return apr_get_os_error();
    }
    cond->signalled = 1;
    res = SetEvent(cond->event);
    if (res == 0) {
        rv = apr_get_os_error();
    }
    ReleaseMutex(cond->mutex);
    return rv;
}

APR_DECLARE(apr_status_t) apr_thread_cond_broadcast(apr_thread_cond_t *cond)
{
    apr_status_t rv = APR_SUCCESS;
    DWORD res;

    res = WaitForSingleObject(cond->mutex, INFINITE);
    if (res != WAIT_OBJECT_0) {
        return apr_get_os_error();
    }
    cond->signalled = 1;
    cond->signal_all = 1;
    res = SetEvent(cond->event);
    if (res == 0) {
        rv = apr_get_os_error();
    }
    ReleaseMutex(cond->mutex);
    return rv;
}

APR_DECLARE(apr_status_t) apr_thread_cond_destroy(apr_thread_cond_t *cond)
{
    return apr_pool_cleanup_run(cond->pool, cond, thread_cond_cleanup);
}

APR_POOL_IMPLEMENT_ACCESSOR(thread_cond)

