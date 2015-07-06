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
#include <stdbool.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <ev.h>
#include "logger.h"
#include "nifty.h"

#if defined __INTEL_COMPILER
# define auto	static
#endif	/* __INTEL_COMPILER */

#undef EV_P
#define EV_P  struct ev_loop *loop __attribute__((unused))

typedef struct echs_task_s *echs_task_t;

/* linked list of ev_periodic objects */
struct echs_task_s {
	/* beef data for libev and book-keeping */
	ev_periodic w;
	echs_task_t next;

	/* beef data for the task in question */
	char *cmd;
	char **env;
};

struct _echsd_s {
	ev_signal sigint;
	ev_signal sighup;
	ev_signal sigterm;
	ev_signal sigpipe;

	struct ev_loop *loop;
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


/* task pool */
#define ECHS_TASK_POOL_INIZ	(256U)
static echs_task_t free_pers;
static size_t nfree_pers;
static size_t zfree_pers;

/* pool list */
struct plst_s {
	echs_task_t _1st;
	size_t size;
};

static struct plst_s *pools;
static size_t npools;
static size_t zpools;

static echs_task_t
make_task_pool(size_t n)
{
/* generate a pile of ev_periodics and chain them up */
	echs_task_t res;

	if (UNLIKELY((res = malloc(sizeof(*res) * n)) == NULL)) {
		return NULL;
	}
	/* chain them up */
	res[n - 1U].next = NULL;
	for (size_t i = n - 1U; i > 0; i--) {
		res[i - 1U].next = res + i;
	}
	/* also add res to the list of pools (for freeing them later) */
	if (npools >= zpools) {
		if (!(zpools *= 2U)) {
			zpools = 16U;
		}
		pools = realloc(pools, zpools * sizeof(*pools));
	}
	pools[npools]._1st = res;
	pools[npools].size = n;
	npools++;
	return res;
}

static void
free_task_pool(void)
{
	for (size_t i = 0U; i < npools; i++) {
		free(pools[i]._1st);
	}
	return;
}

static echs_task_t
make_task(void)
{
/* create one task */
	echs_task_t res;

	if (UNLIKELY(!nfree_pers)) {
		/* put some more task objects in the task pool */
		free_pers = make_task_pool(nfree_pers = zfree_pers ?: 256U);
		if (UNLIKELY(!(zfree_pers *= 2U))) {
			zfree_pers = 256U;
		}
	}
	/* pop off the free list */
	res = free_pers;
	free_pers = free_pers->next;
	return res;
}

static void
free_task(echs_task_t t)
{
/* hand task T over to free list */
	t->next = free_pers;
	free_pers = t;
	nfree_pers++;
	return;
}

static pid_t
run_task(const struct _echsd_s *ctx, echs_task_t t, bool dtchp)
{
	pid_t r;

	/* indicate that we might want to reuse the loop */
	ev_loop_fork(ctx->loop);

	switch ((r = vfork())) {
		int rc;

	case -1:
		ECHS_ERR_LOG("cannot fork: %s", strerror(errno));
		break;

	case 0:
		/* I am daddy's daughter */

		/* close standard socks */
		if (dtchp) {
			close(STDIN_FILENO);
			close(STDOUT_FILENO);
			close(STDERR_FILENO);
		}

		static char *args[] = {
			"echsx",
			"-c", NULL,
			"--stdout=/tmp/foo", "--stderr=/tmp/foo",
			"--mailto=freundt", "--mailfrom=freundt",
			"-n",
			NULL
		};
		args[2U] = t->cmd;
		if (dtchp) {
			args[7U] = NULL;
		}
		rc = execve("/home/freundt/devel/echse/=build/src/echsx", args, t->env);
		_exit(rc);
		/* not reached */

	default:
		/* I am daddy */
		while (waitpid(r, &rc, 0) != r);
		break;
	}
	return r;
}


/* callbacks */
static void
sigint_cb(EV_P_ ev_signal *UNUSED(w), int UNUSED(revents))
{
	ECHS_NOTI_LOG("C-c caught unrolling everything");
	ev_break(EV_A_ EVBREAK_ALL);
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
	ECHS_NOTI_LOG("SIGPIPE caught, doing nothing");
	return;
}

static void
tick_cb(EV_P_ ev_periodic *w, int UNUSED(revents))
{
	echs_task_t t = (void*)w;

	ECHS_NOTI_LOG("starting job");

	/* this one's fire and forget
	 * there's not much we can do anyway at this point, inside a callback */
	(void)run_task(w->data, t, false);

	if (w->reschedule_cb) {
		/* ah, we're going to be used again */
		return;
	}
	free_task(t);
	return;
}

static ev_tstamp
resched(ev_periodic *w, ev_tstamp now)
{
	ECHS_NOTI_LOG("next run %f", now + 10.);
	return now + 10.;
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

	if (res == NULL) {
		return NULL;
	} else if (loop == NULL) {
		goto foul;
	}

	/* initialise private bits */
	ev_signal_init(&res->sigint, sigint_cb, SIGINT);
	ev_signal_start(EV_A_ &res->sigint);
	ev_signal_init(&res->sighup, sighup_cb, SIGHUP);
	ev_signal_start(EV_A_ &res->sighup);
	ev_signal_init(&res->sigterm, sigint_cb, SIGTERM);
	ev_signal_start(EV_A_ &res->sigterm);
	ev_signal_init(&res->sigpipe, sigpipe_cb, SIGPIPE);
	ev_signal_start(EV_A_ &res->sigpipe);

	if_with (echs_task_t t = make_task(), t != NULL) {
		ev_periodic_init(&t->w, tick_cb, ev_now(EV_A), 0., NULL);
		ev_periodic_start(EV_A_ &t->w);
		t->w.data = res;
		t->cmd = "xz -vvk /tmp/shit";
		t->env = NULL;
	}

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
	if (LIKELY(ctx->loop != NULL)) {
		ev_break(ctx->loop, EVBREAK_ALL);
		ev_loop_destroy(ctx->loop);
	}
	free_task_pool();
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
