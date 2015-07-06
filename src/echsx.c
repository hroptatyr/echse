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
#include <sys/stat.h>
#include <sys/resource.h>
#include "logger.h"
#include "nifty.h"
#include "echsx.yucc"

#if defined __INTEL_COMPILER
# define auto	static
#endif	/* __INTEL_COMPILER */

#undef EV_P
#define EV_P  struct ev_loop *loop __attribute__((unused))

#if !defined SPLICE_F_MOVE
/* just so we don't have to use _GNU_SOURCE declare prototype of splice() */
# if defined __INTEL_COMPILER
#  pragma warning(disable:1419)
# endif	/* __INTEL_COMPILER */
extern ssize_t splice(int, __off64_t*, int, __off64_t*, size_t, unsigned int);
# define SPLICE_F_MOVE	(0U)
# if defined __INTEL_COMPILER
#  pragma warning(default:1419)
# endif	/* __INTEL_COMPILER */
#endif	/* !SPLICE_F_MOVE */

typedef struct echs_task_s *echs_task_t;

/* linked list of ev_periodic objects */
struct echs_task_s {
	/* beef data for the task in question */
	const char *cmd;
	char **env;

	/* exit code */
	int xc;
	struct timespec t_sta, t_end;
	struct rusage rus;
};

static pid_t chld;
static yuck_t argi[1U];

static char *const _mcmd[] = {
	"sendmail",
	"-i", "-odi", "-oem", "-oi", "-t",
	NULL
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
	kill(chld, SIGXCPU);
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
get_env(const char *key, char *const *env)
{
	const size_t nkey = strlen(key);

	if (UNLIKELY(env == NULL)) {
		return NULL;
	}
	for (char *const *e = env; *e; e++) {
		const size_t ez = strlen(*e);

		if (ez < nkey + 1U) {
			continue;
		} else if (!memcmp(*e, key, nkey) && (*e)[nkey] == '=') {
			/* found him */
			return *e + nkey + 1U;
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

static size_t
strfts(char *restrict buf, size_t bsz, struct timespec t)
{
	time_t s = t.tv_sec;
	struct tm *tm = gmtime(&s);

	if (UNLIKELY(tm == NULL)) {
		return 0U;
	}
	return strftime(buf, bsz, "%FT%TZ", tm);
}

static int
mail_hdrs(int tgtfd, echs_task_t t)
{
	char buf[4096U];
	char *bp = buf;
	const char *const ep = buf + sizeof(buf);
	char tstmp1[32U], tstmp2[32U];
	long int dur_s;
	long int dur_n;

	strfts(tstmp1, sizeof(tstmp1), t->t_sta);
	strfts(tstmp2, sizeof(tstmp2), t->t_end);
	dur_s = t->t_end.tv_sec - t->t_sta.tv_sec;
	if ((dur_n = t->t_end.tv_nsec - t->t_sta.tv_nsec) < 0) {
		dur_n += 1000000000;
		dur_s--;
	}

	bp += snprintf(bp, ep - bp, "From: %s\n", argi->mailfrom_arg);
	for (size_t i = 0U; i < argi->mailto_nargs; i++) {
		bp += snprintf(bp, ep - bp, "To: %s\n", argi->mailto_args[i]);
	}
	bp += snprintf(bp, ep - bp, "\
Content-Type: text/plain\n\
X-Exit-Status: %d\n\
X-Job-Start: %s\n\
X-Job-End: %s\n",
		       WIFEXITED(t->xc) ? WEXITSTATUS(t->xc)
		       : WIFSIGNALED(t->xc) ? WTERMSIG(t->xc)
		       : -1, tstmp1, tstmp2);
	bp += snprintf(bp, ep - bp, "\
X-Job-Time: %ld.%06lis user  %ld.%06lis system  %ld.%09lis total\n",
		       t->rus.ru_utime.tv_sec, t->rus.ru_utime.tv_usec,
		       t->rus.ru_stime.tv_sec, t->rus.ru_stime.tv_usec,
		       dur_s, dur_n);
	*bp++ = '\n';

	for (ssize_t nwr, tot = bp - buf;
	     tot > 0 &&
		     (nwr = write(tgtfd, bp - tot, tot)) > 0; tot -= nwr);
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
xsplice(int tgtfd, int srcfd)
{
#if defined HAVE_SPLICE && 0
	struct stat st;
	off_t totz;

	if (fstat(srcfd, &st) < 0) {
		/* big bollocks */
		return -1;
	} else if ((totz = st.st_size) < 0) {
		/* hmmm, nice try */
		return -1;
	}

	for (ssize_t nsp, tots = 0;
	     tots < totz &&
		     (nsp = splice(srcfd, NULL,
				   tgtfd, NULL,
				   totz - tots, SPLICE_F_MOVE)) >= 0;
	     tots += nsp);
#elif defined HAVE_SENDFILE && 0

#else  /* !HAVE_SPLICE && !HAVE_SENDFILE */
	with (char *buf[4096U]) {
		ssize_t nrd;

		while ((nrd = read(srcfd, buf, sizeof(buf))) > 0) {
			for (ssize_t nwr, totw = 0;
			     totw < nrd &&
				     (nwr = write(
					      tgtfd,
					      buf + totw, nrd - totw)) >= 0;
			     totw += nwr);
		}
	}
#endif	/* HAVE_SPLICE || HAVE_SENDFILE */
	return 0;
}


static int
run_task(echs_task_t t)
{
	const char *args[] = {"/bin/sh", "-c", t->cmd, NULL};
	pid_t mpid;
	int mpip[2];
	int rc = 0;

	/* go to the pwd as specified */
	if_with (const char *pwd, (pwd = get_env("PWD", t->env)) != NULL) {
		(void)chdir(pwd);
	}

	/* find out about what shell to start */
	if_with (const char *sh, (sh = get_env("SHELL", t->env)) != NULL) {
		*args = sh;
	}

	ECHS_NOTI_LOG("starting `%s'", t->cmd);

	/* fork off the actual beef process */
	switch ((chld = vfork())) {
		int outfd[3];

	case -1:
		/* yeah bollocks */
		rc = -1;
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
		clock_gettime(CLOCK_REALTIME, &t->t_sta);

		while (waitpid(chld, &t->xc, 0) != chld);
		/* unset timeouts */
		alarm(0);
		ECHS_NOTI_LOG("process %d finished with %d", chld, t->xc);
		chld = 0;

		clock_gettime(CLOCK_REALTIME, &t->t_end);
		getrusage(RUSAGE_CHILDREN, &t->rus);
		break;
	}

	/* spawn sendmail */
	if (argi->mailfrom_arg == NULL || !argi->mailto_nargs) {
		return rc;
	} else if (pipe(mpip) < 0) {
		/* shit, better fuck off? */
		return rc;
	}

	switch ((mpid = vfork())) {
		int fd;

	case -1:
		/* yeah bollocks */
		rc = -1;
		break;

	case 0:
		/* child */
		close(mpip[1U]);
		xdup2(mpip[0U], STDIN_FILENO);
		rc = execve("/usr/sbin/sendmail", _mcmd, t->env);
		_exit(rc);
		/* not reached */

	default:
		/* parent */
		close(mpip[0U]);
		mail_hdrs(mpip[1U], t);

		if (argi->stdout_arg && argi->stderr_arg &&
		    !strcmp(argi->stdout_arg, argi->stderr_arg)) {
			/* joint file */
			const int fl = O_RDONLY;
			const char *fn = argi->stdout_arg;

			if ((fd = open(fn, fl)) < 0) {
				/* fuck */
				goto send;
			}
			xsplice(mpip[1U], fd);
			close(fd);
		} else {
			if (argi->stdout_arg) {
				const int fl = O_RDONLY;

				if ((fd = open(argi->stdout_arg, fl)) < 0) {
					/* shit */
					goto send;
				}
				xsplice(mpip[1U], fd);
				close(fd);
				if ((fd = open(argi->stderr_arg, fl)) < 0) {
					/* even more shit */
					goto send;
				}
				xsplice(mpip[1U], fd);
				close(fd);
			}
		}

	send:
		/* send off the mail */
		close(mpip[1U]);

		while (waitpid(mpid, &rc, 0) != mpid);
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
