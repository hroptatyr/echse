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
#include <limits.h>
#include <time.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#if defined __FreeBSD__
# include <sys/syscall.h>
#endif	/* __FreeBSD__ */
#include <pwd.h>
#include <ev.h>
#include "echse.h"
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
	ev_child c;

	/* the stream where this task comes from */
	echs_evstrm_t strm;

	/* beef data for the task in question */
	const char *cmd;
	char **env;
};

struct _echsd_s {
	ev_signal sigint;
	ev_signal sighup;
	ev_signal sigterm;
	ev_signal sigpipe;

	ev_io ctlsock;

	struct ev_loop *loop;
};

static const char *echsx;


static __attribute__((unused)) size_t
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

static const char*
get_exewd(void)
{
#if defined __linux__
	static const char myself[] = "/proc/self/exe";
	static char wd[PATH_MAX];

	if_with (ssize_t z, (z = readlink(myself, wd, sizeof(wd))) >= 0) {
		wd[z] = '\0';
		return wd;
	}
	return NULL;
#elif defined __NetBSD__
	static const char myself[] = "/proc/curproc/exe";
	static char wd[PATH_MAX];
	ssize_t z;

	if_with (ssize_t z, (z = readlink(myself, wd, sizeof(wd))) >= 0) {
		wd[z] = '\0';
		return wd;
	}
	return NULL;
#elif defined __DragonFly__
	static const char myself[] = "/proc/curproc/file";
	static char wd[PATH_MAX];

	if_with (ssize_t z, (z = readlink(myself, wd, sizeof(wd))) >= 0) {
		wd[z] = '\0';
		return wd;
	}
	return NULL;
#elif defined __FreeBSD__
	static char wd[PATH_MAX];
	size_t z = sizeof(wd);
	int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1};
	sysctl(mib, countof(mib), wd, z, NULL, 0);
	return wd;
#elif defined __sun || defined sun
	static char wd[MAXPATHLEN];
	ssize_t z;

	snprintf(wd, sizeof(wd), "/proc/%d/path/a.out", getpid());
	if_with (ssize_t z, (z = readlink(myself, wd, sizeof(wd))) >= 0) {
		wd[z] = '\0';
		return wd;
	}
	return NULL;
#elif defined __APPLE__ && defined __MACH__
	static char wd[PATH_MAX];
	uint32_t z;
	if (_NSGetExecutablePath(wd, &z) == 0) {
		return wd;
	}
	return NULL;
#else
	return NULL;
#endif
}

static const char*
get_echsx(void)
{
	static const char echsx_nam[] = "echsx";
	static char echsx_exe[PATH_MAX];
	const char *xwd;
	const char *dp;

	if (UNLIKELY((xwd = get_exewd()) == NULL)) {
		return NULL;
	} else if ((dp = strrchr(xwd, '/')) == NULL) {
		return NULL;
	}
	/* copy till last slash */
	memcpy(echsx_exe, xwd, dp - xwd + 1U);
	with (char *xp = echsx_exe + (dp - xwd + 1U)) {
		struct stat st;

		memcpy(xp, echsx_nam, sizeof(echsx_nam));
		if (stat(echsx_exe, &st) < 0) {
			break;
		} else if (!S_ISREG(st.st_mode)) {
			break;
		} else if (!(st.st_mode & S_IXOTH)) {
			break;
		}
		/* seems to be a good fit */
		goto found;
	}
	/* try ../bin/echsx next */
	echsx_exe[dp - xwd] = '\0';
	if_with (char *xp, (xp = strrchr(echsx_exe, '/')) != NULL) {
		struct stat st;

		memcpy(xp + 1U, "bin/", 4U);
		memcpy(xp + 1U + 4U, echsx_nam, sizeof(echsx_nam));
		if (stat(echsx_exe, &st) < 0) {
			break;
		} else if (!S_ISREG(st.st_mode)) {
			break;
		} else if (!(st.st_mode & S_IXOTH)) {
			break;
		}
		/* seems to be a good fit */
		goto found;
	}
	/* we've run out of options */
	return NULL;

found:
	return echsx_exe;
}

static const char*
get_vardir(void)
{
	static const char rdir[] = "/var/run";
	static const char ldir[] = ".echse";
	static char vdir[PATH_MAX];
	uid_t u;

	switch ((u = geteuid())) {
	case 0:
		if (mkdir(rdir, 0755) == -1 && errno == EEXIST) {
			size_t vi;

			vi = xstrlcpy(vdir, rdir, sizeof(vdir));
			vdir[vi++] = '/';
			vi += xstrlcpy(vdir + vi, ldir, sizeof(vdir) - vi);
			/* just mkdir the result and throw away errors */
			if (mkdir(vdir, 0700) == 0 || errno == EEXIST) {
				/* we consider our job done */
				return vdir;
			}
		}
		break;
	default:
		if_with (struct passwd *pw,
			 (pw = getpwuid(u)) && pw->pw_dir &&
			 /* make sure home directory is actually there */
			 mkdir(pw->pw_dir, 0755) == -1 && errno == EEXIST) {
			size_t vi;

			vi = xstrlcpy(vdir, pw->pw_dir, sizeof(vdir));
			vdir[vi++] = '/';
			vi += xstrlcpy(vdir + vi, ldir, sizeof(vdir) - vi);
			/* just mkdir the result and throw away errors */
			if (mkdir(vdir, 0700) == 0 || errno == EEXIST) {
				/* we consider our job done */
				return vdir;
			}
			/* otherwise it's just horseshit */
		}
		break;
	}
	/* we've run out of options */
	return NULL;
}

static const char*
pathcat(const char *dirnm, ...)
{
	static char res[PATH_MAX];
	va_list ap;
	size_t ri = 0U;

	va_start(ap, dirnm);
	ri += xstrlcpy(res + ri, dirnm, sizeof(res) - ri);
	for (const char *fn; (fn = va_arg(ap, const char*));) {
		res[ri++] = '/';
		ri += xstrlcpy(res + ri, fn, sizeof(res) - ri);
	}
	va_end(ap);
	return res;
}

static int
make_socket(const char *edir)
{
	struct sockaddr_un sa = {.sun_family = AF_UNIX};
	const char *fn;
	size_t sz;
	int s;

	if (UNLIKELY((s = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)) {
		return -1;
	}
	fn = pathcat(edir, "=echsd", NULL);
	sz = xstrlcpy(sa.sun_path, fn, sizeof(sa.sun_path));
	sz += sizeof(sa.sun_family);
	if (UNLIKELY(bind(s, (struct sockaddr*)&sa, sz) < 0)) {
		goto fail;
	} else if (listen(s, 5U) < 0) {
		goto fail;
	}
	return s;
fail:
	close(s);
	return -1;
}

static int
free_socket(int s, const char *edir)
{
	const char *fn = pathcat(edir, "=echsd", NULL);
	int res = unlink(fn);
	close(s);
	return res;
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
		if (UNLIKELY(free_pers == NULL)) {
			/* grrrr */
			return NULL;
		}
		if (UNLIKELY(!(zfree_pers *= 2U))) {
			zfree_pers = 256U;
		}
	}
	/* pop off the free list */
	res = free_pers;
	free_pers = free_pers->next;
	nfree_pers--;
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
run_task(echs_task_t t, bool dtchp)
{
/* assumes ev_loop_fork() has been called */
	pid_t r;

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
		args[2U] = deconst(t->cmd);
		if (dtchp) {
			args[7U] = NULL;
		}
		rc = execve(echsx, args, t->env);
		_exit(rc);
		/* not reached */

	default:
		/* I am daddy */
		if (dtchp) {
			while (waitpid(r, &rc, 0) != r);
		}
		break;
	}
	return r;
}

static __attribute__((pure, const)) ev_tstamp
instant_to_tstamp(echs_instant_t i)
{
/* this way around it's easier, date range supported is 2001 to 2099
 * (i.e. with no bullshit leap years) */
	static uint16_t __mon_yday[] = {
		/* this is \sum ml,
		 * first element is a bit set of leap days to add */
		0xfff8, 0,
		31, 59, 90, 120, 151, 181,
		212, 243, 273, 304, 334, 365
	};
	unsigned int nd = 0U;
	time_t t;

	/* days from 2001-01-01 till day 0 of current year,
	 * i.e. i.y-01-00 */
	nd += 365U * (i.y - 2001U) + (i.y - 2001U) / 4U;
	/* day-of-year */
	nd += __mon_yday[i.m] + i.d + UNLIKELY(!(i.y % 4U) && i.m >= 3);

	if (LIKELY(!echs_instant_all_day_p(i))) {
		t = (((time_t)nd * 24U + i.H) * 60U + i.M) * 60U + i.S;
	} else {
		t = (time_t)nd * 86400UL;
	}
	/* calc number of seconds since unix epoch */
	t += 11323/*days from unix epoch to our epoch*/ * 86400UL;
	return (double)t;
}

static echs_event_t
unwind_till(echs_evstrm_t x, ev_tstamp t)
{
	echs_event_t e;
	while (!echs_event_0_p(e = echs_evstrm_next(x)) &&
	       instant_to_tstamp(e.from) < t);
	return e;
}


/* libev conn handling */
#define MAX_CONNS	(sizeof(free_conns) * 8U)
#define MAX_QUEUE	MAX_CONNS
static uint64_t free_conns = -1;
static struct echs_conn_s {
	ev_io r;
} conns[MAX_CONNS];

static struct echs_conn_s*
make_conn(void)
{
	int i = ffs(free_conns & 0xffffffffU)
		?: ffs(free_conns >> 32U & 0xffffffffU);

	if (LIKELY(i-- > 0)) {
		/* toggle bit in free conns */
		free_conns ^= 1ULL << i;
		return conns + i;
	}
	return NULL;
}

static void
free_conn(struct echs_conn_s *c)
{
	size_t i = c - conns;

	if (UNLIKELY(i >= MAX_CONNS)) {
		/* huh? */
		ECHS_ERR_LOG("unknown connection passed to free_conn()");
		return;
	}
	/* toggle C-th bit */
	free_conns ^= 1ULL << i;
	memset(c, 0, sizeof(*c));
	return;
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
chld_cb(EV_P_ ev_child *w, int UNUSED(revents))
{
	ECHS_NOTI_LOG("chld %d coughed: %d", w->rpid, w->rstatus);
	ev_child_stop(EV_A_ w);
	w->rpid = w->pid = 0;
	return;
}

static void
taskA_cb(EV_P_ ev_periodic *w, int UNUSED(revents))
{
/* A tasks always run asynchronously, echsx decouples itself from echsd
 * and manages the job with no further interaction */
	echs_task_t t = (void*)w;

	/* indicate that we might want to reuse the loop */
	ev_loop_fork(EV_A);

	/* these are completely asynchronous with no further monitoring
	 * so fire and forget */
	(void)run_task(t, true);

	if (w->reschedule_cb) {
		/* ah, we're going to be used again */
		return;
	}
	free_task(t);
	return;
}

static void
taskB_cb(EV_P_ ev_periodic *w, int UNUSED(revents))
{
/* B tasks always run under supervision of our event loop, should the task
 * be scheduled again while it's still running, cancel the execution and
 * reschedule for the next time. */
	echs_task_t t = (void*)w;

	/* the global context holds the currently running child
	 * if there is one running, defer the execution of this task */
	if (!t->c.pid) {
		/* indicate that we might want to reuse the loop */
		ev_loop_fork(EV_A);

		/* unlike A tasks these will run only once
		 * keep track of the spawned child pid and register
		 * a watcher for status changes */
		with (pid_t p = run_task(t, false)) {
			ECHS_NOTI_LOG("supervising pid %d", p);
			ev_child_init(&t->c, chld_cb, p, false);
			ev_child_start(EV_A_ &t->c);
		}
	}

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
/* the A queue doesn't wait for the jobs to finish, it is asynchronous
 * however jobs will only be timed AFTER NOW. */
	echs_task_t t = (void*)w;
	echs_evstrm_t s = t->strm;
	echs_event_t e;
	ev_tstamp soon;

	if (UNLIKELY(echs_event_0_p(e = unwind_till(s, now)))) {
		ECHS_NOTI_LOG("event in the past, will not schedule");
		return 0.;
	}

	soon = instant_to_tstamp(e.from);
	t->cmd = obint_name(e.sum);
	t->env = NULL;

	ECHS_NOTI_LOG("next run %f", soon);
	return soon;
}

static void
shut_conn(struct echs_conn_s *c)
{
/* shuts a connection partially or fully down */
	const int fd = c->r.fd;

	if (fd > 0) {
		/* just shutdown all */
		shutdown(fd, SHUT_RDWR);
	}
	close(fd);
	free_conn(c);
	return;
}

static void
sock_data_cb(EV_P_ ev_io *w, int UNUSED(revents))
{
	char buf[4096U];
	const int fd = w->fd;
	ssize_t nrd;

	nrd = read(fd, buf, sizeof(buf));
	fwrite(buf, 1, nrd, stdout);

	ev_io_stop(EV_A_ w);
	shut_conn((struct echs_conn_s*)w);
	return;
}

static void
sock_conn_cb(EV_P_ ev_io *w, int UNUSED(revents))
{
	struct echs_conn_s *ec;
	struct sockaddr_un sa;
	socklen_t z = sizeof(sa);
	int s;

	if ((s = accept(w->fd, (struct sockaddr*)&sa, &z)) < 0) {
		ECHS_ERR_LOG("connection vanished");
		return;
	}
	/* very good, get us an io watcher */
	ECHS_NOTI_LOG("connection from `%s'", sa.sun_path);
	if (UNLIKELY((ec = make_conn()) == NULL)) {
		ECHS_ERR_LOG("too many concurrent connections");
		close(s);
		return;
	}
	ev_io_init(&ec->r, sock_data_cb, s, EV_READ);
	ev_io_start(EV_A_ &ec->r);
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
	struct _echsd_s *res = calloc(1, sizeof(*res));
	EV_P = ev_default_loop(EVFLAG_AUTO);

	if (res == NULL) {
		return NULL;
	} else if (EV_A == NULL) {
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

	res->loop = EV_A;
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

static void
echsd_inject_evstrm1(struct _echsd_s *ctx, echs_evstrm_t s, void(*cb)())
{
	echs_task_t t;
	EV_P = ctx->loop;

	if (UNLIKELY((t = make_task()) == NULL)) {
		ECHS_ERR_LOG("cannot submit new task");
		return;
	}

	/* store the stream */
	t->strm = s;
	ev_periodic_init(&t->w, cb, 0./*ignored*/, 0., resched);
	ev_periodic_start(EV_A_ &t->w);
	return;
}

static void
echsd_inject_evstrm(struct _echsd_s *ctx, echs_evstrm_t s, void(*cb)())
{
	echs_evstrm_t strm[64U];

	for (size_t tots = 0U, nstrm;
	     (nstrm = echs_evstrm_demux(strm, countof(strm), s, tots)) > 0U ||
		     tots == 0U; tots += nstrm) {
		/* we've either got some streams or our stream S isn't muxed */
		if (nstrm == 0) {
			/* put original stream as task */
			echsd_inject_evstrm1(ctx, s, cb);
			break;
		}
		for (size_t i = 0U; i < nstrm; i++) {
			echsd_inject_evstrm1(ctx, strm[i], cb);
		}
	}
	return;
}

static void
echsd_inject_sock(struct _echsd_s *ctx, int s)
{
	EV_P = ctx->loop;

	ev_io_init(&ctx->ctlsock, sock_conn_cb, s, EV_READ);
	ev_io_start(EV_A_ &ctx->ctlsock);
	return;
}


#include "echsd.yucc"

int
main(int argc, char *argv[])
{
	yuck_t argi[1U];
	struct _echsd_s *ctx;
	const char *edir;
	echs_evstrm_t s;
	int esok = -1;
	int rc = 0;

	/* best not to be signalled for a minute */
	block_sigs();

	if (yuck_parse(argi, argc, argv) < 0) {
		rc = 1;
		goto out;
	}

	/* try and find our execution helper */
	if (UNLIKELY((echsx = get_echsx()) == NULL)) {
		perror("Error: cannot find execution helper `echsx'");
		rc = 1;
		goto out;
	}

	/* read queues, we've got one echs file per queue */
	if (UNLIKELY((edir = get_vardir()) == NULL)) {
		perror("Error: cannot obtain local state directory");
		rc = 1;
		goto out;
	}

	if (UNLIKELY((esok = make_socket(edir)) < 0)) {
		perror("Error: cannot create socket file");
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

	/* inject the echsd socket */
	echsd_inject_sock(ctx, esok);

	/* inject our state */
	with (const char *qfn = pathcat(edir, "echsq_a.ics", NULL)) {
		if (UNLIKELY((s = make_echs_evstrm_from_file(qfn)) == NULL)) {
			ECHS_NOTI_LOG("\
queue file `%s' does not exist ...", qfn);
			break;
		}
		echsd_inject_evstrm(ctx, s, taskA_cb);
	}

	with (const char *qfn = pathcat(edir, "echsq_b.ics", NULL)) {
		if (UNLIKELY((s = make_echs_evstrm_from_file(qfn)) == NULL)) {
			ECHS_NOTI_LOG("\
queue file `%s' does not exist ...", qfn);
			break;
		}
		echsd_inject_evstrm(ctx, s, taskB_cb);
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


	free_echs_evstrm(s);
	free_echsd(ctx);

clo:
	/* stop them log files */
	echs_closelog();

out:
	if (esok >= 0) {
		(void)free_socket(esok, edir);
	}
	yuck_free(argi);
	return rc;
}

/* echsd.c ends here */
