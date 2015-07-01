/*** echsd.c -- echse queue daemon
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
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <ev.h>
#include "logger.h"
#include "nifty.h"

#if defined __INTEL_COMPILER
# define auto	static
#endif	/* __INTEL_COMPILER */

#undef EV_P
#define EV_P  struct ev_loop *loop __attribute__((unused))

struct _echsd_s {
	ev_signal sigint;
	ev_signal sighup;
	ev_signal sigterm;
	ev_signal sigpipe;

	struct ev_loop *loop;

	ev_io sock;
};


static void
block_sigs(void)
{
	sigset_t fatal_signal_set[1];

	sigemptyset(fatal_signal_set);
	sigaddset(fatal_signal_set, SIGHUP);
	sigaddset(fatal_signal_set, SIGQUIT);
	sigaddset(fatal_signal_set, SIGINT);
	sigaddset(fatal_signal_set, SIGTERM);
	sigaddset(fatal_signal_set, SIGXCPU);
	sigaddset(fatal_signal_set, SIGXFSZ);
	(void)sigprocmask(SIG_BLOCK, fatal_signal_set, (sigset_t*)NULL);
	return;
}

static void
unblock_sigs(void)
{
	sigset_t empty_signal_set[1];

	sigemptyset(empty_signal_set);
	sigprocmask(SIG_SETMASK, empty_signal_set, (sigset_t*)NULL);
	return;
}


/* callbacks */
static void
sigint_cb(EV_P_ ev_signal *UNUSED(w), int UNUSED(revents))
{
	ECHS_NOTI_LOG("C-c caught unrolling everything");
	ev_unloop(EV_A_ EVUNLOOP_ALL);
	return;
}

static void
sighup_cb(EV_P_ ev_signal *UNUSED(w), int UNUSED(revents))
{
	ECHS_NOTI_LOG("SIGHUP caught, doing nothing");
	return;
}

static void
sigpipe_cb(EV_P_ ev_signal *UNUSED(w), int UNUSED(revents))
{
	ECHS_NOTI_LOG("SIGPIPE caught, checking connections ...");
	return;
}


static int
daemonise(void)
{
	int fd;
	pid_t pid;

	switch (pid = fork()) {
	case -1:
		return -1;
	case 0:
		/* i am the child */
		break;
	default:
		/* i am the parent */
		exit(0);
	}

	if (setsid() == -1) {
		return -1;
	}
	if (LIKELY((fd = open("/dev/null", O_RDWR, 0)) >= 0)) {
		(void)dup2(fd, STDIN_FILENO);
		(void)dup2(fd, STDOUT_FILENO);
		(void)dup2(fd, STDERR_FILENO);
		if (fd > STDERR_FILENO) {
			(void)close(fd);
		}
	}
	return 0;
}

static void
echsd_run(const struct _echsd_s *ctx)
{
	ECHS_NOTI_LOG("echsd ready");
	ev_loop(ctx->loop, 0);
	ECHS_NOTI_LOG("echsd exitting ...");
	return;
}

static struct _echsd_s*
make_echsd(void)
{
	struct ev_loop *loop = ev_default_loop(EVFLAG_AUTO);
	struct _echsd_s *res = calloc(1, sizeof(*res));

	/* initialise private bits */
	ev_signal_init(&res->sigint, sigint_cb, SIGINT);
	ev_signal_start(EV_A_ &res->sigint);
	ev_signal_init(&res->sighup, sighup_cb, SIGHUP);
	ev_signal_start(EV_A_ &res->sighup);
	ev_signal_init(&res->sigterm, sigint_cb, SIGTERM);
	ev_signal_start(EV_A_ &res->sigterm);
	ev_signal_init(&res->sigpipe, sigpipe_cb, SIGPIPE);
	ev_signal_start(EV_A_ &res->sigpipe);
	res->loop = loop;
	return res;

foul:
	free(res);
	return NULL;
}

static void
free_echsd(struct _echsd_s *ctx)
{
	if (UNLIKELY(ctx == NULL)) {
		return;
	}
	free(ctx);
	return;
}


#include "echsd.yucc"

int
main(int argc, char *argv[])
{
	yuck_t argi[1U];
	struct _echsd_s *ctx;
	int rc = 0;

	/* best not to be signalled for a minute */
	block_sigs();

	if (yuck_parse(argi, argc, argv) < 0) {
		rc = 1;
		goto out;
	}

	if (argi->foreground_flag) {
		echs_log = echs_errlog;
	} else if (daemonise() < 0) {
		perror("Error: daemonisation failed");
		rc = 1;
		goto out;
	}

	/* start them log files */
	echs_openlog();

	/* obtain our context */
	if (UNLIKELY((ctx = make_echsd()) == NULL)) {
		ECHS_ERR_LOG("cannot instantiate echsd context");
		goto clo;
	}

	/* main loop */
	{
		/* set out sigs loose */
		unblock_sigs();
		/* and here we go */
		echsd_run(ctx);
		/* not reached */
		block_sigs();
	}

	free_echsd(ctx);

clo:
	/* stop them log files */
	echs_closelog();

out:
	yuck_free(argi);
	return rc;
}

/* echsd.c ends here */
