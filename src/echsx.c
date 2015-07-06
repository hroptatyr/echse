/*** echsx.c -- echse queue daemon
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
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <sys/wait.h>
#include "logger.h"
#include "nifty.h"
#include "echsx.yucc"

#if defined __INTEL_COMPILER
# define auto	static
#endif	/* __INTEL_COMPILER */

#undef EV_P
#define EV_P  struct ev_loop *loop __attribute__((unused))

typedef struct echs_task_s *echs_task_t;

/* linked list of ev_periodic objects */
struct echs_task_s {
	/* beef data for the task in question */
	const char *cmd;
	char **env;
};

static pid_t chld;
static yuck_t argi[1U];


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

static void
unblock_sig(int sig)
{
	static sigset_t unblk_set[1];

	sigemptyset(unblk_set);
	sigaddset(unblk_set, sig);
	(void)sigprocmask(SIG_UNBLOCK, unblk_set, (sigset_t*)NULL);
	return;
}

static void
timeo_cb(int UNUSED(signum))
{
	with (struct sigaction sa = {.sa_handler = SIG_DFL}) {
		sigaction(SIGALRM, &sa, NULL);
	}
	block_sigs();
	kill(chld, SIGALRM);
	return;
}

static int
set_timeout(unsigned int tdiff)
{
	if (UNLIKELY(tdiff == 0U)) {
		return -1;
	}

	/* set up new sigaction */
	with (struct sigaction sa = {.sa_handler = timeo_cb}) {
		if (sigaction(SIGALRM, &sa, NULL) < 0) {
			/* grrr nothing works */
			return -1;
		}
	}

	/* unblock just this one signal */
	unblock_sig(SIGALRM);
	return alarm(tdiff);
}

static const char*
getcwd_from_env(char *const *env)
{
	if (UNLIKELY(env == NULL)) {
		return NULL;
	}
	for (char *const *e = env; *e; e++) {
		if (!memcmp(*e, "PWD=", 4U)) {
			/* found him */
			return *e + 4U;
		}
	}
	return NULL;
}

static const char*
getsh_from_env(char *const *env)
{
	if (UNLIKELY(env == NULL)) {
		return NULL;
	}
	for (char *const *e = env; *e; e++) {
		if (!memcmp(*e, "SHELL=", 6U)) {
			/* found him */
			return *e + 6U;
		}
	}
	return NULL;
}

static int
init_iredir(const char *fn)
{
	const int fl = O_RDONLY;
	int fd;

	if ((fd = open(fn, fl)) < 0) {
		/* grrrr, are we supposed to proceed without? */
		ECHS_ERR_LOG("\
Error: cannot open file `%s' for child input: %s", fn, strerror(errno));
	}
	return fd;
}

static int
init_oredir(const char *fn)
{
	const int fl = O_WRONLY | O_TRUNC | O_CREAT;
	int fd;

	if ((fd = open(fn, fl, 0644)) < 0) {
		/* grrrr, are we supposed to proceed without? */
		ECHS_ERR_LOG("\
Error: cannot open file `%s' for child output: %s", fn, strerror(errno));
	}
	return fd;
}

static int
init_redirs(int tgt[static 3U])
{
	/* initialise somehow */
	memset(tgt, -1, 3U * sizeof(*tgt));

	if (argi->stdin_arg) {
		if ((tgt[0U] = init_iredir(argi->stdin_arg)) < 0) {
			return -1;
		}
	}

	if (argi->stdout_arg && argi->stderr_arg &&
	    !strcmp(argi->stdout_arg, argi->stderr_arg)) {
		/* caller wants stdout and stderr in the same file */
		tgt[1U] = tgt[2U] = init_oredir(argi->stdout_arg);
	} else {
		if (argi->stdout_arg) {
			tgt[1U] = init_oredir(argi->stdout_arg);
		}
		if (argi->stderr_arg) {
			tgt[2U] = init_oredir(argi->stderr_arg);
		}
	}
	return 0;
}

static int
xdup2(int olfd, int nufd)
{
	if (UNLIKELY(dup2(olfd, nufd) < 0)) {
		/* better close him, aye? */
		close(nufd);
		return -1;
	}
	return 0;
}


static int
run_task(echs_task_t t)
{
	const char *args[] = {"/bin/sh", "-c", t->cmd, NULL};
	int rc = -1;

	/* go to the pwd as specified */
	if_with (const char *pwd, (pwd = getcwd_from_env(t->env)) != NULL) {
		(void)chdir(pwd);
	}

	/* find out about what shell to start */
	if_with (const char *sh, (sh = getsh_from_env(t->env)) != NULL) {
		*args = sh;
	}

	ECHS_NOTI_LOG("starting `%s'", t->cmd);

	/* fork off the actual beef process */
	switch ((chld = vfork())) {
		int outfd[3];

	case -1:
		/* yeah bollocks */
		break;

	case 0:
		/* child */

		/* deal with the output */
		if (UNLIKELY(init_redirs(outfd) < 0)) {
			/* fuck, something's wrong */
			_exit(EXIT_FAILURE);
		}
		xdup2(outfd[0U], STDIN_FILENO);
		xdup2(outfd[1U], STDOUT_FILENO);
		xdup2(outfd[2U], STDERR_FILENO);

		rc = execve(*args, deconst(args), t->env);
		_exit(rc);
		/* not reached */

	default:
		/* parent */
		ECHS_NOTI_LOG("job %d", chld);

		while (waitpid(chld, &rc, 0) != chld);
		/* unset timeouts */
		alarm(0);
		ECHS_NOTI_LOG("process %d finished with %d", chld, rc);
		chld = 0;
		break;
	}
	return rc;
}

static int
daemonise(void)
{
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

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);
	return 0;
}


int
main(int argc, char *argv[])
{
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

	/* generate the task in question */
	struct echs_task_s t = {"/bin/sleep 30"};

	/* set up timeout */
	set_timeout(4);

	/* main loop */
	{
		/* set out sigs loose */
		unblock_sigs();
		/* and here we go */
		run_task(&t);
		/* not reached */
		block_sigs();
	}

	/* stop them log files */
	echs_closelog();

out:
	yuck_free(argi);
	return rc;
}

/* echsx.c ends here */
