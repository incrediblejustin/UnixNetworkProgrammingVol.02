/* Copyright (C) 2003-2012 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Martin Schwidefsky <schwidefsky@de.ibm.com>, 2003.
   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.
   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
   Lesser General Public License for more details.
   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <http://www.gnu.org/licenses/>.  */

#include <endian.h>
#include <errno.h>
#include <sysdep.h>
#include <lowlevellock.h>
#include <pthread.h>
#include <pthreadP.h>

#include <shlib-compat.h>
#include <stap-probe.h>


struct _condvar_cleanup_buffer
{
  int oldtype;
  pthread_cond_t *cond;
  pthread_mutex_t *mutex;
  unsigned int bc_seq;
};


void
__attribute__ ((visibility ("hidden")))
__condvar_cleanup (void *arg)
{
  struct _condvar_cleanup_buffer *cbuffer =
    (struct _condvar_cleanup_buffer *) arg;
  unsigned int destroying;
  int pshared = (cbuffer->cond->__data.__mutex == (void *) ~0l)
		? LLL_SHARED : LLL_PRIVATE;

  /* We are going to modify shared data.  */
  lll_lock (cbuffer->cond->__data.__lock, pshared);

  if (cbuffer->bc_seq == cbuffer->cond->__data.__broadcast_seq)
    {
      /* This thread is not waiting anymore.  Adjust the sequence counters
	 appropriately.  We do not increment WAKEUP_SEQ if this would
	 bump it over the value of TOTAL_SEQ.  This can happen if a thread
	 was woken and then canceled.  */
      //如果有线程等待被唤醒
      if (cbuffer->cond->__data.__wakeup_seq
	  < cbuffer->cond->__data.__total_seq)
	{
    //唤醒一个线程
	  ++cbuffer->cond->__data.__wakeup_seq;
	  ++cbuffer->cond->__data.__futex;
	}
      ++cbuffer->cond->__data.__woken_seq;
    }

  cbuffer->cond->__data.__nwaiters -= 1 << COND_NWAITERS_SHIFT;

  /* If pthread_cond_destroy was called on this variable already,
     notify the pthread_cond_destroy caller all waiters have left
     and it can be successfully destroyed.  */
  destroying = 0;
  if (cbuffer->cond->__data.__total_seq == -1ULL
      && cbuffer->cond->__data.__nwaiters < (1 << COND_NWAITERS_SHIFT))
    {
      lll_futex_wake (&cbuffer->cond->__data.__nwaiters, 1, pshared);
      destroying = 1;
    }

  /* We are done.  */
  lll_unlock (cbuffer->cond->__data.__lock, pshared);

  /* Wake everybody to make sure no condvar signal gets lost.  */
  if (! destroying)
    lll_futex_wake (&cbuffer->cond->__data.__futex, INT_MAX, pshared);

  /* Get the mutex before returning unless asynchronous cancellation
     is in effect.  */
  __pthread_mutex_cond_lock (cbuffer->mutex);
}


int
__pthread_cond_wait (cond, mutex)
     pthread_cond_t *cond;
     pthread_mutex_t *mutex;
{
  struct _pthread_cleanup_buffer buffer;
  struct _condvar_cleanup_buffer cbuffer;
  int err;

  //条件变量是否被共享
  int pshared = (cond->__data.__mutex == (void *) ~0l)
		? LLL_SHARED : LLL_PRIVATE;

  LIBC_PROBE (cond_wait, 2, cond, mutex);

//加锁，如果被共享的话
  lll_lock (cond->__data.__lock, pshared);

//给传入的锁 mutex 解锁
  err = __pthread_mutex_unlock_usercnt (mutex, 0);
  if (__builtin_expect (err, 0))
    {
      //如果解锁失败，只能给条件变量的锁解锁，然后返回错误
      lll_unlock (cond->__data.__lock, pshared);
      return err;
    }

//条件变量的使用者 + 1
  ++cond->__data.__total_seq;
  ++cond->__data.__futex;
  cond->__data.__nwaiters += 1 << COND_NWAITERS_SHIFT;

  /* Remember the mutex we are using here.  If there is already a
     different address store this is a bad user bug.  Do not store
     anything for pshared condvars.  */
//如果条件变量是私有的，就把传入的锁交给条件变量的锁
  if (cond->__data.__mutex != (void *) ~0l)
    cond->__data.__mutex = mutex;

  /* Prepare structure passed to cancellation handler.  */
  cbuffer.cond = cond;
  cbuffer.mutex = mutex;

  /* Before we block we enable cancellation.  Therefore we have to
     install a cancellation handler.  */
  __pthread_cleanup_push (&buffer, __condvar_cleanup, &cbuffer);

  /* The current values of the wakeup counter.  The "woken" counter
     must exceed this value.  */
  unsigned long long int val;
  unsigned long long int seq;
  val = seq = cond->__data.__wakeup_seq;
  /* Remember the broadcast counter.  */
  cbuffer.bc_seq = cond->__data.__broadcast_seq;

  do
    {
      unsigned int futex_val = cond->__data.__futex;

      /*准备进入等待，先对条件变量解锁*/
      lll_unlock (cond->__data.__lock, pshared);

      /* Enable asynchronous cancellation.  Required by the standard.  */
      cbuffer.oldtype = __pthread_enable_asynccancel ();

      /* 开始等待， 直到被 signal 或者 broadcast 唤醒 */
      lll_futex_wait (&cond->__data.__futex, futex_val, pshared);

      /* Disable asynchronous cancellation.  */
      __pthread_disable_asynccancel (cbuffer.oldtype);

      /*//加锁，如果被共享的话*/
      lll_lock (cond->__data.__lock, pshared);

      /*如果现在的broadcast值与之前的不同，说明线程是被broadcast唤醒*/
      if (cbuffer.bc_seq != cond->__data.__broadcast_seq)
	goto bc_out;

      /* 将被唤醒后的值赋给val，以便检查是否与被唤醒前相同*/
      val = cond->__data.__wakeup_seq;
    }
  //如果不同就说明被唤醒了， 否则继续就是继续解锁等待再次被唤醒
  while (val == seq || cond->__data.__woken_seq == val);

  /*被唤醒的线程数量 */
  ++cond->__data.__woken_seq;

 bc_out://如果是被broadcast唤醒

  cond->__data.__nwaiters -= 1 << COND_NWAITERS_SHIFT;

  /* If pthread_cond_destroy was called on this varaible already,
     notify the pthread_cond_destroy caller all waiters have left
     and it can be successfully destroyed.  */
  if (cond->__data.__total_seq == -1ULL
      && cond->__data.__nwaiters < (1 << COND_NWAITERS_SHIFT))
    lll_futex_wake (&cond->__data.__nwaiters, 1, pshared);

  /*唤醒后条件变量已经检查完， 给它解锁 */
  lll_unlock (cond->__data.__lock, pshared);

  /* The cancellation handling is back to normal, remove the handler.  */
  __pthread_cleanup_pop (&buffer, 0);

  /*再返回前给传入的锁mutex加锁*/
  return __pthread_mutex_cond_lock (mutex);
}

versioned_symbol (libpthread, __pthread_cond_wait, pthread_cond_wait,
		  GLIBC_2_3_2);