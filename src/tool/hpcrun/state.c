// -*-Mode: C++;-*- // technically C99
// $Id$

#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "env.h"
#include "state.h"
#include "csproflib_private.h"
#include "mem.h"
#include "epoch.h"
#include "name.h"
#include "thread_data.h"
#include "pmsg.h"
#include "hpcrun_return_codes.h"
#include "monitor.h"


/* non-threaded profilers can have a single profiling state...
   but it can't be statically allocated because of epochs */

#if 0
static csprof_state_t *current_state;

/* fetching states */

// get the static state variable, used for thread init of
// initial thread
static csprof_state_t *_get_static_state(void){
  return current_state;
}

static void _set_static_state(csprof_state_t *state){
  current_state = state;
}
#endif

#if 0
#ifdef CSPROF_THREADS
static state_t_f      *csprof_get_state_internal = &_get_static_state;
static state_t_setter *_set_state_internal       = &_set_static_state;

// get state from thread specific data
static csprof_state_t *_get_threaded_state(void)
{
  TMSG(THREAD,"thread asking for state %p",pthread_getspecific(prof_data_key));
  return pthread_getspecific(prof_data_key);
}

void _set_threaded_state(csprof_state_t *state)
{
  pthread_setspecific(prof_data_key, state);
}

void state_threaded(void)
{
  csprof_get_state_internal = &_get_threaded_state;
  _set_state_internal       = &_set_threaded_state;
}
#else // ! CSPROF_THREADS
#define csprof_get_state_internal _get_static_state
#define _set_state_internal       _set_static_state

// get main thread safe state
csprof_state_t *csprof_get_safe_state(void)
{
  return _get_static_state();
}

csprof_state_t *csprof_get_state()
{
  csprof_state_t *state = csprof_get_state_internal();

  if (state == NULL) {
    csprof_state_init(state);
    state = csprof_get_state_internal();
  }
  return state;
}
#endif // CSPROF_THREADS
#endif

void csprof_set_state(csprof_state_t *state)
{
  TMSG(STATE," --Set");
  state->next = TD_GET(state);
  TD_GET(state) = state;
}

int
csprof_state_init(csprof_state_t *x)
{
  /* ia64 Linux has this function return a `long int', which is a 64-bit
     integer.  Tru64 Unix returns an `int'.  it probably won't hurt us
     if we get truncated on ia64, right? */

  TMSG(STATE,"--Init");
  memset(x, 0, sizeof(*x));

  return HPCRUN_OK;
}

/* csprof_state_alloc: Special initialization for items stored in
   private memory.  Private memory must be initialized!  Returns
   HPCRUN_OK upon success; HPCRUN_ERR on error. */
int
csprof_state_alloc(csprof_state_t *x, lush_cct_ctxt_t* thr_ctxt)
{
  TMSG(STATE,"--Alloc");
  csprof_cct__init(&x->csdata, thr_ctxt);

  x->epoch = csprof_get_epoch();
  x->csdata_ctxt = thr_ctxt;

#ifdef CSPROF_TRAMPOLINE_BACKEND
  x->pool = csprof_list_pool_new(32);
#if defined(CSPROF_LIST_BACKTRACE_CACHE)
  x->backtrace = csprof_list_new(x->pool);
#else
  TMSG(MALLOC," state_alloc TRAMP");
  x->btbuf = csprof_malloc(sizeof(csprof_frame_t)*32);
  x->bufend = x->btbuf + 32;
  x->bufstk = x->bufend;
  x->treenode = NULL;
#endif
#else
#if defined(CSPROF_LIST_BACKTRACE_CACHE)
  x->backtrace = csprof_list_new(x->pool);
#else
  TMSG(MALLOC," state_alloc btbuf (no TRAMP)");
  x->btbuf = csprof_malloc(sizeof(csprof_frame_t) * CSPROF_BACKTRACE_CACHE_INIT_SZ);
  x->bufend = x->btbuf + CSPROF_BACKTRACE_CACHE_INIT_SZ;
  x->bufstk = x->bufend;
  x->treenode = NULL;
#endif
#endif

  return HPCRUN_OK;
}

int csprof_state_fini(csprof_state_t *x){

  TMSG(STATE,"--Fini");
  return HPCRUN_OK;
}

csprof_cct_node_t*
csprof_state_insert_backtrace(csprof_state_t *state, int metric_id,
			      csprof_frame_t *path_beg,
			      csprof_frame_t *path_end,
			      cct_metric_data_t increment)
{
  csprof_cct_node_t* n;
  n = csprof_cct_insert_backtrace(&state->csdata, state->treenode,
				  metric_id, path_beg, path_end, increment);

  TMSG(CCT, "Treenode is %p", n);
  
  state->treenode = n;
  return n;
}

csprof_frame_t *
csprof_state_expand_buffer(csprof_state_t *state, csprof_frame_t *unwind){
  /* how big is the current buffer? */
  size_t sz = state->bufend - state->btbuf;
  size_t newsz = sz*2;
  /* how big is the current backtrace? */
  size_t btsz = state->bufend - state->bufstk;
  /* how big is the backtrace we're recording? */
  size_t recsz = unwind - state->btbuf;
  /* get new buffer */
  TMSG(MALLOC," state_expand_buffer");
  csprof_frame_t *newbt = csprof_malloc(newsz*sizeof(csprof_frame_t));

  if(state->bufstk > state->bufend) {
    EMSG("Invariant bufstk > bufend violated");
    monitor_real_abort();
  }

  /* copy frames from old to new */
  memcpy(newbt, state->btbuf, recsz*sizeof(csprof_frame_t));
  memcpy(newbt+newsz-btsz, state->bufend-btsz, btsz*sizeof(csprof_frame_t));

  /* setup new pointers */
  state->btbuf = newbt;
  state->bufend = newbt+newsz;
  state->bufstk = newbt+newsz-btsz;

  /* return new unwind pointer */
  return newbt+recsz;
}

/* csprof_state_free: Special finalization for items stored in
   private memory.  Private memory must be initialized!  Returns
   HPCRUN_OK upon success; HPCRUN_ERR on error. */
int
csprof_state_free(csprof_state_t *x){
  csprof_cct__fini(&x->csdata);

  // no need to free memory

  return HPCRUN_OK;
}
