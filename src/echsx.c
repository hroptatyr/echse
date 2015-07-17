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
#include <limits.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/resource.h>
#if defined HAVE_GRP_H
# include <grp.h>
#endif	/* HAVE_GRP_H */
#if defined HAVE_SENDFILE
# include <sys/sendfile.h>
#endif	/* HAVE_SENDFILE */
#include "logger.h"
#include "nifty.h"
#include "echsx.yucc"

#if defined __INTEL_COMPILER
# define auto	static
#endif	/* __INTEL_COMPILER */

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

#define USER_AGENT	"echsx/" VERSION " (echse job execution agent)"

typedef struct echs_task_s *echs_task_t;

struct echs_task_s {
	/* beef data for the task in question */
	const char *cmd;
	char **env;

	/* exit code */
	int xc;
	struct timespec t_sta, t_end;
	struct rusage rus;
	struct rusage rus_off;
};

static pid_t chld;
static yuck_t argi[1U];

static char *const _mcmd[] = {
	"sendmail",
	"-i", "-odi", "-oem", "-oi", "-t",
	NULL
};


static struct {
	char buf[4096U];
	size_t bi;
	int fd;
} fd_aux;

static int
fdbang(int fd)
{
	fd_aux.fd = fd;
	return 0;
}

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

static struct ts_dur_s {
	long int s;
	long int n;
} ts_dur(struct timespec sta, struct timespec end)
{
	struct ts_dur_s res = {
		.s = end.tv_sec - sta.tv_sec,
		.n = end.tv_nsec - sta.tv_nsec,
	};
	if (res.n < 0) {
		res.s--;
		res.n += 1000000000;
	}
	return res;
}

static struct tv_dur_s {
	long int s;
	long int u;
} tv_dur(struct timeval sta, struct timeval end)
{
	struct tv_dur_s res = {
		.s = end.tv_sec - sta.tv_sec,
		.u = end.tv_usec - sta.tv_usec,
	};
	if (res.u < 0) {
		res.s--;
		res.u += 1000000;
	}
	return res;
}

static int
mail_hdrs(int tgtfd, echs_task_t t)
{
	char tstmp1[32U], tstmp2[32U];
	struct ts_dur_s real;
	struct tv_dur_s user;
	struct tv_dur_s sys;
	double cpu;

	strfts(tstmp1, sizeof(tstmp1), t->t_sta);
	strfts(tstmp2, sizeof(tstmp2), t->t_end);
	real = ts_dur(t->t_sta, t->t_end);
	user = tv_dur(t->rus_off.ru_utime, t->rus.ru_utime);
	sys = tv_dur(t->rus_off.ru_stime, t->rus.ru_stime);
	cpu = ((double)(user.s + sys.s) + (double)(user.u + sys.u) * 1.e-6) /
		((double)real.s + (double)real.n * 1.e-9) * 100.;

	fdbang(tgtfd);
	if (argi->mailfrom_arg) {
		fdprintf("From: %s\n", argi->mailfrom_arg);
	}
	for (size_t i = 0U; i < argi->mailto_nargs; i++) {
		fdprintf("To: %s\n", argi->mailto_args[i]);
	}
	if (argi->mailfrom_arg) {
		fdprintf("Content-Type: text/plain\n");
		fdprintf("User-Agent: " USER_AGENT "\n");
	}
	fdprintf("Subject: %s\n", t->cmd);

	if (WIFEXITED(t->xc)) {
		fdprintf("\
X-Exit-Status: %d\n", WEXITSTATUS(t->xc));
	} else if (WIFSIGNALED(t->xc)) {
		int sig = WTERMSIG(t->xc);
		fdprintf("\
X-Exit-Status: %s (signal %d)\n", strsignal(sig), sig);
	}

	fdprintf("\
X-Job-Start: %s\n\
X-Job-End: %s\n",
		       tstmp1, tstmp2);
	fdprintf("\
X-Job-Time: %ld.%06lis user  %ld.%06lis system  %.2f%% cpu  %ld.%09li total\n",
		       user.s, user.u, sys.s, sys.u, cpu, real.s, real.n);
	fdprintf("X-Job-Memory: %ldkB\n", t->rus.ru_maxrss);
	fdputc('\n');
	fdflush();
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
#if defined HAVE_SPLICE || defined HAVE_SENDFILE
	struct stat st;
	off_t totz;

	if (fstat(srcfd, &st) < 0) {
		/* big bollocks */
		ECHS_ERR_LOG("cannot stat file to splice: %s", strerror(errno));
		return -1;
	} else if ((totz = st.st_size) < 0) {
		/* hmmm, nice try */
		ECHS_ERR_LOG("cannot obtain size of file to splice");
		return -1;
	}
#endif	/* HAVE_SPLICE || HAVE_SENDFILE */

#if defined HAVE_SPLICE
	for (ssize_t nsp, tots = 0;
	     tots < totz &&
		     (nsp = splice(srcfd, NULL,
				   tgtfd, NULL,
				   totz - tots, SPLICE_F_MOVE)) >= 0;
	     tots += nsp);
#elif defined HAVE_SENDFILE
	for (ssize_t nsf, tots = 0;
	     tots < totz &&
		     (nsf = sendfile(tgtfd, srcfd, NULL, totz - tots)) >= 0;
	     tots += nsf);
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
		ECHS_NOTI_LOG("starting `%s' -> process %d", t->cmd, chld);
		clock_gettime(CLOCK_REALTIME, &t->t_sta);
		getrusage(RUSAGE_SELF, &t->rus_off);

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
		/* no mail */
		mail_hdrs(STDOUT_FILENO, t);
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

	/* switch to user/group early */
	if (argi->gid_arg) {
		long unsigned int g = strtoul(argi->gid_arg, NULL, 10);
		gid_t supgs[1UL] = {(gid_t)g};

		if (g > (gid_t)~0UL ||
#if defined HAVE_GRP_H
		    setgroups(countof(supgs), supgs) < 0 ||
#endif	/* HAVE_GRP_H */
		    setgid((gid_t)g) < 0) {
			perror("Error: cannot set group id");
			rc = 1;
			goto out;
		}
	}
	if (argi->uid_arg) {
		long unsigned int u = strtoul(argi->uid_arg, NULL, 10);

		if (u > (uid_t)~0UL ||
		    setuid((uid_t)u) < 0) {
			perror("Error: cannot set user id");
			rc = 1;
			goto out;
		}
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

	if (!argi->command_arg) {
		ECHS_ERR_LOG("no command string given");
		rc = 1;
		goto out;
	}

	if (argi->timeout_arg) {
		/* set up timeout */
		long int x = strtol(argi->timeout_arg, NULL, 0);

		if (x < 0 || x >= INT_MAX) {
			ECHS_ERR_LOG("timeout out of range");
		} else if (set_timeout((unsigned int)x) < 0) {
			ECHS_ERR_LOG("\
cannot set timeout, job execution will be unbounded");
		}
	}

	/* main loop */
	with (struct echs_task_s t = {argi->command_arg}) {
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
