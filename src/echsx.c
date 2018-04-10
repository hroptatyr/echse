/*** echsx.c -- echse queue daemon
 *
 * Copyright (C) 2013-2018 Sebastian Freundt
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
#include <pwd.h>
#include <grp.h>
#include <ev.h>
#include <assert.h>
#include <inttypes.h>
#include "dt-strpf.h"
#include "logger.h"
#include "nifty.h"
#include "sock.h"
#include "fdprnt.h"
#include "intern.h"
#include "evical.h"
#include "nummapstr.h"

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

typedef struct echsx_task_s *echsx_task_t;

struct echsx_task_s {
	/* VTODO */
	echs_task_t t;

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

	/* error message */
	const char *errmsg;
	size_t errmsz;

	/* mail file name and whether to rm the mail file */
	const char *mfn;
	unsigned int mrm:1U;

	/* exit code */
	int xc;
	struct timespec t_sta, t_end;
	struct rusage rus;
};

static pid_t chld;

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
fdlock(int fd)
{
	struct flock flck = {
		.l_type = F_WRLCK,
		.l_whence = SEEK_END,
	};

	if (fcntl(fd, F_SETLKW, &flck) < 0) {
		return -1;
	}
	(void)lseek(fd, 0, SEEK_END);
	return 0;
}

static int
fdunlck(int fd)
{
	struct flock flck = {
		.l_type = F_UNLCK,
		.l_whence = SEEK_SET,
		.l_start = 0,
	};

	return fcntl(fd, F_SETLK, &flck);
}


static int
mail_hdrs(int tgtfd, echsx_task_t t)
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
	if (t->t->org) {
		fdprintf("From: %s\n", t->t->org);
	}
	if_with (struct strlst_s *att = t->t->att, att) {
		char *const *atp;

		if (!*(atp = att->l)) {
			break;
		}
		/* otherwise compose To header */
		fdwrite("To: ", strlenof("To: "));
		fdwrite(*atp, strlen(*atp));
		for (atp++; *atp; atp++) {
			fdputc(',');
			fdputc(' ');
			fdwrite(*atp, strlen(*atp));
		}
		fdputc('\n');
	}
	if (t->t->org) {
		fdprintf("Content-Type: text/plain\n");
		fdprintf("User-Agent: " USER_AGENT "\n");
	}
	/* write subject, if there was an error indicate so */
	fdwrite("Subject: ", strlenof("Subject: "));
	if (t->errmsg) {
		fdwrite("[NOT RUN] ", strlenof("[NOT RUN] "));
	}
	if (t->t->cmd) {
		fdwrite(t->t->cmd, strlen(t->t->cmd));
	} else {
		fdwrite("no command given", strlenof("no command given"));
	}
	fdputc('\n');

	if (t->errmsg || t->t->cmd == NULL) {
		/* we don't need any of the stuff below because
		 * it makes no sense when the job has never actually
		 * been started, or does it? */
		goto flsh;
	}

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

flsh:
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
	echsx_task_t t = c->data;
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
		for (ssize_t nsf, tots = 0;
		     tots < nsp &&
			     (nsf = sendfile(ofd, mfd, &mfo, nsp)) >= 0;
		     tots += nsf);

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
prep_task(echsx_task_t t)
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
	int rc = 0;

#define NULFD	(nulfd_used++, nulfd)
	/* put some sane defaults into t */
	t->ifd = t->ofd = t->efd = t->mfd = -1;
	t->opip = t->epip = t->teeo = t->teee = -1;

	/* go to the pwd as specified */
	if (t->t->run_as.wd && chdir(t->t->run_as.wd) < 0) {
		ECHS_ERR_LOG("\
cannot change working directory to `%s': %s", t->t->run_as.wd, STRERR);
		rc = -1;
		goto clo;
	}

	if (t->t->in) {
		if ((t->ifd = open(t->t->in, O_RDONLY)) < 0) {
			/* grrrr, are we supposed to proceed without? */
			ECHS_ERR_LOG("\
cannot open file `%s' for child input: %s", t->t->in, STRERR);
			rc = -1;
			goto clo;
		}
	} else if ((t->ifd = open(nulfn, O_RDONLY)) < 0) {
		ECHS_ERR_LOG("\
cannot open /dev/null for child input: %s", STRERR);
		rc = -1;
		goto clo;
	}

	if (t->t->out == NULL && t->t->err == NULL &&
	    !t->t->mailout && !t->t->mailerr) {
		/* this is fucking simple, R4 */
		t->ofd = t->efd = NULFD;
	} else if (t->t->out == NULL && t->t->err == NULL) {
		/* this one is without pipes entirely, R1, R2, R3 */
		int fd;

		if (UNLIKELY((fd = mkstemp(tmpl)) < 0)) {
			ECHS_ERR_LOG("\
cannot open %s for mail output: %s", tmpl, STRERR);
			rc = -1;
			goto clo;
		} else if (t->t->mailout && t->t->mailerr) {
			/* R1 */
			t->ofd = t->efd = t->mfd = fd;
		} else if (t->t->mailout) {
			/* R2 */
			t->ofd = t->mfd = fd;
			t->efd = NULFD;
		} else if (t->t->mailerr) {
			/* R3 */
			t->efd = t->mfd = fd;
			t->ofd = NULFD;
		}
		/* mark t->mfn for deletion */
		t->mfn = tmpl;
		t->mrm = 1U;
	} else if (!t->t->mailout && !t->t->mailerr) {
		/* yay, we don't have to mail at all, R8, R12, R16, R20 */
		const int fl = O_WRONLY | O_TRUNC | O_CREAT;

		if (t->t->out) {
			/* R12 || R20 || R16 */
			t->ofd = open(t->t->out, fl, 0666);
			if (UNLIKELY(t->ofd < 0)) {
				    ECHS_ERR_LOG("\
cannot open %s for output: %s", t->t->out, STRERR);
			}
		}
		if (t->t->err &&
		    (t->t->out == NULL || strcmp(t->t->out, t->t->err))) {
			/* R8 || R20 */
			t->efd = open(t->t->err, fl, 0666);
			if (UNLIKELY(t->efd < 0)) {
				    ECHS_ERR_LOG("\
cannot open %s for error output: %s", t->t->err, STRERR);
			}
		} else if (t->t->err && t->ofd >= 0) {
			/* R16 */
			t->efd = t->ofd;
		}
		/* postset with defaults */
		if (t->ofd < 0) {
			t->ofd = NULFD;
		}
		if (t->efd < 0) {
			t->efd = NULFD;
		}
	} else if (t->t->mailout && t->t->mailerr &&
		   t->t->out && t->t->err &&
		   !strcmp(t->t->out, t->t->err)) {
		/* this is simple again, R13 */
		const int fl = O_WRONLY | O_TRUNC | O_CREAT;

		t->ofd = t->efd = t->mfd = open(t->t->out, fl, 0666);
		t->mfn = t->t->out;
		if (UNLIKELY(t->ofd < 0)) {
			ECHS_ERR_LOG("\
cannot open %s for output: %s", t->t->out, STRERR);
		}
	} else if ((t->t->mailout == 0U) ^ (t->t->mailerr == 0U) &&
		   (t->t->out == NULL || t->t->err == NULL ||
		    strcmp(t->t->out, t->t->err))) {
		/* all the pipe-less stuff, R6, R7, R10, R11, R18, R19 */
		const int fl = O_WRONLY | O_TRUNC | O_CREAT;

		/* let's go through these cases, we must certainly
		 * set ofd, efd and mfd in every single one of them */
		if (t->t->out == NULL && t->t->mailout) {
			/* R6 (R11 mirror) */
			assert(t->t->err);
			assert(!t->t->mailerr);
			t->ofd = t->mfd = mkstemp(tmpl);
			t->efd = open(t->t->err, fl, 0666);
			t->mfn = tmpl;
			t->mrm = 1U;
			if (UNLIKELY(t->efd < 0)) {
				ECHS_ERR_LOG("\
cannot open %s for error output: %s", t->t->err, STRERR);
			}
		} else if (t->t->out == NULL) {
			/* R7 */
			assert(t->t->err);
			assert(t->t->mailerr);
			t->ofd = NULFD;
			t->efd = t->mfd = open(t->t->err, fl, 0666);
			t->mfn = t->t->err;
			if (UNLIKELY(t->efd < 0)) {
				ECHS_ERR_LOG("\
cannot open %s for error output: %s", t->t->err, STRERR);
			}
		} else if (t->t->err == NULL && t->t->mailout) {
			/* R10 */
			assert(t->t->out);
			assert(!t->t->mailerr);
			t->ofd = t->mfd = open(t->t->out, fl, 0666);
			t->efd = NULFD;
			t->mfn = t->t->out;
			if (UNLIKELY(t->ofd < 0)) {
				ECHS_ERR_LOG("\
cannot open %s for output: %s", t->t->out, STRERR);
			}
		} else if (t->t->err == NULL) {
			/* R11 (R6 mirror) */
			assert(t->t->out);
			assert(!t->t->mailout);
			t->efd = t->mfd = mkstemp(tmpl);
			t->ofd = open(t->t->out, fl, 0666);
			t->mfn = tmpl;
			t->mrm = 1U;
			if (UNLIKELY(t->ofd < 0)) {
				ECHS_ERR_LOG("\
cannot open %s for output: %s", t->t->out, STRERR);
			}
		} else if (t->t->mailout) {
			/* R18 */
			assert(t->t->out);
			assert(t->t->err);
			assert(!t->t->mailerr);
			t->ofd = t->mfd = open(t->t->out, fl, 0666);
			if (UNLIKELY(t->ofd < 0)) {
				ECHS_ERR_LOG("\
cannot open %s for output: %s", t->t->out, STRERR);
			}
			t->efd = open(t->t->err, fl, 0666);
			if (UNLIKELY(t->efd < 0)) {
				ECHS_ERR_LOG("\
cannot open %s for error output: %s", t->t->err, STRERR);
			}
			t->mfn = t->t->out;
		} else {
			/* R19 */
			assert(t->t->out);
			assert(t->t->err);
			assert(t->t->mailerr);
			t->ofd = open(t->t->out, fl, 0666);
			if (UNLIKELY(t->ofd < 0)) {
				ECHS_ERR_LOG("\
cannot open %s for output: %s", t->t->out, STRERR);
			}
			t->efd = t->mfd = open(t->t->err, fl, 0666);
			if (UNLIKELY(t->efd < 0)) {
				ECHS_ERR_LOG("\
cannot open %s for error output: %s", t->t->err, STRERR);
			}
			t->mfn = t->t->err;
		}
		/* postset with defaults */
		if (UNLIKELY(t->ofd < 0)) {
			t->ofd = NULFD;
		}
		if (UNLIKELY(t->efd < 0)) {
			t->efd = NULFD;
		}
	} else {
		/* all the pipe-ful rest, R5, R9, R14, R15, R17 */
		const int fl = O_RDWR | O_TRUNC | O_CREAT;
		int opip[2] = {-1, -1};
		int epip[2] = {-1, -1};

		if (pipe(opip) < 0) {
			ECHS_ERR_LOG("\
cannot open pipe for output: %s", STRERR);
			rc = -1;
			goto clo;
		} else if (pipe(epip) < 0) {
			ECHS_ERR_LOG("\
cannot open pipe for error output: %s", STRERR);
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
		(void)fd_cloexec(t->opip);
		(void)fd_cloexec(t->epip);

		/* and get us a file for the mailer */
		t->mfd = mkstemp(tmpl);
		t->mfn = tmpl;
		t->mrm = 1U;

		if (t->t->out && t->t->err &&
		    !strcmp(t->t->out, t->t->err)) {
			/* R14, R15, turn the tee on its head */
			if (t->t->mailout) {
				/* R14 */
				t->teeo = t->mfd;
			} else if (t->t->mailerr) {
				/* R15 */
				t->teee = t->mfd;
			} else {
				abort();
			}
			t->mfd = open(t->t->out, fl, 0666);
			if (UNLIKELY(t->mfd < 0)) {
				ECHS_ERR_LOG("\
cannot open %s for output: %s", t->t->out, STRERR);
			}
		} else if (t->t->out && t->t->err) {
			/* R17 */
			t->teeo = open(t->t->out, fl, 0666);
			if (UNLIKELY(t->teeo < 0)) {
				ECHS_ERR_LOG("\
cannot open %s for output: %s", t->t->out, STRERR);
			}
			t->teee = open(t->t->err, fl, 0666);
			if (UNLIKELY(t->teee < 0)) {
				ECHS_ERR_LOG("\
cannot open %s for error output: %s", t->t->err, STRERR);
			}
		} else if (t->t->out) {
			/* R9 */
			t->teeo = open(t->t->out, fl, 0666);
			if (UNLIKELY(t->teeo < 0)) {
				ECHS_ERR_LOG("\
cannot open %s for output: %s", t->t->out, STRERR);
			}
		} else if (t->t->err) {
			/* R5 */
			t->teee = open(t->t->err, fl, 0666);
			if (UNLIKELY(t->teee < 0)) {
				ECHS_ERR_LOG("\
cannot open %s for error output: %s", t->t->err, STRERR);
			}
		} else {
			abort();
		}
	}
clo:
	if (!nulfd_used && LIKELY(nulfd >= 0)) {
		close(nulfd);
	}
#undef NULFD
	return rc;
}

static int
run_task(echsx_task_t t)
{
/* this is C for T->CMD >(> /path/stdout) 2>(> /path/stderr) &>(sendmail ...) */
	const char *args[] = {"/bin/sh", "-c", t->t->cmd, NULL};
	char *const *env = t->t->env ? t->t->env->l : NULL;
	posix_spawn_file_actions_t fa;
	int rc = 0;

	/* use the specified shell */
	if (t->t->run_as.sh) {
		*args = t->t->run_as.sh;
	}

	if (posix_spawn_file_actions_init(&fa) < 0) {
		ECHS_ERR_LOG("\
cannot initialise file actions: %s", STRERR);
		return -1;
	}

	/* initialise the default loop */
	EV_P = ev_default_loop(EVFLAG_AUTO);

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
	if (posix_spawn(&chld, *args, &fa, NULL, deconst(args), env) < 0) {
		ECHS_ERR_LOG("cannot spawn `%s': %s", *args, STRERR);
		rc = -1;
		t->xc = 127;
	} else {
		ECHS_NOTI_LOG("starting `%s' -> process %d", t->t->cmd, chld);
		/* assume success */
		t->xc = 0;
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
	}

	/* make sure we free assoc'd resources */
	ev_loop_destroy(EV_A);

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
	ECHS_NOTI_LOG("process %d finished with %d", chld, WEXITSTATUS(t->xc));
	chld = 0;

	clock_gettime(CLOCK_REALTIME, &t->t_end);
	getrusage(RUSAGE_CHILDREN, &t->rus);
	return rc;
}

static int
mail_task(echsx_task_t t)
{
/* send our findings from task T via mail */
	static const char mailcmd[] = "/usr/sbin/sendmail";
	int mpip[2] = {-1, -1};
	posix_spawn_file_actions_t fa;
	int mfd = -1;
	int rc = 0;

	if (t->t->org == NULL || t->t->att == NULL || !t->t->att->nl) {
		/* no mail */
		return 0;
	} else if (!t->t->mailout && !t->t->mailerr &&
		   !t->t->mailrun && t->errmsg == NULL) {
		/* they really don't want us to send mail do they */
		return 0;
	} else if (pipe(mpip) < 0) {
		ECHS_ERR_LOG("\
cannot set up pipe to mailer: %s", STRERR);
		return -1;
	}

	(void)fd_cloexec(mfd = mpip[1]);
	if (posix_spawn_file_actions_init(&fa) < 0) {
		ECHS_ERR_LOG("\
cannot initialise file actions: %s", STRERR);
		rc = -1;
	} else {
		/* we're all set for the big forking */
		posix_spawn_file_actions_adddup2(&fa, mpip[0U], STDIN_FILENO);
		posix_spawn_file_actions_addclose(&fa, mpip[0U]);
		posix_spawn_file_actions_addclose(&fa, STDOUT_FILENO);
		posix_spawn_file_actions_addclose(&fa, STDERR_FILENO);

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
		int fd;

		/* now it's time to send the actual mail */
		fdbang(mfd);
		mail_hdrs(mfd, t);

		if (t->errmsg) {
			/* error message only */
			fdwrite(t->errmsg, t->errmsz);
			fdputc('\n');
		} else if (t->mfn == NULL) {
			/* no mail file no splicing, simples */
			;
		} else if ((fd = open(t->mfn, O_RDONLY)) < 0) {
			/* tell user we fucked his mail file */
			fdprintf("Error: cannot open mail file `%s'\n", t->mfn);
		} else {
			xsplice(mfd, fd);
			close(fd);
		}

		/* that's all from us */
		fdflush();
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
jlog_task(echsx_task_t t)
{
	static const char jhdr[] = "BEGIN:VTODO\n";
	static const char jftr[] = "END:VTODO\n";
	static char stmp[32U] = "CODTSTAMP:";
	size_t nstmp;

	if (t->t_end.tv_sec <= 0 && time(&t->t_end.tv_sec) == (time_t)-1) {
		/* shit! */
		ECHS_ERR_LOG("\
cannot obtain current time: %s", STRERR);
		return -1;
	}

	fdbang(STDOUT_FILENO);
	if (fdlock(STDOUT_FILENO) < 0) {
		ECHS_ERR_LOG("\
cannot obtain lock: %s", STRERR);
		return -1;
	}

	/* introduce ourselves */
	fdwrite(jhdr, strlenof(jhdr));
	/* start off with the stamp and uid */
	with (echs_instant_t te = epoch_to_echs_instant(t->t_end.tv_sec)) {
		size_t n;

		n = strlenof("XXDTSTAMP:");
		n += dt_strf_ical(stmp + n, sizeof(stmp) - n, te);
		stmp[n++] = '\n';
		fdwrite(stmp + 2U, n - 2U);
		/* keep track of this for later */
		nstmp = n;
	}

	if (LIKELY(t->t->oid)) {
		static const char fld[] = "UID:";
		const char *tid = obint_name(t->t->oid);

		fdwrite(fld, strlenof(fld));
		fdwrite(tid, strlen(tid));
		fdputc('\n');
	}

	/* write start/completed (and their high-res counterparts?) */
	if (t->t_sta.tv_sec > 0) {
		static char strt[32U] = "DTSTART:";
		echs_instant_t ts = epoch_to_echs_instant(t->t_sta.tv_sec);
		size_t n;

		n = strlenof("DTSTART:");
		n += dt_strf_ical(strt + n, sizeof(strt) - n, ts);
		strt[n++] = '\n';
		fdwrite(strt, n);
	}
	memcpy(stmp + 2U, "MPLETED:", strlenof("MPLETED:"));
	fdwrite(stmp, nstmp);

	with (size_t cmdz = strlen(t->t->cmd)) {
		static char fld[] = "SUMMARY:";
		char sum[cmdz + strlenof(fld) + 1U];
		size_t si;

		si = xstrlncpy(sum, sizeof(sum), fld, strlenof(fld));
		si += xstrlncpy(sum + si, sizeof(sum) - si, t->t->cmd, cmdz);
		sum[si++] = '\n';
		fdwrite(sum, si);
	}

	if (t->errmsg || t->xc < 0) {
		static const char canc[] = "STATUS:CANCELLED\n";
		static const char desc[] = "DESCRIPTION:";
		static const char nrun[] = "not run for reasons unknown";
		fdwrite(canc, strlenof(canc));
		fdwrite(desc, strlenof(desc));
		if (t->errmsg) {
			fdwrite(t->errmsg, t->errmsz);
		} else {
			fdwrite(nrun, strlenof(nrun));
		}
		fdputc('\n');
		goto flsh;
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
		int s = WIFEXITED(t->xc)
			? WEXITSTATUS(t->xc)
			: WIFSIGNALED(t->xc)
			? WTERMSIG(t->xc) ^ 128
			: -1;

		fdprintf("\
X-USER-TIME:%ld.%06lis\n\
X-SYSTEM-TIME:%ld.%06lis\n\
X-REAL-TIME:%ld.%09lis\n", user.s, user.u, sys.s, sys.u, real.s, real.n);
		fdprintf("\
X-CPU-USAGE:%.2f%%\n", cpu);
		fdprintf("\
X-MEM-USAGE:%ldkB\n", t->rus.ru_maxrss);

		fdprintf("\
DESCRIPTION:$?=%d  %ldkB mem\\n\n\
 %ld.%06lis user  %ld.%06lis sys  %.2f%% cpu  %ld.%09lis real\n",
			 s, t->rus.ru_maxrss,
			 user.s, user.u, sys.s, sys.u, cpu, real.s, real.n);
	}

flsh:
	fdwrite(jftr, strlenof(jftr));
	fdflush();
	for (size_t i = 3U; i && fdunlck(STDOUT_FILENO) < 0; i--);
	return 0;
}

static void
free_task(echsx_task_t t)
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


#include "echsx.yucc"

static yuck_t argi[1U];

static int
echsx(echs_task_t t)
{
	char _err[1024U];
	struct echsx_task_s xt = {t};
	mode_t umsk_old = 0777U;
	int rc = 0;
	const char *tmps;
	uintptr_t tmpn;
	uid_t u;
	gid_t g;

#define NOT_A_UID	((uid_t)-1)
#define NOT_A_GID	((gid_t)-1)

#define XERROR(args...)						\
	with (int __z) {					\
		__z = snprintf(_err, sizeof(_err), args);	\
		if (__z < 0) {					\
			/* you've got to be kidding :O */	\
			return -1;				\
		}						\
		xt.errmsg = _err;				\
		xt.errmsz = __z;				\
	}

	/* switch to user/group early */
	if ((tmps = nummapstr_str(t->run_as.g))) {
		struct group *gr;

		if (UNLIKELY((gr = getgrnam(tmps)) == NULL)) {
			XERROR("\
cannot obtain group id for group `%s': %s", tmps, STRERR);
			goto fatal;
		}
		/* otherwise get the gid */
		g = gr->gr_gid;
	} else if ((tmpn = nummapstr_num(t->run_as.g)) == NUMMAPSTR_NAN) {
		/* no group info at all :O */
		g = NOT_A_GID;
	} else {
		g = (gid_t)tmpn;
	}

	if ((tmps = nummapstr_str(t->run_as.u))) {
		struct passwd *pw;

		if (UNLIKELY((pw = getpwnam(tmps)) == NULL)) {
			XERROR("\
cannot obtain user id for user `%s': %s", tmps, STRERR);
			goto fatal;
		}
		/* otherwise get the uid */
		u = pw->pw_uid;
		if (UNLIKELY(g == NOT_A_GID)) {
			/* also obtain the gid */
			g = pw->pw_gid;
		}
	} else if ((tmpn = nummapstr_num(t->run_as.u)) == NUMMAPSTR_NAN) {
		/* no user info at all :O */
		XERROR("\
X-ECHS-SETUID field in VTODO is mandatory");
		goto fatal;
	} else if ((u = (uid_t)tmpn, g == NOT_A_GID)) {
		struct passwd *pw;

		if (UNLIKELY((pw = getpwuid(u)) == NULL)) {
			XERROR("\
cannot obtain group id for numeric user %u: %s", u, STRERR);
			goto fatal;
		}
		g = pw->pw_gid;
	}

	/* now actually setgid/setuid the whole thing */
	if (UNLIKELY(setgid(g) < 0)) {
		XERROR("\
cannot set group id to %u: %s", g, STRERR);
		goto fatal;
	}
	if (UNLIKELY(setuid(u) < 0)) {
		XERROR("\
cannot set user id to %u: %s", u, STRERR);
		goto fatal;
	}

	/* are we supposed to run? */
	if (argi->no_run_flag) {
		/* nope, apparently not */
		static const char msg[] = "\
The scheduled task reached its maximum number of simultaneous runs.";
		xt.errmsg = msg;
		xt.errmsz = strlenof(msg);
		goto fatal;
	}

	/* check the command string */
	if (t->cmd == NULL) {
		static const char msg[] = "\
No command has been specified.";
		xt.errmsg = msg;
		xt.errmsz = strlenof(msg);
		goto fatal;
	}

	/* set up timeout */
	switch (t->vtod_typ) {
		unsigned int timeo;

	case VTOD_TYP_TIMEOUT:
		if (UNLIKELY(t->timeout.d < 0)) {
			int z = snprintf(_err, sizeof(_err), "\
Value for timeout (%" PRIi64 ") is out of range.", t->timeout.d);
			xt.errmsg = _err;
			xt.errmsz = z;
			goto fatal;
		}
		/* otherwise */
		timeo = t->timeout.d;
		goto timeo;

	case VTOD_TYP_DUE: {
		time_t due = echs_instant_to_epoch(t->due);
		time_t now = 0;

		if (UNLIKELY(time(&now) == (time_t)-1 || now >= due)) {
			/* brilliant, what exactly do we do with
			 * an overdue thing? */
			static const char msg[] = "\
Task is past its due time, not executing.";
			xt.errmsg = msg;
			xt.errmsz = strlenof(msg);
			goto fatal;
		}
		/* otherwise */
		timeo = due - now;
		goto timeo;
	}
	timeo:
		if (UNLIKELY(set_timeout(timeo) < 0)) {
			ECHS_NOTI_LOG("\
void timeout value, job execution will be unbounded");
		}
		/*@fallthrough@*/
	default:
		break;
	}

	/* umask fiddling */
	umsk_old = umask(t->umsk);
	if (t->umsk > 0777U) {
		/* better reset */
		(void)umask(umsk_old);
	}

	/* prepare */
	if (prep_task(&xt) < 0) {
		rc = 127;
		goto clean_up;
	}
	/* set our sigs loose */
	unblock_sigs();
	/* and here we go */
	if (run_task(&xt) < 0) {
		/* bollocks */
		rc = 127;
		goto clean_up;
	}

	if (0) {
	fatal:
		ECHS_ERR_LOG("%s", xt.errmsg);
		rc = -1;
	} else {
		/* finally, inherit task's return code */
		rc = WEXITSTATUS(xt.xc);
	}

	/* no disruptions please */
	block_sigs();
	/* write out VJOURNAL */
	if (argi->vjournal_flag) {
		jlog_task(&xt);
	}
	/* brag about our findings */
	if (mail_task(&xt) < 0) {
		rc = 127;
		goto clean_up;
	}

clean_up:
	free_task(&xt);

	/* reset umask */
	(void)umask(umsk_old);
	return rc;
}

static int
daemonise(void)
{
	switch (fork()) {
	case -1:
		return -1;
	case 0:
		/* i am the child */
		break;
	default:
		/* i am the parent */
		exit(0);
	}
	return setsid();
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

	if (!argi->daemon_flag) {
		echs_log = echs_errlog;
	} else if (daemonise() < 0) {
		perror("Error: daemonisation failed");
		rc = 1;
		goto out;
	}

	/* start them log files */
	echs_openlog();

	with (ical_parser_t pp = NULL) {
		char buf[4096U];
		ssize_t nrd;

	more:
		switch ((nrd = read(STDIN_FILENO, buf, sizeof(buf)))) {
			echs_instruc_t ins;

		default:
			if (echs_evical_push(&pp, buf, nrd) < 0) {
				/* pushing more aids fuckall */
				break;
			}
			/*@fallthrough@*/
		case 0:
			do {
				ins = echs_evical_pull(&pp);

				if (UNLIKELY(ins.v != INSVERB_SCHE)) {
					break;
				} else if (UNLIKELY(ins.t == NULL)) {
					ECHS_ERR_LOG("\
cannot execute: no instructions given");
					continue;
				} else if (UNLIKELY(!ins.t->oid)) {
					ECHS_ERR_LOG("\
cannot execute: no uid present");
					goto free;
				}
				/* otherwise ins.t is a task and good to go */
				echsx(ins.t);
			free:
				free_echs_task(ins.t);
			} while (1);
			if (LIKELY(nrd > 0)) {
				goto more;
			}
			/*@fallthrough@*/
		case -1:
			/* last ever pull this is */
			ins = echs_evical_last_pull(&pp);

			if (UNLIKELY(ins.v != INSVERB_SCHE)) {
				break;
			} else if (UNLIKELY(ins.t != NULL)) {
				/* we shouldn't be getting a task upon the
				 * last pull, so just free him */
				free_echs_task(ins.t);
			}
			break;
		}
	}

	/* stop them log files */
	echs_closelog();

out:
	/* we're just too nice, freeing shit and all */
	close(STDIN_FILENO);
	yuck_free(argi);
	return rc;
}

/* echsx.c ends here */
