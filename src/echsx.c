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
#include <spawn.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/resource.h>
#if defined HAVE_SENDFILE
# include <sys/sendfile.h>
#endif	/* HAVE_SENDFILE */
#if defined HAVE_NET_PROTO_UIPC_H
# include <net/proto_uipc.h>
#endif	/* HAVE_NET_PROTO_UIPC_H */
#include <ev.h>
#include <assert.h>
#include "dt-strpf.h"
#include "logger.h"
#include "nifty.h"
#include "fdprnt.h"
#include "echsx.yucc"

#if defined __INTEL_COMPILER
# define auto	static
#endif	/* __INTEL_COMPILER */

#if defined HAVE_SPLICE && !defined SPLICE_F_MOVE && !defined _AIX
/* just so we don't have to use _GNU_SOURCE declare prototype of splice() */
# if defined __INTEL_COMPILER
#  pragma warning(disable:1419)
# endif	/* __INTEL_COMPILER */
extern ssize_t tee(int, int, size_t, unsigned int);
extern ssize_t splice(int, __off64_t*, int, __off64_t*, size_t, unsigned int);
# define SPLICE_F_MOVE	(0U)
# if defined __INTEL_COMPILER
#  pragma warning(default:1419)
# endif	/* __INTEL_COMPILER */
#endif	/* !SPLICE_F_MOVE */

#undef EV_P
#define EV_P  struct ev_loop *loop __attribute__((unused))

#define USER_AGENT	"echsx/" VERSION " (echse job execution agent)"
#define STRERR		(strerror(errno))

typedef struct echs_task_s *echs_task_t;

struct echs_task_s {
	/* beef data for the task in question */
	const char *cmd;
	char **env;

	/* them descriptors and file names prepped for us */
	int ifd;
	int ofd;
	int efd;
	int mfd;

	/* this is for the complicated cases where we need pipes */
	int opip;
	int epip;
	int teeo;
	int teee;

	const char *mfn;
	unsigned int mrm:1U;

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

static inline size_t
xstrlncpy(char *restrict dst, size_t dsz, const char *src, size_t ssz)
{
	if (UNLIKELY(ssz > dsz)) {
		ssz = dsz - 1U;
	}
	memcpy(dst, src, ssz);
	dst[ssz] = '\0';
	return ssz;
}


static int
mail_hdrs(int tgtfd, echs_task_t t)
{
	char tstmp1[32U], tstmp2[32U];
	struct ts_dur_s real;
	struct tv_dur_s user;
	struct tv_dur_s sys;
	double cpu;

	dt_strf(tstmp1, sizeof(tstmp1), epoch_to_echs_instant(t->t_sta.tv_sec));
	dt_strf(tstmp2, sizeof(tstmp2), epoch_to_echs_instant(t->t_end.tv_sec));
	real = ts_dur(t->t_sta, t->t_end);
	user = tv_dur((struct timeval){0}, t->rus.ru_utime);
	sys = tv_dur((struct timeval){0}, t->rus.ru_stime);
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
xsplice(int tgtfd, int srcfd)
{
#if defined HAVE_SENDFILE
	struct stat st;
	off_t totz;

	if (fstat(srcfd, &st) < 0) {
		/* big bollocks */
		ECHS_ERR_LOG("cannot stat file to splice: %s", STRERR);
		return -1;
	} else if ((totz = st.st_size) < 0) {
		/* hmmm, nice try */
		ECHS_ERR_LOG("cannot obtain size of file to splice");
		return -1;
	}

	for (ssize_t nsf, tots = 0;
	     tots < totz &&
		     (nsf = sendfile(tgtfd, srcfd, NULL, totz - tots)) >= 0;
	     tots += nsf);

#else  /* !HAVE_SENDFILE */
	with (char *buf[16U * 4096U]) {
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
#endif	/* HAVE_SENDFILE */
	return 0;
}


/* callbacks for run task */
struct data_s {
	ev_io w;

	int mailfd;
	int filefd;
};

static void
chld_cb(EV_P_ ev_child *c, int UNUSED(revents))
{
	echs_task_t t = c->data;
	t->xc = c->rstatus;
	ev_child_stop(EV_A_ c);
	ev_break(EV_A_ EVBREAK_ALL);
	return;
}

static void
data_cb(EV_P_ ev_io *w, int UNUSED(revents))
{
	int cfd = w->fd;
	struct data_s *out = (void*)w;

#if defined HAVE_SPLICE
	const unsigned int fl = SPLICE_F_MOVE;
	int mfd = out->mailfd;
	ssize_t nsp;

	/* here we move things to out->mailfd, being certain it's a
	 * descriptor where mmap-like opers work on, then, in the
	 * next step we use sendfile(2) to push it to out->filed if
	 * applicable */
	if (0) {
		;
# if defined _AIX
	} else if (splice(cfd, mfd, fl) < 0) {
		/* nothing workee */
		goto shut;
# else	/* !_AIX */
	} else if ((nsp = splice(cfd, NULL, mfd, NULL, UINT_MAX, fl)) <= 0) {
		/* nothing works, does it */
		goto shut;
# endif	 /* _AIX */
	} else if (out->filefd >= 0) {
		const int ofd = out->filefd;
		off_t mfo = lseek(mfd, 0, SEEK_CUR);

		assert(mfo >= nsp);
		mfo -= nsp;
# if defined HAVE_SENDFILE
		(void)sendfile(ofd, mfd, &mfo, nsp);

# else  /* !HAVE_SENDFILE */
		char buf[16U * 4096U];

		for (ssize_t nrd;
		     (nrd = pread(mfd, buf, sizeof(buf), mfo)) > 0;
		     mfo += nrd) {
			for (ssize_t nwr, tot = 0;
			     tot < nrd &&
				     (nwr = write(
					      ofd, buf + tot, nrd - tot)) > 0;
			     tot += nwr);
		}
# endif	/* HAVE_SENDFILE */
	}
#else  /* !HAVE_SPLICE */
/* do it the old-fashioned way */
	char buf[16U * 4096U];
	ssize_t nrd;
	int ofd;

	if ((nrd = read(cfd, buf, sizeof(buf))) <= 0) {
		goto shut;
	} else if ((ofd = out->filefd) >= 0) {
		/* `tee'ing to the out file */
		for (ssize_t nwr, tot = 0;
		     tot < nrd && (nwr = write(ofd, buf + tot, nrd - tot)) > 0;
		     tot += nwr);
	}
	/* definitely write stuff to mail fd though */
	ofd = out->mailfd;
	for (ssize_t nwr, tot = 0;
	     tot < nrd && (nwr = write(ofd, buf + tot, nrd - tot)) > 0;
	     tot += nwr);
#endif	/* HAVE_SPLICE */
	return;

shut:
	ev_io_stop(EV_A_ w);
	close(cfd);
	return;
}


static int
prep_task(echs_task_t t)
{
/* we've got those 20 combinations between
 * --mailout (Mo), --mailerr (Me) --stdout (So), stderr (Se)
 *
 *      So  Se   Mo  Me
 *  1   0   0   [*] [*]   ofd = efd = mfd = X                rm X  .
 *  2   0   0   [*] [ ]   ofd = mfd = X     efd = /dev/null  rm X  .
 *  3   0   0   [ ] [*]   ofd = /dev/null   efd = mfd = X    rm X  .
 *  4   0   0   [ ] [ ]   ofd = efd = mfd = /dev/null              .
 *
 *      So  Se   Mo  Me
 *  5   0   F   [*] [*]   ofd = |  efd = |  mfd = X          rm X  .
 *  6   0   F   [*] [ ]   ofd = mfd = X     efd = F          rm X  .
 *  7   0   F   [ ] [*]   ofd = /dev/null   efd = mfd = F          .
 *  8   0   F   [ ] [ ]   ofd = mfd = /dev/null   efd = F          .
 *
 *      So  Se   Mo  Me
 *  9   F   0   [*] [*]   ofd = |  efd = |  mfd = X          rm X  .
 * 10   F   0   [*] [ ]   ofd = mfd = F     efd = /dev/null        .
 * 11   F   0   [ ] [*]   ofd = F     efd = mfd = X          rm X  .
 * 12   F   0   [ ] [ ]   ofd = F     efd = mfd = /dev/null        .
 *
 *      So  Se   Mo  Me
 * 13   F   F   [*] [*]   ofd = efd = mfd = F                      .
 * 14   F   F   [*] [ ]   ofd = |     efd = |   mfd = X      rm X  .
 * 15   F   F   [ ] [*]   ofd = |     efd = |   mfd = X      rm X  .
 * 16   F   F   [ ] [ ]   ofd = efd = F     mfd = /dev/null        .
 *
 *      So  Se   Mo  Me
 * 17   F1  F2  [*] [*]   ofd = |  efd = |  mfd = X          rm X  .
 * 18   F1  F2  [*] [ ]   ofd = mfd = F1    efd = F2               .
 * 19   F1  F2  [ ] [*]   ofd = F1    efd = mfd = F2               .
 * 20   F1  F2  [ ] [ ]   ofd = F1    efd = F2                     .
 *
 * where 0 is /dev/null and Fi denotes a file name. */
	static const char nulfn[] = "/dev/null";
	static char tmpl[] = "/tmp/echsXXXXXXXX";
	int nulfd = open(nulfn, O_WRONLY, 0600);
	int nulfd_used = 0;
	mode_t oldm;
	int rc = 0;

	/* set the umask here just so we're safe throughout the whole run */
	oldm = umask(0077);

#define NULFD	(nulfd_used++, nulfd)
	/* put some sane defaults into t */
	t->ifd = t->ofd = t->efd = t->mfd = -1;
	t->opip = t->epip = t->teeo = t->teee = -1;

	/* go to the pwd as specified */
	if (argi->cwd_arg && chdir(argi->cwd_arg) < 0) {
		ECHS_ERR_LOG("\
cannot change working directory to `%s': %s", argi->cwd_arg, STRERR);
		rc = -1;
		goto clo;
	}

	if (argi->stdin_arg) {
		if ((t->ifd = open(argi->stdin_arg, O_RDONLY)) < 0) {
			/* grrrr, are we supposed to proceed without? */
			ECHS_ERR_LOG("\
cannot open file `%s' for child input: %s", argi->stdin_arg, STRERR);
			rc = -1;
			goto clo;
		}
	} else if ((t->ifd = open(nulfn, O_RDONLY)) < 0) {
		ECHS_ERR_LOG("\
cannot open /dev/null for child input: %s", STRERR);
		rc = -1;
		goto clo;
	}

	if (argi->stdout_arg == NULL && argi->stderr_arg == NULL &&
	    !argi->mailout_flag && !argi->mailerr_flag) {
		/* this is fucking simple, R4 */
		t->ofd = t->efd = NULFD;
	} else if (argi->stdout_arg == NULL && argi->stderr_arg == NULL) {
		/* this one is without pipes entirely, R1, R2, R3 */
		int fd;

		if (UNLIKELY((fd = mkstemp(tmpl)) < 0)) {
			ECHS_ERR_LOG("\
cannot open %s for mail output: %s", tmpl, STRERR);
			rc = -1;
			goto clo;
		} else if (argi->mailout_flag && argi->mailerr_flag) {
			t->ofd = t->efd = t->mfd = fd;
		} else if (argi->mailout_flag) {
			t->ofd = t->mfd = fd;
		} else if (argi->mailerr_flag) {
			t->efd = t->mfd = fd;
		}
		/* mark t->mfn for deletion */
		t->mfn = tmpl;
		t->mrm = 1U;
	} else if (!argi->mailout_flag && !argi->mailerr_flag) {
		/* yay, we don't have to mail at all, R8, R12, R16, R20 */
		const int fl = O_WRONLY | O_TRUNC | O_CREAT;

		if (argi->stdout_arg) {
			t->ofd = open(argi->stdout_arg, fl, 0644);
			if (UNLIKELY(t->ofd < 0)) {
				    ECHS_ERR_LOG("\
cannot open %s for output: %s", argi->stdout_arg, STRERR);
			}
		}
		if (argi->stderr_arg && t->ofd >= 0 &&
		    strcmp(argi->stdout_arg, argi->stderr_arg)) {
			t->efd = open(argi->stderr_arg, fl, 0644);
			if (UNLIKELY(t->efd < 0)) {
				    ECHS_ERR_LOG("\
cannot open %s for error output: %s", argi->stderr_arg, STRERR);
			}
		} else if (argi->stderr_arg && t->ofd >= 0) {
			t->efd = t->ofd;
		}
		/* postset with defaults */
		if (t->ofd < 0) {
			t->ofd = NULFD;
		}
		if (t->efd < 0) {
			t->efd = NULFD;
		}
	} else if (argi->mailout_flag && argi->mailerr_flag &&
		   argi->stdout_arg && argi->stderr_arg &&
		   !strcmp(argi->stdout_arg, argi->stderr_arg)) {
		/* this is simple again, R13 */
		const int fl = O_WRONLY | O_TRUNC | O_CREAT;

		t->ofd = t->efd = t->mfd = open(argi->stdout_arg, fl, 0644);
		t->mfn = argi->stdout_arg;
		if (UNLIKELY(t->ofd < 0)) {
			ECHS_ERR_LOG("\
cannot open %s for output: %s", argi->stdout_arg, STRERR);
		}
	} else if ((argi->mailout_flag == 0U) ^ (argi->mailerr_flag == 0U) &&
		   (argi->stdout_arg == NULL || argi->stderr_arg == NULL ||
		    strcmp(argi->stdout_arg, argi->stderr_arg))) {
		/* all the pipe-less stuff, R6, R7, R10, R11, R18, R19 */
		const int fl = O_WRONLY | O_TRUNC | O_CREAT;

		if (argi->stdout_arg == NULL && argi->mailout_flag) {
			/* R6 */
			t->ofd = t->mfd = mkstemp(tmpl);
			t->mfn = tmpl;
			t->mrm = 1U;
		} else if (argi->stdout_arg == NULL) {
			/* R7 */
			assert(argi->stderr_arg);
			assert(argi->mailerr_flag);
			t->ofd = NULFD;
			t->efd = t->mfd = open(argi->stderr_arg, fl, 0644);
			t->mfn = argi->stderr_arg;
		} else if (argi->stderr_arg == NULL && argi->mailout_flag) {
			/* R10 */
			t->efd = NULFD;
			t->ofd = t->mfd = open(argi->stdout_arg, fl, 0644);
			t->mfn = argi->stdout_arg;
		} else if (argi->stderr_arg == NULL) {
			/* R11 */
			assert(argi->stdout_arg);
			assert(argi->mailerr_flag);
			t->efd = t->mfd = mkstemp(tmpl);
			t->mfn = tmpl;
			t->mrm = 1U;
		} else if (argi->mailout_flag) {
			/* R18 */
			assert(argi->stdout_arg);
			assert(argi->stderr_arg);
			assert(!argi->mailerr_flag);
			t->ofd = t->mfd = open(argi->stdout_arg, fl, 0644);
			t->efd = open(argi->stderr_arg, fl, 0644);
			t->mfn = argi->stdout_arg;
		} else {
			/* R19 */
			assert(argi->stdout_arg);
			assert(argi->stderr_arg);
			assert(argi->mailerr_flag);
			t->ofd = open(argi->stdout_arg, fl, 0644);
			t->efd = t->mfd = open(argi->stderr_arg, fl, 0644);
			t->mfn = argi->stderr_arg;
		}
	} else {
		/* all the pipe-ful rest, R5, R9, R14, R15, R17 */
		const int fl = O_RDWR | O_TRUNC | O_CREAT;
		int opip[2] = {-1, -1};
		int epip[2] = {-1, -1};

		if (pipe(opip) < 0) {
			rc = -1;
			goto clo;
		} else if (pipe(epip) < 0) {
			close(opip[0U]);
			close(opip[1U]);
			rc = -1;
			goto clo;
		}
		/* we assign pipe ends from the perspective of the child */
		t->ofd = opip[1U];
		t->opip = opip[0U];
		t->efd = epip[1U];
		t->epip = epip[0U];

		/* mark parent side as close-on-exec */
		(void)fcntl(t->opip, F_SETFD, FD_CLOEXEC);
		(void)fcntl(t->epip, F_SETFD, FD_CLOEXEC);

		/* and get us a file for the mailer */
		t->mfd = mkstemp(tmpl);
		t->mfn = tmpl;
		t->mrm = 1U;

		if (argi->stdout_arg && argi->stderr_arg &&
		    !strcmp(argi->stdout_arg, argi->stderr_arg)) {
			/* R14, R15, turn the tee on its head */
			if (argi->mailout_flag) {
				/* R14 */
				t->teeo = t->mfd;
			} else if (argi->mailerr_flag) {
				/* R15 */
				t->teee = t->mfd;
			} else {
				abort();
			}
			t->mfd = open(argi->stdout_arg, fl, 0644);
		} else if (argi->stdout_arg && argi->stderr_arg) {
			/* R17 */
			t->teeo = open(argi->stdout_arg, fl, 0644);
			t->teee = open(argi->stderr_arg, fl, 0644);
		} else if (argi->stdout_arg) {
			/* R9 */
			t->teeo = open(argi->stdout_arg, fl, 0644);
		} else if (argi->stderr_arg) {
			/* R5 */
			t->teee = open(argi->stdout_arg, fl, 0644);
		} else {
			abort();
		}
	}
clo:
	if (!nulfd_used && LIKELY(nulfd >= 0)) {
		close(nulfd);
	}
#undef NULFD
	/* restore old umask */
	(void)umask(oldm);
	return rc;
}

static int
run_task(echs_task_t t)
{
/* this is C for T->CMD >(> /path/stdout) 2>(> /path/stderr) &>(sendmail ...) */
	const char *args[] = {"/bin/sh", "-c", t->cmd, NULL};
	posix_spawn_file_actions_t fa;
	int rc = 0;

	/* use the specified shell */
	if (argi->shell_arg) {
		*args = argi->shell_arg;
	}

	if (posix_spawn_file_actions_init(&fa) < 0) {
		ECHS_ERR_LOG("\
cannot initialise file actions: %s", STRERR);
		return -1;
	}

	/* fiddle with the child's descriptors */
	rc += posix_spawn_file_actions_adddup2(&fa, t->ifd, STDIN_FILENO);
	rc += posix_spawn_file_actions_adddup2(&fa, t->ofd, STDOUT_FILENO);
	rc += posix_spawn_file_actions_adddup2(&fa, t->efd, STDERR_FILENO);
	rc += posix_spawn_file_actions_addclose(&fa, t->ifd);
	rc += posix_spawn_file_actions_addclose(&fa, t->ofd);
	if (t->efd != t->ofd) {
		rc += posix_spawn_file_actions_addclose(&fa, t->efd);
	}

	/* spawn the actual beef process */
	if (posix_spawn(&chld, *args, &fa, NULL, deconst(args), t->env) < 0) {
		ECHS_ERR_LOG("cannot spawn `%s': %s", *args, STRERR);
		rc = -1;
	} else {
		ECHS_NOTI_LOG("starting `%s' -> process %d", t->cmd, chld);
	}

	/* also get rid of the file actions resources */
	posix_spawn_file_actions_destroy(&fa);

	/* close descriptors that were good for our child only */
	close(t->ifd);
	close(t->ofd);
	if (t->efd != t->ofd) {
		close(t->efd);
	}
	t->ifd = t->ofd = t->efd = -1;

	/* parent */
	clock_gettime(CLOCK_REALTIME, &t->t_sta);

	/* let's be quick and set up an event loop */
	if (chld > 0) {
		EV_P = ev_default_loop(EVFLAG_AUTO);
		ev_child c;

		ev_child_init(&c, chld_cb, chld, false);
		c.data = t;
		ev_child_start(EV_A_ &c);

		/* rearrange the descriptors again, for the mailer */
		if (t->opip >= 0 || t->epip >= 0) {
			struct data_s o = {
				.mailfd = t->mfd,
				.filefd = t->teeo,
			};
			struct data_s e = {
				.mailfd = t->mfd,
				.filefd = t->teee,
			};

			assert(t->opip >= 0);
			assert(t->epip >= 0);

			ev_io_init(&o.w, data_cb, t->opip, EV_READ);
			ev_io_start(EV_A_ &o.w);

			ev_io_init(&e.w, data_cb, t->epip, EV_READ);
			ev_io_start(EV_A_ &e.w);
		}

		ev_loop(EV_A_ 0);

		ev_break(EV_A_ EVBREAK_ALL);
		ev_loop_destroy(EV_A);
	}

	/* close the rest */
	if (t->opip >= 0 || t->epip >= 0) {
		close(t->mfd);
		t->mfd = -1;

		if (t->teeo >= 0) {
			close(t->teeo);
		}
		if (t->teee >= 0) {
			close(t->teee);
		}

		/* t->opip and t->epip should have encountered EOF
		 * but just to be sure */
		if (t->opip >= 0) {
			close(t->opip);
		}
		if (t->epip >= 0) {
			close(t->epip);
		}

		/* make all them descriptors unknown from now on */
		t->teeo = t->teee = t->opip = t->epip = -1;
	}

	/* unset timeouts */
	alarm(0);
	ECHS_NOTI_LOG("process %d finished with %d", chld, t->xc);
	chld = 0;

	clock_gettime(CLOCK_REALTIME, &t->t_end);
	getrusage(RUSAGE_CHILDREN, &t->rus);
	return rc;
}

static int
mail_task(echs_task_t t)
{
/* send our findings from task T via mail */
	static const char mailcmd[] = "/usr/sbin/sendmail";
	int mpip[2] = {-1, -1};
	posix_spawn_file_actions_t fa;
	int mfd = -1;
	int rc = 0;

	if (argi->mailfrom_arg == NULL || !argi->mailto_nargs) {
		/* no mail */
		return 0;
	} else if (pipe(mpip) < 0) {
		ECHS_ERR_LOG("\
cannot set up pipe to mailer: %s", STRERR);
		return -1;
	}

	(void)fcntl(mfd = mpip[1], F_SETFD, FD_CLOEXEC);
	if (posix_spawn_file_actions_init(&fa) < 0) {
		ECHS_ERR_LOG("\
cannot initialise file actions: %s", STRERR);
		rc = -1;
	} else {
		/* we're all set for the big forking */
		posix_spawn_file_actions_adddup2(&fa, mpip[0U], STDIN_FILENO);
		posix_spawn_file_actions_addclose(&fa, mpip[0U]);

		if (posix_spawn(&chld, mailcmd, &fa, NULL, _mcmd, NULL) < 0) {
			ECHS_ERR_LOG("\
cannot spawn `sendmail': %s", STRERR);
			rc = -1;
		}

		/* parent */
		posix_spawn_file_actions_destroy(&fa);
	}
	/* we're not interested in the read end of the descriptor */
	close(mpip[0U]);

	if (chld > 0) {
		/* now it's time to send the actual mail */
		mail_hdrs(mfd, t);

		/* joint file */
		if_with (int fd, (fd = open(t->mfn, O_RDONLY)) >= 0) {
			xsplice(mfd, fd);
			close(fd);
		}
	}

	/* send off the mail by closing the in-pipe */
	close(mfd);

	if (chld > 0) {
		int mailrc;

		while (waitpid(chld, &mailrc, 0) != chld);
		if (mailrc) {
			rc = -1;
		}
	}
	/* we're not monitoring anything anymore */
	chld = 0;
	return rc;
}

static int
jlog_task(echs_task_t t)
{
	static const char jhdr[] = "BEGIN:VJOURNAL\n";
	static const char jftr[] = "END:VJOURNAL\n";

	fdbang(STDOUT_FILENO);
	fdwrite(jhdr, strlenof(jhdr));
	/* kick off with a nice time stamp and uid */
	with (time_t now) {
		static char stmp[32U] = "DTSTAMP:";
		size_t ztmp = strlenof("DTSTAMP:");

		if (UNLIKELY(time(&now) <= 0)) {
			/* nah, leave early */
			break;
		}
		ztmp += dt_strf_ical(
			stmp + ztmp, sizeof(stmp) - ztmp,
			epoch_to_echs_instant(now));
		stmp[ztmp++] = '\n';
		fdwrite(stmp, ztmp);
	}
	/* write start/end (and their high-res counterparts?) */
	{
		static char stmp1[32U] = "DTSTART:";
		static char stmp2[32U] = "DTEND:";
		size_t n;
		echs_instant_t ts = epoch_to_echs_instant(t->t_sta.tv_sec);
		echs_instant_t te = epoch_to_echs_instant(t->t_end.tv_sec);

		n = strlenof("DTSTART:");
		n += dt_strf_ical(stmp1 + n, sizeof(stmp2) - n, ts);
		stmp1[n++] = '\n';
		fdwrite(stmp1, n);

		n = strlenof("DTEND:");
		n += dt_strf_ical(stmp2 + n, sizeof(stmp2) - n, te);
		stmp2[n++] = '\n';
		fdwrite(stmp2, n);
	}

	with (size_t cmdz = strlen(t->cmd)) {
		static char fld[] = "SUMMARY:";
		char sum[cmdz + strlenof(fld) + 1U];
		size_t si;

		si = xstrlncpy(sum, sizeof(sum), fld, strlenof(fld));
		si += xstrlncpy(sum + si, sizeof(sum) - si, t->cmd, cmdz);
		sum[si++] = '\n';
		fdwrite(sum, si);
	}

	{
		struct ts_dur_s real = ts_dur(t->t_sta, t->t_end);
		struct tv_dur_s user =
			tv_dur((struct timeval){0}, t->rus.ru_utime);
		struct tv_dur_s sys =
			tv_dur((struct timeval){0}, t->rus.ru_stime);
		double cpu =
			((double)(user.s + sys.s) +
			 (double)(user.u + sys.u) * 1.e-6) /
			((double)real.s + (double)real.n * 1.e-9) * 100.;

		fdprintf("\
X-USER-TIME:%ld.%06lis\n\
X-SYSTEM-TIME:%ld.%06lis\n\
X-REAL-TIME:%ld.%09lis\n", user.s, user.u, sys.s, sys.u, real.s, real.n);
		fdprintf("\
X-CPU-USAGE:%.2f%%\n", cpu);
		fdprintf("\
X-MEM-USAGE:%ldkB\n", t->rus.ru_maxrss);
	}

	if (WIFEXITED(t->xc)) {
		fdprintf("\
X-EXIT-STATUS:%d\n", WEXITSTATUS(t->xc));
	} else if (WIFSIGNALED(t->xc)) {
		int sig = WTERMSIG(t->xc);
		fdprintf("\
X-EXIT-STATUS:%d\n\
X-SIGNAL:%d\n\
X-SIGNAL-STRING:%s\n", 128 ^ sig, sig, strsignal(sig));
	}

	fdwrite(jftr, strlenof(jftr));
	fdflush();
	return 0;
}

static int
mail_warn(void)
{
/* mail some error about task not being run */
	static const char mailcmd[] = "/usr/sbin/sendmail";
	static const char wtxt[] = "\
The scheduled task reached its maximum number of simultaneous tasks\n\
and hence will not be run.\n\
";
	int mpip[2] = {-1, -1};
	posix_spawn_file_actions_t fa;
	int mfd = -1;
	int rc = 0;

	if (argi->mailfrom_arg == NULL || !argi->mailto_nargs) {
		/* no mail */
		return 0;
	} else if (pipe(mpip) < 0) {
		ECHS_ERR_LOG("\
cannot set up pipe to mailer: %s", STRERR);
		return -1;
	}

	(void)fcntl(mfd = mpip[1], F_SETFD, FD_CLOEXEC);
	if (posix_spawn_file_actions_init(&fa) < 0) {
		ECHS_ERR_LOG("\
cannot initialise file actions: %s", STRERR);
		rc = -1;
	} else {
		/* we're all set for the big forking */
		posix_spawn_file_actions_adddup2(&fa, mpip[0U], STDIN_FILENO);
		posix_spawn_file_actions_addclose(&fa, mpip[0U]);

		if (posix_spawn(&chld, mailcmd, &fa, NULL, _mcmd, NULL) < 0) {
			ECHS_ERR_LOG("\
cannot spawn `sendmail': %s", STRERR);
			rc = -1;
		}

		/* parent */
		posix_spawn_file_actions_destroy(&fa);
	}
	/* we're not interested in the read end of the descriptor */
	close(mpip[0U]);

	if (chld > 0) {
		char tstmp[32U];
		time_t now;

		/* get ourselves a time stamp */
		now = time(NULL);

		/* now it's time to send the actual mail */
		dt_strf(tstmp, sizeof(tstmp), epoch_to_echs_instant(now));

		fdbang(mfd);
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
		fdprintf("Subject: [NOT-RUN] %s\n", argi->command_arg);

		fdprintf("\
X-Job-Start: %s\n\
X-Job-End: %s\n",
			 tstmp, tstmp);
		fdputc('\n');
		fdflush();

		write(mfd, wtxt, strlenof(wtxt));
	}

	/* send off the mail by closing the in-pipe */
	close(mfd);

	if (chld > 0) {
		int mailrc;

		while (waitpid(chld, &mailrc, 0) != chld);
		if (mailrc) {
			rc = -1;
		}
	}
	/* we're not monitoring anything anymore */
	chld = 0;
	return rc;
}

static void
free_task(echs_task_t t)
{
/* free resources associated with T
 * there should be no descriptors open
 * but we might want to rm temp files */
	if (t->mrm) {
		assert(t->mfn);
		unlink(t->mfn);
	}
	return;
}


static int
daemonise(void)
{
	static char nulfn[] = "/dev/null";
	int nulfd;
	pid_t pid;
	int rc = 0;

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

	if (UNLIKELY(setsid() < 0)) {
		return -1;
	}

	if (UNLIKELY((nulfd = open(nulfn, O_RDONLY)) < 0)) {
		/* nope, consider us fucked, we can't even print
		 * the error message anymore */
		return -1;
	} else if (UNLIKELY(dup2(nulfd, STDIN_FILENO) < 0)) {
		/* yay, just what we need right now */
		rc = -1;
	}
	/* make sure nobody sees what we've been doing */
	close(nulfd);

	if (UNLIKELY((nulfd = open(nulfn, O_WRONLY, 0600)) < 0)) {
		/* bugger */
		return -1;
	} else if (UNLIKELY(dup2(nulfd, STDOUT_FILENO) < 0)) {
		/* nah, that's just not good enough */
		rc = -1;
	} else if (UNLIKELY(dup2(nulfd, STDERR_FILENO) < 0)) {
		/* still shit */
		rc = -1;
	}
	/* make sure we only have the copies around */
	close(nulfd);
	return rc;
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

		if (g > (gid_t)~0UL ||
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

	if (argi->umask_arg) {
		char *on;
		long unsigned int mstr = strtoul(argi->umask_arg, &on, 8);

		if (on == NULL || *on) {
			fputs("Error: cannot set umask\n", stderr);
			rc = 1;
			goto out;
		}
		(void)umask((mode_t)mstr);
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

	/* main `loop' */
	if (!argi->no_run_flag) {
		struct echs_task_s t = {argi->command_arg};

		/* prepare */
		if (prep_task(&t) < 0) {
			rc = 127;
			goto clean_up;
		}
		/* set our sigs loose */
		unblock_sigs();
		/* and here we go */
		if (run_task(&t) < 0) {
			/* bollocks */
			rc = 127;
			goto clean_up;
		}
		/* no disruptions please */
		block_sigs();
		/* write out VJOURNAL */
		if (argi->vjournal_flag) {
			jlog_task(&t);
		}
		/* brag about our findings */
		if (mail_task(&t) < 0) {
			rc = 127;
			goto clean_up;
		}

		/* finally, inherit task's return code */
		rc = WEXITSTATUS(t.xc);

	clean_up:
		free_task(&t);
	} else {
		/* jsut mail a warning and come back */
		if (mail_warn() < 0) {
			rc = 127;
			goto clean_up;
		}
	}

	/* stop them log files */
	echs_closelog();

out:
	yuck_free(argi);
	return rc;
}

/* echsx.c ends here */
