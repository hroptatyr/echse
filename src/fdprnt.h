/*** fdprnt.h -- dprintf() et al but better
 *
 * Copyright (C) 2014-2015 Sebastian Freundt
 *
 * Author:  Sebastian Freundt <freundt@ga-group.nl>
 *
 * This file is part of echse.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the author nor the names of any contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ***/
#if !defined INCLUDED_fdprnt_h_
#define INCLUDED_fdprnt_h_
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include "nifty.h"

static struct {
	char buf[4096U];
	size_t bi;
	int fd;
} fd_aux;

static ssize_t
fdflush(void)
{
	ssize_t twr = 0;

	for (ssize_t nwr, tot = fd_aux.bi;
	     twr < tot &&
		     (nwr = write(fd_aux.fd, fd_aux.buf + twr, tot - twr)) > 0;
	     twr += nwr);
	fd_aux.bi = 0U;
	return twr;
}

static int
fdputc(int c)
{
	if (UNLIKELY(fd_aux.bi >= sizeof(fd_aux.buf))) {
		fdflush();
		/* just for coverity which couldn't figure this one out */
		fd_aux.bi = 0U;
	}
	fd_aux.buf[fd_aux.bi++] = (char)c;
	return 0;
}

static __attribute__((format(printf, 1, 2))) int
fdprintf(const char *fmt, ...)
{
/* like fprintf() (i.e. buffering) but write to FD. */
	int tp;

	va_list vap;
	va_start(vap, fmt);

	/* try and write */
	tp = vsnprintf(
		fd_aux.buf + fd_aux.bi, sizeof(fd_aux.buf) - fd_aux.bi,
		fmt, vap);
	if (UNLIKELY((size_t)tp >= sizeof(fd_aux.buf) - fd_aux.bi)) {
		/* yay, finally some write()ing */
		fdflush();
		/* ... try the formatting again */
		tp = vsnprintf(
			fd_aux.buf + fd_aux.bi, sizeof(fd_aux.buf) - fd_aux.bi,
			fmt, vap);
	}
	va_end(vap);

	/* reassign and out */
	if (UNLIKELY(tp < 0 || sizeof(fd_aux.buf) - tp < fd_aux.bi)) {
		/* we're fucked */
		return -1;
	}
	fd_aux.bi += tp;
	return 0;
}

static ssize_t
fdwrite(const char *str, size_t len)
{
	if (UNLIKELY(len > sizeof(fd_aux.buf))) {
		/* that's never going to work */
		return -1;
	} else if (UNLIKELY(fd_aux.bi + len >= sizeof(fd_aux.buf))) {
		/* yay, finally some write()ing */
		fdflush();
	}
	/* just memcpy the string */
	memcpy(fd_aux.buf + fd_aux.bi, str, len);
	fd_aux.bi += len;
	return len;
}

static int
fdbang(int fd)
{
	if (fd_aux.bi && fd_aux.fd != fd) {
		/* flush what we've got to the old fd */
		fdflush();
	}
	fd_aux.fd = fd;
	return 0;
}

#endif	/* INCLUDED_fdprnt_h_ */
