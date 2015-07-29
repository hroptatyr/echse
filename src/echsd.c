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
#include <dirent.h>
#if defined __FreeBSD__
# include <sys/syscall.h>
#endif	/* __FreeBSD__ */
#if defined HAVE_UCRED_H
# include <ucred.h>
#endif	/* HAVE_UCRED_H */
#ifdef HAVE_SYS_UCRED_H
# include <sys/ucred.h>
#endif	/* HAVE_SYS_UCRED_H */
#if defined HAVE_SENDFILE
# include <sys/sendfile.h>
#endif	/* HAVE_SENDFILE */
#if defined HAVE_PATHS_H
# include <paths.h>
#endif	/* HAVE_PATHS_H */
#include <spawn.h>
#include <pwd.h>
#include <grp.h>
#include <ev.h>
#include "echse.h"
#include "evical.h"
#include "logger.h"
#include "nifty.h"

#if defined __INTEL_COMPILER
# define auto	static
#endif	/* __INTEL_COMPILER */

#undef EV_P
#define EV_P  struct ev_loop *loop __attribute__((unused))

#define STRERR	(strerror(errno))

typedef struct _task_s *_task_t;

typedef struct {
	uid_t u;
	gid_t g;
	const char *wd;
	const char *sh;
} ncred_t;

/* linked list of ev_periodic objects */
struct _task_s {
	/* beef data for libev and book-keeping */
	ev_periodic w;
	_task_t next;
	ev_child c;

	/* this is the task as understood by libechse */
	echs_task_t task;
	echs_evstrm_t strm;

	/* number of runs */
	size_t nrun;

	ncred_t dflt_cred;
};

struct _echsd_s {
	ev_signal sigint;
	ev_signal sighup;
	ev_signal sigterm;
	ev_signal sigpipe;

	ev_io ctlsock;

	struct ev_loop *loop;
};

#if !defined HAVE_STRUCT_UCRED
struct ucred {
	pid_t pid;
	uid_t uid;
	gid_t gid;
};
#endif	/* !HAVE_STRUCT_UCRED */

#if !defined _PATH_TMP
# define _PATH_TMP	"/tmp/"
#endif	/* _PATH_TMP */

static const char *echsx;
static int qdirfd = -1;
static struct ucred meself;


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

static char*
xmemmem(const char *hay, const size_t hayz, const char *ndl, const size_t ndlz)
{
	const char *const eoh = hay + hayz;
	const char *const eon = ndl + ndlz;
	const char *hp;
	const char *np;
	const char *cand;
	unsigned int hsum;
	unsigned int nsum;
	unsigned int eqp;

	/* trivial checks first
         * a 0-sized needle is defined to be found anywhere in haystack
         * then run strchr() to find a candidate in HAYSTACK (i.e. a portion
         * that happens to begin with *NEEDLE) */
	if (ndlz == 0UL) {
		return deconst(hay);
	} else if ((hay = memchr(hay, *ndl, hayz)) == NULL) {
		/* trivial */
		return NULL;
	}

	/* First characters of haystack and needle are the same now. Both are
	 * guaranteed to be at least one character long.  Now computes the sum
	 * of characters values of needle together with the sum of the first
	 * needle_len characters of haystack. */
	for (hp = hay + 1U, np = ndl + 1U, hsum = *hay, nsum = *hay, eqp = 1U;
	     hp < eoh && np < eon;
	     hsum ^= *hp, nsum ^= *np, eqp &= *hp == *np, hp++, np++);

	/* HP now references the (NZ + 1)-th character. */
	if (np < eon) {
		/* haystack is smaller than needle, :O */
		return NULL;
	} else if (eqp) {
		/* found a match */
		return deconst(hay);
	}

	/* now loop through the rest of haystack,
	 * updating the sum iteratively */
	for (cand = hay; hp < eoh; hp++) {
		hsum ^= *cand++;
		hsum ^= *hp;

		/* Since the sum of the characters is already known to be
		 * equal at that point, it is enough to check just NZ - 1
		 * characters for equality,
		 * also CAND is by design < HP, so no need for range checks */
		if (hsum == nsum && memcmp(cand, ndl, ndlz - 1U) == 0) {
			return deconst(cand);
		}
	}
	return NULL;
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
get_queudir(void)
{
	static const char spodir[] = "/var/spool";
	static const char appdir[] = ".echse";
	static char d[PATH_MAX];
	uid_t u;

	switch ((u = meself.uid)) {
		struct passwd *pw;
		struct stat st;
		size_t di;
	case 0:
		if (stat(spodir, &st) < 0 || !S_ISDIR(st.st_mode)) {
			break;
		}

		di = xstrlcpy(d, spodir, sizeof(d));
		d[di++] = '/';
		di += xstrlcpy(d + di, appdir + 1U, sizeof(d) - di);
		/* just mkdir the result and throw away errors */
		if (mkdir(d, 0700) < 0 && errno != EEXIST) {
			/* bollocks */
			break;
		}
		/* we consider our job done */
		return d;

	default:
		/* users have a tougher time since there's no local
		 * directory that serves the purpose of /var/spool/
		 * but where users have write permission
		 * also, since cron and at cannot be run without
		 * super-privileges we don't have any inspirations
		 * of how it could be done
		 * *sigh* let's put the queue files into ~/.echse
		 * then, prefixed with the machine we're working on */
		if ((pw = getpwuid(u)) == NULL || pw->pw_dir == NULL) {
			/* there's nothing else we can do */
			break;
		} else if (stat(pw->pw_dir, &st) < 0 || !S_ISDIR(st.st_mode)) {
			/* gimme a break! */
			break;
		}
		di = xstrlcpy(d, pw->pw_dir, sizeof(d));
		d[di++] = '/';
		di += xstrlcpy(d + di, appdir, sizeof(d) - di);
		if (mkdir(d, 0700) < 0 && errno != EEXIST) {
			/* plain horseshit again */
			break;
		}
		d[di++] = '/';
		/* now the machine name */
		if (gethostname(d + di, sizeof(d) - di) < 0) {
			/* is there anything that works on this machine? */
			break;
		}
		/* and mkdir it again, just in case */
		if (mkdir(d, 0700) < 0 && errno != EEXIST) {
			/* plain horseshit again */
			break;
		}
		/* now that's a big success */
		return d;
	}
	/* we've run out of options */
	return NULL;
}

static const char*
get_sockdir(void)
{
	static const char rundir[] = "/var/run";
	static const char appdir[] = ".echse";
	static char d[PATH_MAX];
	struct stat st;
	uid_t u;

	switch ((u = meself.uid)) {
		size_t di;
	case 0:
		/* barf right away when there's no /var/run */
		if (stat(rundir, &st) < 0 || !S_ISDIR(st.st_mode)) {
			return NULL;
		}

		di = xstrlcpy(d, rundir, sizeof(d));
		d[di++] = '/';
		di += xstrlcpy(d + di, appdir + 1U, sizeof(d) - di);
		/* just mkdir the result and throw away errors */
		if (mkdir(d, 0700) < 0 && errno != EEXIST) {
			/* it's just horseshit */
			break;
		}
		/* we consider our job done */
		return d;
	default:
		di = xstrlcpy(d, rundir, sizeof(d));
		d[di++] = '/';
		di += snprintf(d + di, sizeof(d) - di, "user/%u", u);
		if (stat(d, &st) < 0 || !S_ISDIR(st.st_mode)) {
			/* no /var/run/user/XXX,
			 * try /tmp/echse */
			goto tmpdir;
		}
		d[di++] = '/';
		di += xstrlcpy(d + di, appdir + 1U, sizeof(d) - di);
		/* now mkdir the result and throw away errors */
		if (mkdir(d, 0700) < 0 && errno != EEXIST) {
			/* plain horseshit again */
			break;
		}
		/* we consider our job done */
		return d;

	tmpdir:
		di = xstrlcpy(d, _PATH_TMP, sizeof(d));
		di += xstrlcpy(d + di, appdir + 1U, sizeof(d) - di);
		if (mkdir(d, 0700) < 0 && errno != EEXIST) {
			/* plain horseshit again */
			break;
		}
		/* now that's a big success */
		return d;
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
make_socket(const char *sdir)
{
	struct sockaddr_un sa = {.sun_family = AF_UNIX};
	const char *fn;
	size_t sz;
	int s;

	if (UNLIKELY((s = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)) {
		return -1;
	} else if (UNLIKELY(fcntl(s, F_SETFD, FD_CLOEXEC) < 0)) {
		return -1;
	}
	fn = pathcat(sdir, "=echsd", NULL);
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
free_socket(int s, const char *sdir)
{
	const char *fn = pathcat(sdir, "=echsd", NULL);
	int res = unlink(fn);
	close(s);
	return res;
}

static int
get_peereuid(uid_t *restrict uid, gid_t *restrict gid, int s)
{
/* return UID/GID pair of connected peer in S. */
#if defined SO_PEERCRED
	struct ucred c;
	socklen_t z = sizeof(c);

	if (getsockopt(s, SOL_SOCKET, SO_PEERCRED, &c, &z) < 0) {
		return -1;
	} else if (z != sizeof(c)) {
		errno = EINVAL;
		return -1;
	}
	*uid = c.uid;
	*gid = c.gid;
	return 0;
#elif defined LOCAL_PEERCRED
	struct xucred c;
	socklen_t z = sizeof(c);

	if (getsockopt(s, 0, LOCAL_PEERCRED, &c, &z) < 0) {
		return -1;
	} else if (z != sizeof(c)) {
		return -1;
	} else if (c.cr_version != XUCRED_VERSION) {
		return -1;
	}
	*uid = c.cr_uid;
	*gid = c.cr_gid;
	return 0;
#elif defined HAVE_GETPEERUCRED
	ucred_t *c = NULL;

	if (getpeerucred(s, &c) < 0) {
		return -1;
	}

	*uid = ucred_geteuid(c);
	*gid = ucred_getegid(c);
	ucred_free(c);

	if (*uid == (uid_t)(-1) || *gid == (gid_t)(-1)) {
		return -1;
	}
	return 0;
#else
	errno = ENOSYS;
	return -1;
#endif	/* SO_PEERCRED || LOCAL_PEERCRED || HAVE_GETPEERUCRED */
}

static ncred_t
compl_uid(uid_t u)
{
	struct passwd *p;

	if ((p = getpwuid(u)) == NULL) {
		return (ncred_t){0, 0, NULL, NULL};
	}
	return (ncred_t){p->pw_uid, p->pw_gid, p->pw_dir, p->pw_shell};
}

static ncred_t
compl_user(const char *u)
{
	struct passwd *p;

	if ((p = getpwnam(u)) == NULL) {
		return (ncred_t){0, 0, NULL, NULL};
	}
	return (ncred_t){p->pw_uid, p->pw_gid, p->pw_dir, p->pw_shell};
}

static ncred_t
cred_to_ncred(cred_t c)
{
/* turn string creds into numeric creds */
	ncred_t res = {0};

	if (c.u != NULL) {
		char *on;
		long unsigned int u = strtoul(c.u, &on, 10);

		if (*on == '\0') {
			/* oooh, we've got a numeric value already */
			res.u = (uid_t)u;
		} else {
			res = compl_user(c.u);
		}
	}
	if (c.g != NULL) {
		char *on;
		long unsigned int g = strtoul(c.g, &on, 10);
		struct group *p;

		if (*on == '\0') {
			/* oooh, we've got a numeric value already */
			res.g = (gid_t)g;
		} else if ((p = getgrnam(c.g)) != NULL) {
			res.g = p->gr_gid;
		}
	}
	if (c.wd) {
		res.wd = c.wd;
	}
	if (c.sh) {
		res.sh = c.sh;
	}
	return res;
}


/* task pool */
#define ECHS_TASK_POOL_INIZ	(256U)
static _task_t free_pers;
static size_t nfree_pers;
static size_t zfree_pers;

/* pool list */
struct plst_s {
	_task_t _1st;
	size_t size;
};

static struct plst_s *pools;
static size_t npools;
static size_t zpools;

static _task_t
make_task_pool(size_t n)
{
/* generate a pile of ev_periodics and chain them up */
	_task_t res;

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

static _task_t
make_task(void)
{
/* create one task */
	_task_t res;

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
free_task(_task_t t)
{
/* hand task T over to free list */
	if (LIKELY(t->dflt_cred.wd != NULL)) {
		free(deconst(t->dflt_cred.wd));
	}
	if (LIKELY(t->dflt_cred.sh != NULL)) {
		free(deconst(t->dflt_cred.sh));
	}

	t->next = free_pers;
	free_pers = t;
	nfree_pers++;
	return;
}

static pid_t
run_task(_task_t t, bool dtchp)
{
/* assumes ev_loop_fork() has been called */
	static char fileX[] = "/tmp/foo";
	static char uid[16U], gid[16U];
	static char *args_proto[] = {
		"echsx",
		"-c", NULL,
		"--stdout", fileX, "--stderr", fileX,
		"--uid", uid, "--gid", gid,
		"--cwd", NULL, "--shell", NULL,
		"--mailfrom", NULL,
		"-n",
		NULL
	};
	/* use a VLA for the real args */
	const size_t natt = t->task->att ? t->task->att->nl : 0U;
	char *args[countof(args_proto) + 2U * natt];
	char *const *env = deconst(t->task->env);
	pid_t r;

	/* set up the real args */
	memcpy(args, args_proto, sizeof(args_proto));
	with (ncred_t run_as = cred_to_ncred(t->task->run_as)) {
		if (!run_as.u) {
			run_as.u = t->dflt_cred.u;
		}
		if (!run_as.g) {
			run_as.g = t->dflt_cred.g;
		}
		snprintf(uid, sizeof(uid), "%u", (uid_t)run_as.u);
		snprintf(gid, sizeof(gid), "%u", (gid_t)run_as.g);

		if (!run_as.wd) {
			run_as.wd = t->dflt_cred.wd;
		}
		if (!run_as.sh) {
			run_as.sh = t->dflt_cred.sh;
		}
		args[12U] = deconst(run_as.wd);
		args[14U] = deconst(run_as.sh);
	}
	args[2U] = deconst(t->task->cmd);
	if (t->task->org) {
		args[16U] = deconst(t->task->org);
	} else if (natt) {
		args[16U] = t->task->att->l[0U];
	} else {
		args[16U] = "echse";
	}
	with (size_t i = 17U) {
		for (size_t j = 0U; j < natt; j++) {
			args[i++] = "--mailto";
			args[i++] = t->task->att->l[j];
		}
		if (!dtchp) {
			args[i++] = "-n";
		}
		/* finalise args array */
		args[i++] = NULL;
	}

	/* finally fork out our child */
	if (UNLIKELY(posix_spawn(&r, echsx, NULL, NULL, args, env) < 0)) {
		ECHS_ERR_LOG("cannot fork: %s", STRERR);
	} else if (dtchp) {
		int rc;
		while (waitpid(r, &rc, 0) != r);
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
	t += 11322/*days from unix epoch to our epoch*/ * 86400UL;
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


/* s2c communication */
typedef enum {
	ECHS_CMD_UNK,
	ECHS_CMD_LIST,

	/* to indicate that we need more data to finish the command */
	ECHS_CMD_MORE = -1
} echs_cmd_t;

struct echs_cmd_list_s {
	const char *fn;
	size_t len;
};

struct echs_cmdparam_s {
	echs_cmd_t cmd;
	union {
		struct echs_cmd_list_s list;
	};
};

static echs_cmd_t
cmd_list_p(struct echs_cmdparam_s param[static 1U], const char *buf, size_t bsz)
{
	static const char ht_verb[] = "GET ";
	static const char ht_vers[] = " HTTP/1.1\r\n";
	const char *ep;

	if (!memcmp(buf, ht_verb, strlenof(ht_verb)) &&
	    (ep = xmemmem(buf, bsz, ht_vers, strlenof(ht_vers)))) {
		/* aaaah http shit and looks like a GET request */
		const char *bp;

		for (; ep > buf && ep[-1] == ' '; ep--);
		for (bp = ep; bp > buf && bp[-1] != '/'; bp--);
		param->cmd = ECHS_CMD_LIST;
		param->list = (struct echs_cmd_list_s){bp, ep - bp};
		return xmemmem(ep, bsz - (ep - buf), "\r\n\r\n", 4U)
			? ECHS_CMD_LIST : ECHS_CMD_MORE;
	}
	return ECHS_CMD_UNK;
}

static ssize_t
cmd_list(int ofd, const struct echs_cmd_list_s cmd[static 1U])
{
	static const char rpl403[] = "\
HTTP/1.1 403 Forbidden\r\n\r\n";
	static const char rpl404[] = "\
HTTP/1.1 404 Not Found\r\n\r\n";
	static const char rpl200[] = "\
HTTP/1.1 200 Ok\r\n\r\n";
	char fn[cmd->len + 1U];
	const char *rpl;
	size_t rpz;
	struct stat st;
	int fd = -1;
	ssize_t nwr = 0;

	memcpy(fn, cmd->fn, cmd->len);
	fn[cmd->len] = '\0';
	if (fstatat(qdirfd, fn, &st, 0) < 0) {
		rpl = rpl404, rpz = strlenof(rpl404);
	} else if ((fd = openat(qdirfd, fn, O_RDONLY)) < 0) {
		rpl = rpl403, rpz = strlenof(rpl403);
	} else {
		rpl = rpl200, rpz = strlenof(rpl200);
	}
	/* write reply header */
	nwr += write(ofd, rpl, rpz);
	/* and content, if any */
#if defined HAVE_SENDFILE
	if (!(fd < 0)) {
		size_t fz = st.st_size;

		for (ssize_t nsf;
		     fz && (nsf = sendfile(ofd, fd, NULL, fz)) > 0;
		     fz -= nsf, nwr += nsf);
	}
#endif	/* HAVE_SENDFILE */
	return nwr;
}

static echs_cmd_t
guess_cmd(struct echs_cmdparam_s param[static 1U], const char *buf, size_t bsz)
{
	echs_cmd_t r = ECHS_CMD_UNK;

	if ((r = cmd_list_p(param, buf, bsz))) {
		;
	} else if (0) {
		;
	}
	return r;
}

static ssize_t
handle_cmd(int fd, const struct echs_cmdparam_s param[static 1U])
{
	ssize_t nwr = 0;

	switch (param->cmd) {
	case ECHS_CMD_LIST:
		nwr = cmd_list(fd, &param->list);
		break;

	case ECHS_CMD_MORE:
	default:
		/* just do fuckall */
		break;
	}
	return nwr;
}


/* libev conn handling */
#define MAX_CONNS	(sizeof(free_conns) * 8U)
#define MAX_QUEUE	MAX_CONNS
static uint64_t free_conns = -1;
static struct echs_conn_s {
	ev_io r;
	/* i/o buffer, its size and an offset */
	char *buf;
	size_t bsz;
	off_t bix;
	/* the command we're building up
	 * this contains a partial or full parse of all parameters */
	struct echs_cmdparam_s cmd[1U];
} conns[MAX_CONNS];

static struct echs_conn_s*
make_conn(void)
{
	int i = ffs(free_conns & 0xffffffffU)
		?: ffs(free_conns >> 32U & 0xffffffffU);

	if (LIKELY(i-- > 0)) {
		/* toggle bit in free conns */
		free_conns ^= 1ULL << i;

		conns[i].buf = malloc(conns[i].bsz = 4096U);
		conns[i].bix = 0;

		memset(conns[i].cmd, 0, sizeof(*conns[i].cmd));

		if (LIKELY(conns[i].buf != NULL)) {
			return conns + i;
		}
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
	/* also free buffer resources */
	if (LIKELY(c->buf != NULL)) {
		free(c->buf);
	}
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
unsched(EV_P_ ev_periodic *w, int UNUSED(revents))
{
	_task_t t = (void*)w;

	ECHS_NOTI_LOG("taking event off of schedule");
	ev_periodic_stop(EV_A_ w);
	free_task(t);
	return;
}

static void
chld_cb(EV_P_ ev_child *w, int UNUSED(revents))
{
	_task_t t = (void*)((char*)w - offsetof(struct _task_s, c));

	ECHS_NOTI_LOG("chld %d coughed: %d", w->rpid, w->rstatus);
	ev_child_stop(EV_A_ w);
	w->rpid = w->pid = 0;

	if (UNLIKELY(t->w.reschedule_cb == NULL)) {
		/* we promised taskB_cb to kill this guy */
		unsched(EV_A_ &t->w, 0);
	}
	return;
}

static void
taskA_cb(EV_P_ ev_periodic *w, int UNUSED(revents))
{
/* A tasks always run asynchronously, echsx decouples itself from echsd
 * and manages the job with no further interaction */
	_task_t t = (void*)w;

	/* indicate that we might want to reuse the loop */
	ev_loop_fork(EV_A);

	/* these are completely asynchronous with no further monitoring
	 * so fire and forget */
	(void)run_task(t, true);

	/* prepare for rescheduling */
	if (UNLIKELY(w->reschedule_cb == NULL)) {
		/* that's the end of our streak */
		unsched(EV_A_ w, 0);
	}
	return;
}

static void
taskB_cb(EV_P_ ev_periodic *w, int UNUSED(revents))
{
/* B tasks always run under supervision of our event loop, should the task
 * be scheduled again while it's still running, cancel the execution and
 * reschedule for the next time. */
	_task_t t = (void*)w;

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

	/* prepare for rescheduling */
	if (UNLIKELY(w->reschedule_cb == NULL)) {
		/* the child watcher will reap this task */
		;
	}
	return;
}

static ev_tstamp
resched(ev_periodic *w, ev_tstamp now)
{
/* the A queue doesn't wait for the jobs to finish, it is asynchronous
 * however jobs will only be timed AFTER NOW.
 * This will be called BEFORE the actual callback is called so we have
 * to defer unschedule operations by one. */
	_task_t t = (void*)w;
	echs_evstrm_t s = t->strm;
	echs_event_t e = unwind_till(s, now);
	ev_tstamp soon;

	if (UNLIKELY(echs_event_0_p(e) && !t->nrun)) {
		/* this has never been run in the first place */
		ECHS_NOTI_LOG("event in the past, not scheduling");
		w->reschedule_cb = NULL;
		w->cb = unsched;
		return now;
	} else if (UNLIKELY(echs_event_0_p(e))) {
		/* we need to unschedule AFTER the next run */
		ECHS_NOTI_LOG("event completed, will not reschedule");
		w->reschedule_cb = NULL;
		return now + 1.e+30;
	}

	soon = instant_to_tstamp(e.from);
	t->task = e.task;
	t->nrun++;

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
	struct echs_conn_s *c = (void*)w;
	const int fd = c->r.fd;
	ssize_t nrd;

	if (UNLIKELY((nrd = read(fd, c->buf + c->bix, c->bsz - c->bix)) < 0)) {
		goto shut;
	}
	/* check the command we're supposed to obey */
	switch (guess_cmd(c->cmd, c->buf, c->bix += nrd)) {
	default:
		handle_cmd(fd, c->cmd);
		goto shut;

	case ECHS_CMD_MORE:
		/* don't close the connection, prepare for more */
		if ((size_t)c->bix >= c->bsz) {
			c->buf = realloc(c->buf, c->bsz *= 2U);
			if (UNLIKELY(c->buf == NULL)) {
				goto shut;
			}
		}
		break;
	}
	return;

shut:
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
	uid_t u;
	gid_t g;
	int s;

	if (UNLIKELY(get_peereuid(&u, &g, w->fd) < 0)) {
		ECHS_ERR_LOG("\
authenticity of connection %d cannot be established: %s", w->fd, STRERR);
		return;
	} else if ((s = accept(w->fd, (struct sockaddr*)&sa, &z)) < 0) {
		ECHS_ERR_LOG("connection vanished: %s", STRERR);
		return;
	}

	/* very good, get us an io watcher */
	ECHS_NOTI_LOG("connection from %u/%u", u, g);
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
_inject_evstrm1(struct _echsd_s *ctx, echs_evstrm_t s, uid_t u, void(*cb)())
{
	_task_t t;
	EV_P = ctx->loop;

	if (UNLIKELY((t = make_task()) == NULL)) {
		ECHS_ERR_LOG("cannot submit new task");
		return;
	}

	/* store the stream */
	t->strm = s;
	/* run all tasks as U and the default group of U */
	t->dflt_cred = compl_uid(u);
	/* also, by default, in U's home dir using U's shell */
	t->dflt_cred.wd = strdup(t->dflt_cred.wd);
	t->dflt_cred.sh = strdup(t->dflt_cred.sh);

	ev_periodic_init(&t->w, cb, 0./*ignored*/, 0., resched);
	ev_periodic_start(EV_A_ &t->w);
	return;
}

static void
_inject_evstrm(struct _echsd_s *ctx, echs_evstrm_t s, uid_t u, void(*cb)())
{
	echs_evstrm_t strm[64U];

	for (size_t tots = 0U, nstrm;
	     (nstrm = echs_evstrm_demux(strm, countof(strm), s, tots)) > 0U ||
		     tots == 0U; tots += nstrm) {
		/* we've either got some streams or our stream S isn't muxed */
		if (nstrm == 0) {
			/* put original stream as task */
			_inject_evstrm1(ctx, s, u, cb);
			break;
		}
		for (size_t i = 0U; i < nstrm; i++) {
			_inject_evstrm1(ctx, strm[i], u, cb);
		}
	}
	return;
}

static void
_inject_file(struct _echsd_s *ctx, const char *fn, uid_t run_as, void(*cb)())
{
	char buf[65536U];
	ical_parser_t pp = NULL;
	size_t nrd;
	int fd;

	if (UNLIKELY(run_as >= (uid_t)~0UL)) {
		return;
	} else if (meself.uid && meself.uid != run_as) {
		return;
	}

	if ((fd = openat(qdirfd, fn, O_RDONLY)) < 0) {
		return;
	}

more:
	switch ((nrd = read(fd, buf, sizeof(buf)))) {
		echs_evstrm_t s;

	default:
		if (echs_evical_push(&pp, buf, nrd) < 0) {
			/* pushing more brings nothing */
			break;
		}
	case 0:
		while ((s = echs_evical_pull(&pp)) != NULL) {
			_inject_evstrm(ctx, s, run_as, cb);
		}
		if (LIKELY(nrd > 0)) {
			goto more;
		}
		break;
	case -1:
		if (UNLIKELY((s = echs_evical_last_pull(&pp)) != NULL)) {
			/* close right away */
			free_echs_evstrm(s);
		}
		break;
	}

	close(fd);
	return;
}

static void
echsd_inject_queues(struct _echsd_s *ctx, const char *qd)
{
	uid_t u = meself.uid;

	/* we are a super-echsd, load all .ics files we can find */
	if_with (DIR *d, d = opendir(qd)) {
		static const char prfx[] = "echsq_";
		static const char sufx[] = ".ics";

		for (struct dirent *dp; (dp = readdir(d)) != NULL;) {
			const char *fn = dp->d_name;
			long unsigned int xu;
			char *on;
			unsigned char qi;
			void(*cb)();

			/* check if it's the right prefix */
			if (strncmp(fn, prfx, strlenof(prfx))) {
				/* not our thing */
				continue;
			}
			/* snarf the uid */
			xu = strtoul(fn + strlenof(prfx), &on, 10);
			/* looking good? */
			if (UNLIKELY(xu >= (uid_t)~0UL)) {
				continue;
			} else if (u && xu != u) {
				continue;
			}
			/* has it got A or B indicator? */
			qi = (unsigned char)*on++;
			switch (qi | 0x20U) {
			case 'a':
				cb = taskA_cb;
				break;
			case 'b':
				cb = taskB_cb;
				break;
			default:
				continue;
			}
			/* check suffix */
			if (strncmp(on, sufx, strlenof(sufx))) {
				/* nope */
				continue;
			}
			/* otherwise, try and load it */
			_inject_file(ctx, fn, xu, cb);
		}
		closedir(d);
	}
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
	const char *qdir;
	const char *sdir;
	int esok = -1;
	int rc = 0;

	/* best not to be signalled for a minute */
	block_sigs();

	if (yuck_parse(argi, argc, argv) < 0) {
		rc = 1;
		goto out;
	}

	/* who are we? */
	meself.pid = getpid();
	meself.uid = geteuid();
	meself.gid = getegid();

	/* try and find our execution helper */
	if (UNLIKELY((echsx = get_echsx()) == NULL)) {
		perror("Error: cannot find execution helper `echsx'");
		rc = 1;
		goto out;
	}

	/* read queues, we've got one echs file per queue */
	if (UNLIKELY((qdir = get_queudir()) == NULL)) {
		perror("Error: cannot obtain local state directory");
		rc = 1;
		goto out;
	} else if (UNLIKELY((qdirfd = open(qdir, O_RDONLY | O_CLOEXEC)) < 0)) {
		perror("Error: cannot open echsd spool directory");
		rc = 1;
		goto out;
	} else if (UNLIKELY((sdir = get_sockdir()) == NULL)) {
		perror("Error: cannot find directory to put the socket in");
		rc = 1;
		goto out;
	}

	if (UNLIKELY((esok = make_socket(sdir)) < 0)) {
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

	/* inject our state, i.e. read all echsq files */
	echsd_inject_queues(ctx, qdir);

	/* main loop */
	{
		/* set out sigs loose */
		unblock_sigs();
		/* and here we go */
		echsd_run(ctx);
		/* not reached */
		block_sigs();
	}

	/* free context and associated resources */
	free_echsd(ctx);

clo:
	/* stop them log files */
	echs_closelog();

out:
	if (esok >= 0) {
		(void)free_socket(esok, sdir);
	}
	if (qdirfd >= 0) {
		close(qdirfd);
	}
	yuck_free(argi);
	return rc;
}

/* echsd.c ends here */
