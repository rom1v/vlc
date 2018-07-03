/*****************************************************************************
 * Copyright (C) 2017 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <assert.h>
#include <vlc_common.h>
#include <vlc_atomic.h>
#include <vlc_list.h>
#include <vlc_threads.h>

#include "libvlc.h"
#include "background_worker.h"

struct task {
    struct vlc_list node;
    void* id; /**< id associated with entity */
    void* entity; /**< the entity to process */
    int timeout; /**< timeout duration in milliseconds */
};

struct background_worker;

struct background_thread {
    struct background_worker *owner;
    vlc_thread_t thread;
    vlc_mutex_t lock; /**< acquire to inspect members that follow */
    vlc_cond_t probe_cancel_wait; /**< wait for probe request or cancelation */
    vlc_tick_t deadline; /**< deadline of the current task */
    bool probe_request; /**< true if a probe is requested */
    bool cancel_request; /**< true if a cancel is requested */
    struct task *task; /**< current task */
    struct vlc_list node;
};

struct background_worker {
    void* owner;
    struct background_worker_config conf;

    vlc_mutex_t lock; /**< protect the following fields */

    int uncompleted; /**< number of tasks requested but not completed */
    int nthreads; /**< number of threads in the threads list */
    struct vlc_list threads;

    struct vlc_list queue;
    vlc_cond_t queue_wait;

    vlc_cond_t nothreads_wait; /**< wait for nthreads == 0 */
    bool closing;
};

static struct task *task_Create(struct background_worker *worker, void *id, void *entity, int timeout)
{
    struct task *task = malloc(sizeof(*task));
    if (unlikely(!task))
        return NULL;

    task->id = id;
    task->entity = entity;
    task->timeout = timeout < 0 ? worker->conf.default_timeout : timeout;
    worker->conf.pf_hold(task->entity);
    return task;
}

static void task_Destroy(struct background_worker *worker, struct task *task)
{
    worker->conf.pf_release(task->entity);
    free(task);
}

static struct task *QueueTake(struct background_worker *worker, int timeout_ms)
{
    vlc_assert_locked(&worker->lock);

    vlc_tick_t deadline = vlc_tick_now() + VLC_TICK_FROM_MS(timeout_ms);
    bool timeout = false;
    while (!timeout && !worker->closing && vlc_list_is_empty(&worker->queue))
        timeout = vlc_cond_timedwait(&worker->queue_wait, &worker->lock, deadline) != 0;

    if (worker->closing || timeout)
        return NULL;

    struct task *task = vlc_list_first_entry_or_null(&worker->queue, struct task, node);
    assert(task);
    vlc_list_remove(&task->node);

    return task;
}

static void QueuePush(struct background_worker *worker, struct task *task)
{
    vlc_assert_locked(&worker->lock);
    vlc_list_append(&task->node, &worker->queue);
    vlc_cond_signal(&worker->queue_wait);
}

static void QueueRemoveAll(struct background_worker *worker, void *id)
{
    vlc_assert_locked(&worker->lock);
    struct task *task;
    vlc_list_foreach(task, &worker->queue, node)
    {
        if (!id || task->id == id)
        {
            vlc_list_remove(&task->node);
            task_Destroy(worker, task);
        }
    }
}

static struct background_thread *background_thread_Create(struct background_worker *owner)
{
    struct background_thread *thread = malloc(sizeof(*thread));
    if (!thread)
        return NULL;

    vlc_mutex_init(&thread->lock);
    vlc_cond_init(&thread->probe_cancel_wait);
    thread->deadline = VLC_TICK_INVALID;
    thread->probe_request = false;
    thread->cancel_request = false;
    thread->task = NULL;
    thread->owner = owner;
    return thread;
}

static void background_thread_Destroy(struct background_thread *thread)
{
    vlc_cond_destroy(&thread->probe_cancel_wait);
    vlc_mutex_destroy(&thread->lock);
    free(thread);
}

static struct background_worker *background_worker_Create(void *owner,
                                         struct background_worker_config *conf)
{
    struct background_worker* worker = malloc(sizeof(*worker));
    if (unlikely(!worker))
        return NULL;

    worker->conf = *conf;
    worker->owner = owner;

    vlc_mutex_init(&worker->lock);
    worker->uncompleted = 0;
    worker->nthreads = 0;
    vlc_list_init(&worker->threads);
    vlc_list_init(&worker->queue);
    vlc_cond_init(&worker->queue_wait);
    vlc_cond_init(&worker->nothreads_wait);
    worker->closing = false;
    return worker;
}

static void background_worker_Destroy(struct background_worker *worker)
{
    vlc_cond_destroy(&worker->queue_wait);
    vlc_mutex_destroy(&worker->lock);
    free(worker);
}

static void FinishTask(struct background_thread *thread, struct task *task)
{
    struct background_worker *worker = thread->owner;
    task_Destroy(worker, task);

    vlc_mutex_lock(&thread->lock);
    thread->task = NULL;
    vlc_mutex_unlock(&thread->lock);

    vlc_mutex_lock(&worker->lock);
    worker->uncompleted--;
    assert(worker->uncompleted >= 0);
    vlc_mutex_unlock(&worker->lock);
}

static void RemoveThread(struct background_thread *thread)
{
    struct background_worker *worker = thread->owner;

    vlc_mutex_lock(&worker->lock);

    vlc_list_remove(&thread->node);
    worker->nthreads--;
    assert(worker->nthreads >= 0);
    if (!worker->nthreads)
        vlc_cond_signal(&worker->nothreads_wait);

    vlc_mutex_unlock(&worker->lock);

    background_thread_Destroy(thread);
}

static void* Thread( void* data )
{
    struct background_thread *thread = data;
    struct background_worker *worker = thread->owner;

    for (;;)
    {
        vlc_mutex_lock(&worker->lock);
        struct task *task = QueueTake(worker, 5000);
        vlc_mutex_unlock(&worker->lock);
        if (!task)
            /* terminate this thread */
            break;

        vlc_mutex_lock(&thread->lock);
        thread->task = task;
        thread->cancel_request = false;
        thread->probe_request = false;
        if (task->timeout > 0)
            thread->deadline = vlc_tick_now() + VLC_TICK_FROM_MS(task->timeout);
        else
            thread->deadline = INT64_MAX; /* no deadline */
        vlc_mutex_unlock(&thread->lock);

        void *handle;
        if (worker->conf.pf_start(worker->owner, task->entity, &handle))
        {
            FinishTask(thread, task);
            continue;
        }

        for (;;)
        {
            vlc_mutex_lock(&thread->lock);
            bool timeout = false;
            while (!timeout && !thread->probe_request && !thread->cancel_request)
                /* any non-zero return value means timeout */
                timeout = vlc_cond_timedwait(&thread->probe_cancel_wait, &thread->lock, thread->deadline) != 0;

            bool cancel = thread->cancel_request;
            thread->cancel_request = false;
            thread->probe_request = false;
            vlc_mutex_unlock(&thread->lock);

            if (timeout || cancel || worker->conf.pf_probe(worker->owner, handle))
            {
                worker->conf.pf_stop(worker->owner, handle);
                FinishTask(thread, task);
                break;
            }
        }
    }

    RemoveThread(thread);

    return NULL;
}

static bool SpawnThread(struct background_worker *worker)
{
    vlc_assert_locked(&worker->lock);


    struct background_thread *thread = background_thread_Create(worker);
    if (!thread)
        return false;

    if (vlc_clone_detach(&thread->thread, Thread, thread, VLC_THREAD_PRIORITY_LOW))
    {
        free(thread);
        return false;
    }
    worker->nthreads++;
    vlc_list_append(&thread->node, &worker->threads);

    return true;
}

struct background_worker* background_worker_New( void* owner,
    struct background_worker_config* conf )
{
    return background_worker_Create(owner, conf);
}

int background_worker_Push( struct background_worker* worker, void* entity,
                        void* id, int timeout )
{
    struct task *task = task_Create(worker, id, entity, timeout);
    if (unlikely(!task))
        return VLC_ENOMEM;

    vlc_mutex_lock(&worker->lock);
    QueuePush(worker, task);
    if (++worker->uncompleted > worker->nthreads && worker->nthreads < worker->conf.max_threads)
        SpawnThread(worker);
    vlc_mutex_unlock(&worker->lock);

    return VLC_SUCCESS;
}

static void BackgroundWorkerCancelLocked(struct background_worker *worker, void *id)
{
    vlc_assert_locked(&worker->lock);

    QueueRemoveAll(worker, id);

    struct background_thread *thread;
    vlc_list_foreach(thread, &worker->threads, node)
    {
        vlc_mutex_lock(&thread->lock);
        if (!id || (thread->task && thread->task->id == id && !thread->cancel_request))
        {
            thread->cancel_request = true;
            vlc_cond_signal(&thread->probe_cancel_wait);
        }
        vlc_mutex_unlock(&thread->lock);
    }
}

void background_worker_Cancel( struct background_worker* worker, void* id )
{
    vlc_mutex_lock(&worker->lock);
    BackgroundWorkerCancelLocked(worker, id);
    vlc_mutex_unlock(&worker->lock);
}

void background_worker_RequestProbe( struct background_worker* worker )
{
    vlc_mutex_lock(&worker->lock);

    struct background_thread *thread;
    vlc_list_foreach(thread, &worker->threads, node)
    {
        vlc_mutex_lock(&thread->lock);
        thread->probe_request = true;
        vlc_mutex_unlock(&thread->lock);
        vlc_cond_signal(&thread->probe_cancel_wait);
    }

    vlc_mutex_unlock(&worker->lock);
}

void background_worker_Delete( struct background_worker* worker )
{
    vlc_mutex_lock(&worker->lock);

    worker->closing = true;
    BackgroundWorkerCancelLocked(worker, NULL);
    /* closing is now true, this will wake up any QueueTake() */
    vlc_cond_broadcast(&worker->queue_wait);

    while (worker->nthreads)
        vlc_cond_wait(&worker->nothreads_wait, &worker->lock);

    vlc_mutex_unlock(&worker->lock);

    /* no threads use the worker anymore, we can destroy it */
    background_worker_Destroy(worker);
}
