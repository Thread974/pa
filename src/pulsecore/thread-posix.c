/***
  This file is part of PulseAudio.

  Copyright 2006 Lennart Poettering
  Copyright 2006 Pierre Ossman <ossman@cendio.se> for Cendio AB

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2.1 of the License,
  or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pthread.h>
#include <sched.h>
#include <errno.h>

#ifdef __linux__
#include <sys/prctl.h>
#endif

#include <pulse/xmalloc.h>
#include <pulsecore/atomic.h>
#include <pulsecore/macro.h>

#include "thread.h"

struct pa_thread {
    pthread_t id;
    pa_thread_func_t thread_func;
    void *userdata;
    pa_atomic_t running;
    pa_bool_t joined;
    char *name;
};

struct pa_tls {
    pthread_key_t key;
};

static void thread_free_cb(void *p) {
    pa_thread *t = p;

    pa_assert(t);

    if (!t->thread_func) {
        /* This is a foreign thread, we need to free the struct */
        pa_xfree(t->name);
        pa_xfree(t);
    }
}

PA_STATIC_TLS_DECLARE(current_thread, thread_free_cb);

static void* internal_thread_func(void *userdata) {
    pa_thread *t = userdata;
    pa_assert(t);

#ifdef __linux__
    prctl(PR_SET_NAME, t->name);
#elif defined(HAVE_PTHREAD_SETNAME_NP) && defined(OS_IS_DARWIN)
    pthread_setname_np(t->name);
#endif

    t->id = pthread_self();

    PA_STATIC_TLS_SET(current_thread, t);

    pa_atomic_inc(&t->running);
    t->thread_func(t->userdata);
    pa_atomic_sub(&t->running, 2);

    return NULL;
}

/* gprof-helper.c -- preload library to profile pthread-enabled programs
 *
 * Authors: Sam Hocevar <sam at zoy dot org>
 *          Daniel Jönsson <danieljo at fagotten dot org>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the Do What The Fuck You Want To
 *  Public License as published by Banlu Kemiyatorn. See
 *  http://sam.zoy.org/projects/COPYING.WTFPL for more details.
 *
 * Compilation example:
 * gcc -shared -fPIC gprof-helper.c -o gprof-helper.so -lpthread -ldl
 *
 * Usage example:
 * LD_PRELOAD=./gprof-helper.so your_program
 */

/* Our data structure passed to the wrapper */
typedef struct wrapper_s
{
    void * (*start_routine)(void *);
    void * arg;

    pthread_mutex_t lock;
    pthread_cond_t  wait;

    struct itimerval itimer;

} wrapper_t;

/* The wrapper function in charge for setting the itimer value */
static void * wrapper_routine(void * data)
{
    /* Put user data in thread-local variables */
    void * (*start_routine)(void *) = ((wrapper_t*)data)->start_routine;
    void * arg = ((wrapper_t*)data)->arg;

    /* Set the profile timer value */
    setitimer(ITIMER_PROF, &((wrapper_t*)data)->itimer, NULL);

    /* Tell the calling thread that we don't need its data anymore */
    pthread_mutex_lock(&((wrapper_t*)data)->lock);
    pthread_cond_signal(&((wrapper_t*)data)->wait);
    pthread_mutex_unlock(&((wrapper_t*)data)->lock);

    /* Call the real function */
    return start_routine(arg);
}

/* Our wrapper function for the real pthread_create() */
static int hacked_pthread_create(pthread_t *__restrict thread,
                   __const pthread_attr_t *__restrict attr,
                   void * (*start_routine)(void *),
                   void *__restrict arg)
{
    wrapper_t wrapper_data;
    int i_return;
    /* Initialize the wrapper structure */
    wrapper_data.start_routine = start_routine;
    wrapper_data.arg = arg;
    getitimer(ITIMER_PROF, &wrapper_data.itimer);
    pthread_cond_init(&wrapper_data.wait, NULL);
    pthread_mutex_init(&wrapper_data.lock, NULL);
    pthread_mutex_lock(&wrapper_data.lock);

    fprintf(stderr, "pthreads: pthread_create wrapper called %lu.%03lu\n", wrapper_data.itimer.it_interval.tv_sec, wrapper_data.itimer.it_interval.tv_usec/1000);
    fprintf(stderr, "pthreads: pthread_create wrapper called %lu.%03lu\n", wrapper_data.itimer.it_value.tv_sec, wrapper_data.itimer.it_value.tv_usec/1000);

    /* The real pthread_create call */
    i_return = pthread_create(thread,
                                   attr,
                                   &wrapper_routine,
                                   &wrapper_data);

    /* If the thread was successfully spawned, wait for the data
     * to be released */
    if(i_return == 0)
    {
        pthread_cond_wait(&wrapper_data.wait, &wrapper_data.lock);
    }

    pthread_mutex_unlock(&wrapper_data.lock);
    pthread_mutex_destroy(&wrapper_data.lock);
    pthread_cond_destroy(&wrapper_data.wait);

    return i_return;
}

pa_thread* pa_thread_new(const char *name, pa_thread_func_t thread_func, void *userdata) {
    pa_thread *t;

    pa_assert(thread_func);

    t = pa_xnew0(pa_thread, 1);
    t->name = pa_xstrdup(name);
    t->thread_func = thread_func;
    t->userdata = userdata;

    pa_log_debug("new thread");

    if (hacked_pthread_create(&t->id, NULL, internal_thread_func, t) < 0) {
        pa_xfree(t);
        return NULL;
    }

    pa_atomic_inc(&t->running);

    return t;
}

int pa_thread_is_running(pa_thread *t) {
    pa_assert(t);

    /* Unfortunately there is no way to tell whether a "foreign"
     * thread is still running. See
     * http://udrepper.livejournal.com/16844.html for more
     * information */
    pa_assert(t->thread_func);

    return pa_atomic_load(&t->running) > 0;
}

void pa_thread_free(pa_thread *t) {
    pa_assert(t);

    pa_thread_join(t);

    pa_xfree(t->name);
    pa_xfree(t);
}

int pa_thread_join(pa_thread *t) {
    pa_assert(t);
    pa_assert(t->thread_func);

    if (t->joined)
        return -1;

    t->joined = TRUE;
    return pthread_join(t->id, NULL);
}

pa_thread* pa_thread_self(void) {
    pa_thread *t;

    if ((t = PA_STATIC_TLS_GET(current_thread)))
        return t;

    /* This is a foreign thread, let's create a pthread structure to
     * make sure that we can always return a sensible pointer */

    t = pa_xnew0(pa_thread, 1);
    t->id = pthread_self();
    t->joined = TRUE;
    pa_atomic_store(&t->running, 2);

    PA_STATIC_TLS_SET(current_thread, t);

    return t;
}

void* pa_thread_get_data(pa_thread *t) {
    pa_assert(t);

    return t->userdata;
}

void pa_thread_set_data(pa_thread *t, void *userdata) {
    pa_assert(t);

    t->userdata = userdata;
}

void pa_thread_set_name(pa_thread *t, const char *name) {
    pa_assert(t);

    pa_xfree(t->name);
    t->name = pa_xstrdup(name);

#ifdef __linux__
    prctl(PR_SET_NAME, name);
#elif defined(HAVE_PTHREAD_SETNAME_NP) && defined(OS_IS_DARWIN)
    pthread_setname_np(name);
#endif
}

const char *pa_thread_get_name(pa_thread *t) {
    pa_assert(t);

#ifdef __linux__
    if (!t->name) {
        t->name = pa_xmalloc(17);

        if (prctl(PR_GET_NAME, t->name) >= 0)
            t->name[16] = 0;
        else {
            pa_xfree(t->name);
            t->name = NULL;
        }
    }
#elif defined(HAVE_PTHREAD_GETNAME_NP) && defined(OS_IS_DARWIN)
    if (!t->name) {
        t->name = pa_xmalloc0(17);
        pthread_getname_np(t->id, t->name, 16);
    }
#endif

    return t->name;
}

void pa_thread_yield(void) {
#ifdef HAVE_PTHREAD_YIELD
    pthread_yield();
#else
    pa_assert_se(sched_yield() == 0);
#endif
}

pa_tls* pa_tls_new(pa_free_cb_t free_cb) {
    pa_tls *t;

    t = pa_xnew(pa_tls, 1);

    if (pthread_key_create(&t->key, free_cb) < 0) {
        pa_xfree(t);
        return NULL;
    }

    return t;
}

void pa_tls_free(pa_tls *t) {
    pa_assert(t);

    pa_assert_se(pthread_key_delete(t->key) == 0);
    pa_xfree(t);
}

void *pa_tls_get(pa_tls *t) {
    pa_assert(t);

    return pthread_getspecific(t->key);
}

void *pa_tls_set(pa_tls *t, void *userdata) {
    void *r;

    r = pthread_getspecific(t->key);
    pa_assert_se(pthread_setspecific(t->key, userdata) == 0);
    return r;
}
