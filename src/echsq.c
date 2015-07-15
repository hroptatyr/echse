/*** echsq.c -- echse queue manager
 *
 * Copyright (C) 2013-2015 Sebastian Freundt
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
#if defined HAVE_CONFIG_H
# include "config.h"
#endif	/* HAVE_CONFIG_H */
#if defined HAVE_VERSION_H
# include "version.h"
#endif	/* HAVE_VERSION_H */
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/sendfile.h>
#include <sys/un.h>
#include <fcntl.h>
#include <pwd.h>
#include "nifty.h"

extern char **environ;

#if defined HAVE_SPLICE && !defined SPLICE_F_MOVE
/* just so we don't have to use _GNU_SOURCE declare prototype of splice() */
# if defined __INTEL_COMPILER
#  pragma warning(disable:1419)
# endif	/* __INTEL_COMPILER */
extern ssize_t splice(int, __off64_t*, int, __off64_t*, size_t, unsigned int);
# define SPLICE_F_MOVE	(1U)
# define SPLICE_F_MORE	(4U)
# if defined __INTEL_COMPILER
#  pragma warning(default:1419)
# endif	/* __INTEL_COMPILER */
#endif	/* HAVE_SPLICE && !SPLICE_F_MOVE */


static __attribute__((format(printf, 1, 2))) void
serror(const char *fmt, ...)
{
	va_list vap;
	va_start(vap, fmt);
	vfprintf(stderr, fmt, vap);
	va_end(vap);
	if (errno) {
		fputc(':', stderr);
		fputc(' ', stderr);
		fputs(strerror(errno), stderr);
	}
	fputc('\n', stderr);
	return;
}

static size_t
xstrlcpy(char *restrict dst, const char *src, size_t dsz)
{
	size_t ssz = strlen(src);
	if (ssz > dsz) {
		ssz = dsz - 1U;
	}
	memcpy(dst, src, ssz);
	dst[ssz] = '\0';
	return ssz;
}

static const char*
pathcat(const char *dirnm, ...)
{
	static char res[PATH_MAX];
	va_list ap;
	size_t ri = 0U;

	if (UNLIKELY(dirnm == NULL)) {
		return NULL;
	}

	va_start(ap, dirnm);
	ri += xstrlcpy(res + ri, dirnm, sizeof(res) - ri);
	for (const char *fn; (fn = va_arg(ap, const char*));) {
		res[ri++] = '/';
		ri += xstrlcpy(res + ri, fn, sizeof(res) - ri);
	}
	va_end(ap);
	return res;
}


static const char*
get_vardir(bool systemp)
{
	if (!systemp) {
		struct passwd *pw = getpwuid(getuid());
		return pw->pw_dir;
	}
	return "/var/run";
}

static int
make_conn(const char *path)
{
/* run LIST command on socket PATH */
	struct sockaddr_un sa = {AF_UNIX};
	int s;
	size_t z;

	if (UNLIKELY((s = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)) {
		return -1;
	}
	z = xstrlcpy(sa.sun_path, path, sizeof(sa.sun_path));
	z += sizeof(sa.sun_family);
	if (UNLIKELY(connect(s, (struct sockaddr*)&sa, z) < 0)) {
		goto fail;
	}
	return s;
fail:
	close(s);
	return -1;
}

static int
free_conn(int s)
{
	return close(s);
}


#if defined STANDALONE
#include "echsq.yucc"

static int
cmd_list(const struct yuck_cmd_list_s argi[static 1U])
{
	const char *dir;
	unsigned char q;
	ssize_t cmz;
	int s;

	/* let's try the local echsd and then the system-wide one */
	if (!(dir = pathcat(get_vardir(false), ".echse", "=echsd", NULL))) {
		goto next1;
	} else if ((s = make_conn(dir)) < 0) {
		serror("Error: cannot connect to `%s'", dir);
		goto next1;
	}
	/* which queue to request */
	if (argi->queue_arg) {
		q = (unsigned char)(*argi->queue_arg | 0x20U);
	} else {
		q = 'a';
	}
	/* otherwise try and request the queues */
	for (; q >= 'a' && q <= 'b';
	     q = (unsigned char)(argi->queue_arg ? '@' : q + 1U)) {
		char buf[4096U];
		char cmd[64];

		cmz = snprintf(cmd, sizeof(cmd),
			       "GET /echsq_%u%c.ics HTTP/1.1\r\n\r\n",
			       getuid(), q);
		write(s, cmd, cmz);

		for (ssize_t nrd; (nrd = read(s, buf, sizeof(buf))) > 0;) {
			write(STDOUT_FILENO, buf, nrd);
		}
	}
	free_conn(s);

next1:
	return 0;
}

int
main(int argc, char *argv[])
{
	/* command line options */
	yuck_t argi[1U];
	int rc = 0;

	if (yuck_parse(argi, argc, argv)) {
		rc = 1;
		goto out;
	}

	switch (argi->cmd) {
	case ECHSQ_CMD_NONE:
	case ECHSQ_CMD_LIST:
		/* with no command just list queues */
		rc = cmd_list((struct yuck_cmd_list_s*)argi);
		break;


	case ECHSQ_CMD_ADD:
		break;

	default:
		break;
	}
out:
	yuck_free(argi);
	return rc;
}
#endif	/* STANDALONE */

/* echsq.c ends here */
