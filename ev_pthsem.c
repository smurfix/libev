/*
 * libev pthsem fd activity backend
 *
 * Copyright 2016 Matthias Urlichs <matthias@urlichs.de>
 *
 * based on: fd_select.c
 *
 * Copyright (c) 2007,2008,2009,2010,2011 Marc Alexander Lehmann <libev@schmorp.de>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modifica-
 * tion, are permitted provided that the following conditions are met:
 *
 *   1.  Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *
 *   2.  Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MER-
 * CHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPE-
 * CIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTH-
 * ERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Alternatively, the contents of this file may be used under the terms of
 * the GNU General Public License ("GPL") version 2 or any later version,
 * in which case the provisions of the GPL are applicable instead of
 * the above. If you wish to allow the use of your version of this file
 * only under the terms of the GPL and not to allow others to use your
 * version of this file under the BSD license, indicate your decision
 * by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL. If you do not delete the
 * provisions above, a recipient may use your version of this file under
 * either the BSD or the GPL.
 */

#include <inttypes.h>
#include <sys/select.h>
#include <string.h>

#include <pthsem.h>

static void
pthsem_modify (EV_P_ int fd, int oev, int nev)
{
  if (oev == nev)
    return;

  {
    assert (("libev: fd >= FD_SETSIZE passed to pthsem backend", fd < FD_SETSIZE));

    if (nev & EV_READ)
      FD_SET (fd, (fd_set *)vec_ri);
    else
      FD_CLR (fd, (fd_set *)vec_ri);

    if (nev & EV_WRITE)
      FD_SET (fd, (fd_set *)vec_wi);
    else
      FD_CLR (fd, (fd_set *)vec_wi);
  }
}

static void
pthsem_poll (EV_P_ ev_tstamp timeout)
{
  struct timeval tv;
  int res;
  int fd_setsize;

  EV_RELEASE_CB;
  EV_TV_SET (tv, timeout);

  fd_setsize = sizeof (fd_set);

  memcpy (vec_ro, vec_ri, fd_setsize);
  memcpy (vec_wo, vec_wi, fd_setsize);

  fd_setsize = anfdmax < FD_SETSIZE ? anfdmax : FD_SETSIZE;

  pth_event (PTH_EVENT_SELECT | PTH_MODE_REUSE, pthsem_event, &res, fd_setsize + 1, &vec_ro, &vec_wo, NULL);
  pth_event (PTH_EVENT_RTIME | PTH_MODE_REUSE, pthsem_timeout, pth_time (tv.tv_sec, tv.tv_usec));
  pth_event_concat (pthsem_event, pthsem_timeout, NULL);
  pth_wait (pthsem_event);
  pth_event_isolate (pthsem_timeout);

  EV_ACQUIRE_CB;
  if (expect_false (res < 0))
    {
      if (errno == EBADF)
        fd_ebadf (EV_A);
      else if (errno == ENOMEM && !syserr_cb)
        fd_enomem (EV_A);
      else if (errno != EINTR)
        ev_syserr ("(libev) select");

      return;
    }


  {
    int fd;

    for (fd = 0; fd < anfdmax; ++fd)
      if (anfds [fd].events)
        {
          int events = 0;
          int handle = fd;

          if (FD_ISSET (handle, (fd_set *)vec_ro)) events |= EV_READ;
          if (FD_ISSET (handle, (fd_set *)vec_wo)) events |= EV_WRITE;

          if (expect_true (events))
            fd_event (EV_A_ fd, events);
        }
  }

}

inline_size
int
pthsem_init (EV_P_ int flags)
{
  backend_mintime = 1e-6;
  backend_modify  = pthsem_modify;
  backend_poll    = pthsem_poll;

  /* dummy values, for now */
  pthsem_event = pth_event (PTH_EVENT_FD,0);
  pthsem_timeout = pth_event (PTH_EVENT_FD,0);

  vec_ri  = ev_malloc (sizeof (fd_set)); FD_ZERO ((fd_set *)vec_ri);
  vec_ro  = ev_malloc (sizeof (fd_set));
  vec_wi  = ev_malloc (sizeof (fd_set)); FD_ZERO ((fd_set *)vec_wi);
  vec_wo  = ev_malloc (sizeof (fd_set));

  return EVBACKEND_SELECT;
}

inline_size
void
pthsem_destroy (EV_P)
{
  pth_event_free (pthsem_event, PTH_FREE_THIS);
  pth_event_free (pthsem_timeout, PTH_FREE_THIS);

  ev_free (vec_ri);
  ev_free (vec_ro);
  ev_free (vec_wi);
  ev_free (vec_wo);
}

