/*** echsd.c -- echse queue daemon
 *
 * Copyright (C) 2013-2020 Sebastian Freundt
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
#include "fdprnt.h"
#include "nifty.h"
#include "sock.h"
#include "nedtrie.h"
/* for rescheduling */
#include "evfilt.h"
/* for user/group mappings */
#include "nummapstr.h"

#if defined __INTEL_COMPILER
# define auto	static
#endif	/* __INTEL_COMPILER */

#undef EV_P
#define EV_P  struct ev_loop *loop __attribute__((unused))

#define STRERR	(strerror(errno))

#if defined __linux__
# define USE_ABSTRACT_SOCKETS	1
#else  /* !__linux__ */
# define USE_ABSTRACT_SOCKETS	0
#endif	/* __linux__ */

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

	/* currently scheduled run-time */
	echs_instant_t cur;
	echs_idiff_t dur;

	/* this is the task as understood by libechse */
	echs_task_t t;

	/* number of runs */
	size_t nrun;
	/* number of concurrent runs */
	size_t nsim;

	ncred_t dflt_cred;
};

struct _echsd_s {
	ev_signal sigint;
	ev_signal sighup;
	ev_signal sigterm;
	ev_signal sigpipe;

	/* checkpoint timer */
	ev_timer cptim;

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
static char hname[HOST_NAME_MAX];
static size_t hnamez;


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

static size_t
xstrlcpy(char *restrict dst, const char *src, size_t dsz)
{
	return xstrlncpy(dst, dsz, src, strlen(src));
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
	ssize_t z;

	if (UNLIKELY((z = readlink(myself, wd, sizeof(wd))) < 0)) {
		return NULL;
	} else if (UNLIKELY((size_t)z >= sizeof(wd))) {
		return NULL;
	}
	/* otherwise we can count ourselves lucky */
	wd[z] = '\0';
	return wd;
#elif defined __NetBSD__
	static const char myself[] = "/proc/curproc/exe";
	static char wd[PATH_MAX];
	ssize_t z;

	if (UNLIKELY((z = readlink(myself, wd, sizeof(wd))) < 0)) {
		return NULL;
	} else if (UNLIKELY((size_t)z >= sizeof(wd))) {
		return NULL;
	}
	/* otherwise we can count ourselves lucky */
	wd[z] = '\0';
	return wd;
#elif defined __DragonFly__
	static const char myself[] = "/proc/curproc/file";
	static char wd[PATH_MAX];
	ssize_t z;

	if (UNLIKELY((z = readlink(myself, wd, sizeof(wd))) < 0)) {
		return NULL;
	} else if (UNLIKELY((size_t)z >= sizeof(wd))) {
		return NULL;
	}
	/* otherwise we can count ourselves lucky */
	wd[z] = '\0';
	return wd;
#elif defined __FreeBSD__
	static char wd[PATH_MAX];
	size_t z = sizeof(wd) - 1U;
	int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1};

	/* make sure that \0 terminator fits */
	wd[z] = '\0';
	if (UNLIKELY(sysctl(mib, countof(mib), wd, &z, NULL, 0) < 0)) {
		return NULL;
	}
	return wd;
#elif defined __sun || defined sun
	static char wd[MAXPATHLEN];
	ssize_t z;

	snprintf(wd, sizeof(wd), "/proc/%d/path/a.out", getpid());

	if (UNLIKELY((z = readlink(wd, wd, sizeof(wd))) < 0)) {
		return NULL;
	} else if (UNLIKELY((size_t)z >= sizeof(wd))) {
		return NULL;
	}
	/* otherwise we can count ourselves lucky */
	wd[z] = '\0';
	return wd;
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

		di = xstrlncpy(d, sizeof(d), spodir, strlenof(spodir));
		if (LIKELY(di < sizeof(d))) {
			d[di++] = '/';
		}
		di = xstrlncpy(
			d + di, sizeof(d) - di,
			appdir + 1U, strlenof(appdir) - 1U);
		/* check that we're actually making the directory we need */
		if (UNLIKELY(di != strlenof(appdir) - 1U)) {
			/* nope */
			break;
		}
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
		if (LIKELY(di + 1U < sizeof(d))) {
			d[di++] = '/';
		}
		di += xstrlncpy(
			d + di, sizeof(d) - di,
			appdir, strlenof(appdir));
		if (mkdir(d, 0700) < 0 && errno != EEXIST) {
			/* plain horseshit again */
			break;
		}
		if (LIKELY(di < sizeof(d))) {
			d[di++] = '/';
		}
		/* now the machine name */
		di = xstrlncpy(d + di, sizeof(d) - di, hname, hnamez);

		/* check that we're actually making the directory we need */
		if (UNLIKELY(di != hnamez)) {
			/* frigg */
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

static ssize_t
make_ssock(char *restrict buf, size_t bsz)
{
#if USE_ABSTRACT_SOCKETS
/* are we glad that we have abstract sockets? */
	static const char sockfn[] = "@/var/run/echse/=echsd";

	if (UNLIKELY(bsz < sizeof(sockfn))) {
		return -1;
	}
	/* otherwise just use that string */
	memcpy(buf, sockfn, sizeof(sockfn));
	*buf = '\0';
	return strlenof(sockfn);

#else  /* !USE_ABSTRACT_SOCKETS */
	static const char rundir[] = "/var/run";
	static const char appdir[] = ".echse";
	static const char sockfn[] = "=echsd";
	struct stat st;
	size_t bi;

	/* barf right away when there's no /var/run */
	if (stat(rundir, &st) < 0 || !S_ISDIR(st.st_mode)) {
		return -1;
	}

	/* join rundir and appdir */
	bi = xstrlncpy(buf, bsz, rundir, strlenof(rundir));
	if (LIKELY(bi < bsz)) {
		buf[bi++] = '/';
	}
	bi += xstrlncpy(buf + bi, bsz - bi, appdir + 1U, strlenof(appdir) - 1U);
	/* just mkdir the result and throw away errors */
	if (mkdir(buf, 0700) < 0 && errno != EEXIST) {
		/* it's just horseshit */
		return -1;
	}
	if (LIKELY(bi < bsz)) {
		buf[bi++] = '/';
	}
	bi += xstrlncpy(buf + bi, bsz - bi, sockfn, strlenof(sockfn));
	return bi;
#endif	/* USE_ABSTRACT_SOCKETS */
}

static ssize_t
make_usock(char *restrict buf, size_t bsz, uid_t u)
{
#if USE_ABSTRACT_SOCKETS
/* aren't we glad about those things? */
	static const char sockfn[] = "@/var/run/echse/=echsd";
	size_t bi;

	if (UNLIKELY(bsz < sizeof(sockfn) + 6U/*for numeric user*/)) {
		return -1;
	}
	memcpy(buf, sockfn, bi = strlenof(sockfn));
	with (int rc = snprintf(buf + bi, bsz - bi, "-%u", u)) {
		if (rc <= 0) {
			return -1;
		}
		bi += rc;
	}
	*buf = '\0';
	return bi;

#else  /* !USE_ABSTRACT_SOCKETS */
	static const char appdir[] = ".echse";
	static const char sockfn[] = "=echsd";
	struct passwd *pw;
	struct stat st;
	size_t bi;

	/* just like the queudir case we have no consistent
	 * way of writing our sockets somewhere locally as
	 * user (sort of an equivalent of /var/run)
	 * we've seen on openSuSE that systemd's tmpfiles.d
	 * system is so aggressive as to either not give us
	 * /var/run/user/UID/ or just deleting stuff in there
	 * after a certain amount of time.
	 * Same goes for /tmp, these days on systemd linux
	 * boxen files in /tmp will be deleted after a while.
	 * So to cut this long story short, we're using the
	 * home directory just like in get_queudir() */
	if ((pw = getpwuid(u)) == NULL || pw->pw_dir == NULL) {
		/* there's nothing else we can do */
		return -1;
	} else if (stat(pw->pw_dir, &st) < 0 || !S_ISDIR(st.st_mode)) {
		/* gimme a break! */
		return -1;
	}
	/* join homedir and appdir */
	bi = xstrlcpy(buf, pw->pw_dir, bsz);
	if (LIKELY(bi < bsz)) {
		buf[bi++] = '/';
	}
	bi += xstrlncpy(buf + bi, bsz - bi, appdir, strlenof(appdir));
	/* create the appdir if not already there */
	if (mkdir(buf, 0700) < 0 && errno != EEXIST) {
		/* plain horseshit again */
		return -1;
	}
	/* ok, append machine name now */
	if (LIKELY(bi < bsz)) {
		buf[bi++] = '/';
	}
	bi += xstrlncpy(buf + bi, bsz - bi, hname, hnamez);
	/* and mkdir it again, just in case */
	if (mkdir(buf, 0700) < 0 && errno != EEXIST) {
		/* plain horseshit again */
		return -1;
	}
	/* now for our final trick, append the sockfn */
	if (LIKELY(bi < bsz)) {
		buf[bi++] = '/';
	}
	bi += xstrlncpy(buf + bi, bsz - bi, sockfn, strlenof(sockfn));
	return bi;
#endif	/* !USE_ABSTRACT_SOCKETS */
}

static int
get_peereuid(ncred_t *restrict cred, int s)
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
	*cred = (ncred_t){c.uid, c.gid};
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
	*cred = (ncred_t){c.c.cr_uid, c.cr_gid};
	return 0;
#elif defined HAVE_GETPEERUCRED
	ucred_t *c = NULL;

	if (getpeerucred(s, &c) < 0) {
		return -1;
	}

	cred->uid = ucred_geteuid(c);
	cred->gid = ucred_getegid(c);
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

static int
set_peereuid(int s)
{
/* enable the passing of credentials on this socket. */
#if defined SO_PASSCRED
	int yes = 1;

	return setsockopt(s, SOL_SOCKET, SO_PASSCRED, &yes, sizeof(yes));
#else
	return 0;
#endif	/* SO_PASSCRED */
}

static int
make_socket(void)
{
	struct sockaddr_un sa = {.sun_family = AF_UNIX};
	size_t sz;
	int s;

	if (UNLIKELY((s = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)) {
		return -1;
	} else if (UNLIKELY(fd_cloexec(s) < 0)) {
		goto fail;
	}

	if (!meself.uid) {
		ssize_t tmp = make_ssock(sa.sun_path, sizeof(sa.sun_path));

		if (UNLIKELY(tmp < 0)) {
			goto fail;
		}
		/* otherwise we're good to go nuclear */
		sz = tmp;
	} else {
		ssize_t tmp = make_usock(
			sa.sun_path, sizeof(sa.sun_path), meself.uid);

		if (UNLIKELY(tmp < 0)) {
			goto fail;
		}
		/* otherwise three cheers for the user */
		sz = tmp;
	}

	/* go for gold */
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
free_socket(int s)
{
	struct sockaddr_un sa = {.sun_family = AF_UNIX};
	socklen_t sz = sizeof(sa);
	const char *fn;
	int rc = 0;

	if (UNLIKELY(getsockname(s, (void*)&sa, &sz) < 0)) {
		rc = -1;
	} else if ((fn = sa.sun_path) == NULL || !*fn) {
		/* abstract socket? */
		;
	} else if (UNLIKELY(unlink(fn) < 0)) {
		rc = -1;
	}
	close(s);
	return rc;
}

#define NOT_A_UID	((uid_t)-1)
#define NOT_A_GID	((gid_t)-1)

static ncred_t
compl_uid(uid_t u)
{
	struct passwd *p;

	if (UNLIKELY(u == NOT_A_UID || (p = getpwuid(u)) == NULL)) {
		return (ncred_t){NOT_A_UID};
	}
	return (ncred_t){p->pw_uid, p->pw_gid, p->pw_dir, p->pw_shell};
}

static ncred_t
compl_user(const char *u)
{
	struct passwd *p;

	if (UNLIKELY(u == NULL || (p = getpwnam(u)) == NULL)) {
		return (ncred_t){NOT_A_UID};
	}
	return (ncred_t){p->pw_uid, p->pw_gid, p->pw_dir, p->pw_shell};
}

static ncred_t
compl_owner(nummapstr_t o)
{
	const char *tmps;
	uintptr_t tmpu;

	if ((tmps = nummapstr_str(o))) {
		/* numerify owner */
		return compl_user(tmps);
	} else if ((tmpu = nummapstr_num(o)) != NUMMAPSTR_NAN) {
		return compl_uid(tmpu);
	}
	/* else fucked */
	return (ncred_t){NOT_A_UID};
}

static inline __attribute__((const, pure)) uid_t
echs_task_owner(echs_task_t t)
{
	uintptr_t owner = nummapstr_num(t->owner);
	return owner != NUMMAPSTR_NAN ? owner : NOT_A_UID;
}

static inline __attribute__((const, pure)) bool
echs_task_owned_by_p(echs_task_t t, uid_t uid)
{
	return echs_task_owner(t) == uid;
}


/* task pool */
#define ECHS_TASK_POOL_INIZ	(256U)
static _task_t free_tasks;
static size_t nfree_tasks;
static size_t zfree_tasks;

/* pool list */
struct tlst_s {
	_task_t _1st;
	size_t size;
};

/* mapping from oid to task */
struct tmap_s {
	echs_toid_t oid;
	_task_t t;
};

static struct tlst_s *tpools;
static size_t ntpools;
static size_t ztpools;

static struct tmap_s *task_ht;
static size_t ztask_ht;

static int
ini_task_ht(void)
{
	if (UNLIKELY(!ztask_ht)) {
		/* instantiate hash table */
		const size_t iniz = 16U;

		task_ht = calloc(iniz, sizeof(*task_ht));
		if (UNLIKELY(task_ht == NULL)) {
			/* I want to kill myself */
			return -1;
		}
		/* that went well */
		ztask_ht = iniz;
	}
	return 0;
}

static ssize_t
put_task_slot(echs_toid_t oid)
{
/* find slot for OID for putting
 * if collision recommend new size for task_ht */
	size_t slot = oid & (ztask_ht - 1ULL);
	const echs_toid_t toid = task_ht[slot].oid;

	if (LIKELY(!toid)) {
		return slot;
	} else if (UNLIKELY(toid == oid)) {
		/* huh? that's very inconsistent */
		return slot;
	}
	/* calc new size */
	with (size_t nuz = 1ULL << (__builtin_ctzll(toid ^ oid) + 1U)) {
		struct tmap_s *nut = calloc(nuz, sizeof(*task_ht));

		assert(nuz > ztask_ht);
		if (UNLIKELY(nut == NULL)) {
			/* ah well */
			return -1;
		}
		for (size_t i = 0U; i < ztask_ht; i++) {
			/* we can't get any additional collisions */
			if (task_ht[i].oid) {
				const size_t si = task_ht[i].oid & (nuz - 1ULL);
				nut[si] = task_ht[i];
			}
		}
		/* reass */
		free(task_ht);
		task_ht = nut;
		ztask_ht = nuz;
		ECHS_NOTI_LOG("resized table of tasks to %zu", ztask_ht);
		slot = oid & (ztask_ht - 1ULL);
		assert(!task_ht[slot].oid);
	}
	return slot;
}

static size_t
get_task_slot(echs_toid_t oid)
{
/* find slot for OID for getting */
	for (size_t i = 16U/*retries*/, slot = oid & (ztask_ht - 1U); i; i--) {
		if (task_ht[slot].oid == oid) {
			return slot;
		}
	}
	return (size_t)-1ULL;
}

static _task_t
make_task_pool(size_t n)
{
/* generate a pile of _task_t's and chain them up */
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
	if (ntpools >= ztpools) {
		const size_t nuz = (ztpools * 2U) ?: 16U;
		void *nup = realloc(tpools, nuz * sizeof(*tpools));

		if (UNLIKELY(nup == NULL)) {
			/* shame */
			free(res);
			return NULL;
		}
		/* we're good to go */
		tpools = nup;
		ztpools = nuz;
	}
	tpools[ntpools]._1st = res;
	tpools[ntpools].size = n;
	ntpools++;
	return res;
}

static void
free_task_pools(void)
{
	if (tpools) {
		for (size_t i = 0U; i < ntpools; i++) {
			free(tpools[i]._1st);
		}
		free(tpools);
	}
	tpools = NULL;
	ntpools = ztpools = 0UL;
	return;
}

static _task_t
make_task(echs_toid_t oid)
{
/* create one task */
	ssize_t slot = put_task_slot(oid);
	_task_t res;

	if (UNLIKELY(slot < 0)) {
		ECHS_ERR_LOG("cannot find slot for task %lx", oid);
		return NULL;
	}

	if (UNLIKELY(!nfree_tasks)) {
		/* put some more task objects in the task pool */
		const size_t adz = zfree_tasks ?: ECHS_TASK_POOL_INIZ;

		free_tasks = make_task_pool(adz);
		if (UNLIKELY(free_tasks == NULL)) {
			/* grrrr */
			return NULL;
		}
		nfree_tasks = adz;
		zfree_tasks = zfree_tasks ? adz * 2U : ECHS_TASK_POOL_INIZ;
	}

	/* pop off the free list */
	res = free_tasks;
	free_tasks = free_tasks->next;
	nfree_tasks--;

	task_ht[slot] = (struct tmap_s){oid, res};
	memset(res, 0, sizeof(*res));
	return res;
}

static void
free_task(_task_t t)
{
/* hand task T over to free list */
	/* free from our task hash table */
	with (size_t i = get_task_slot(t->t->oid)) {
		if (UNLIKELY(i >= ztask_ht || task_ht[i].oid != t->t->oid)) {
			/* that's no good :O */
			ECHS_NOTI_LOG("inconsistent table of tasks");
			break;
		}
		task_ht[i] = (struct tmap_s){0U, NULL};
	}

	if (LIKELY(t->dflt_cred.wd != NULL)) {
		free(deconst(t->dflt_cred.wd));
	}
	if (LIKELY(t->dflt_cred.sh != NULL)) {
		free(deconst(t->dflt_cred.sh));
	}
	free_echs_task(t->t);

	t->next = free_tasks;
	free_tasks = t;
	nfree_tasks++;
	return;
}

static _task_t
get_task(echs_toid_t oid)
{
/* find the task with oid OID. */
	size_t slot;

	if (UNLIKELY(!ztask_ht)) {
		return NULL;
	} else if (UNLIKELY((slot = get_task_slot(oid)) >= ztask_ht)) {
		/* not resizing now */
		return NULL;
	} else if (!task_ht[slot].oid) {
		/* not found */
		return NULL;
	}
	return task_ht[slot].t;
}

static void
free_task_ht(void)
{
	if (LIKELY(task_ht != NULL)) {
		free(task_ht);
		task_ht = NULL;
	}
	return;
}

static int
vtodoify(int ofd, _task_t t)
{
	static const char vcal_hdr[] = "\
BEGIN:VCALENDAR\n\
VERSION:2.0\n\
PRODID:-//GA Financial Solutions//echse//EN\n\
CALSCALE:GREGORIAN\n";
	static const char vcal_ftr[] = "\
END:VCALENDAR\n\
";
	static const char vtod_hdr[] = "\
BEGIN:VTODO\n";
	static const char vtod_ftr[] = "\
END:VTODO\n";
	int rc = 0;

	/* bang and print VCAL header */
	fdbang(ofd);
	if (UNLIKELY(fdwrite(vcal_hdr, strlenof(vcal_hdr)) < 0)) {
		rc--;
		goto out;
	}

	/* start off with VTODO's header */
	if (UNLIKELY(fdwrite(vtod_hdr, strlenof(vtod_hdr)) < 0)) {
		rc--;
		goto out;
	}


	rc -= fdprintf("UID:%s\n", obint_name(t->t->oid)) < 0;
	rc -= fdprintf("SUMMARY:%s\n", t->t->cmd) < 0;
	if (UNLIKELY(rc < 0)) {
		goto out;
	}

	with (ncred_t run_as = {.wd = t->t->run_as.wd, .sh = t->t->run_as.sh}) {
		/* we trust the injector to have put dflt_creds properly */
		run_as.u = t->dflt_cred.u;
		run_as.g = t->dflt_cred.g;

		if (run_as.wd == NULL) {
			run_as.wd = t->dflt_cred.wd;
		}
		if (run_as.sh == NULL) {
			run_as.sh = t->dflt_cred.sh;
		}

		rc -= fdprintf("X-ECHS-SETUID:%u\n", (uid_t)run_as.u) < 0;
		rc -= fdprintf("X-ECHS-SETGID:%u\n", (gid_t)run_as.g) < 0;
		rc -= fdprintf("X-ECHS-SHELL:%s\n", run_as.sh) < 0;
		rc -= fdprintf("LOCATION:%s\n", run_as.wd) < 0;
	}
	if (UNLIKELY(rc < 0)) {
		goto out;
	}

	with (echs_idiff_t d = t->dur) {
		const int s = d.d / 1000U + !!(d.d % 1000U);

		rc -= fdprintf("DURATION:%d\n", s) < 0;
	}
	with (unsigned int um = 0066U) {
		if (t->t->umsk < 0777U) {
			um = t->t->umsk;
		}
		rc -= fdprintf("X-ECHS-UMASK:0%o\n", um) < 0;
	}
	if (UNLIKELY(rc < 0)) {
		goto out;
	}

	rc -= fdprintf("X-ECHS-MAIL-RUN:%u\n", (unsigned int)t->t->mailrun) < 0;
	rc -= fdprintf("X-ECHS-MAIL-OUT:%u\n", (unsigned int)t->t->mailout) < 0;
	rc -= fdprintf("X-ECHS-MAIL-ERR:%u\n", (unsigned int)t->t->mailerr) < 0;
	if (t->t->in) {
		rc -= fdprintf("X-ECHS-IFILE:%s\n", t->t->in) < 0;
	}
	if (t->t->out) {
		rc -= fdprintf("X-ECHS-OFILE:%s\n", t->t->out) < 0;
	}
	if (t->t->err) {
		rc -= fdprintf("X-ECHS-EFILE:%s\n", t->t->err) < 0;
	}
	if (t->t->org) {
		rc -= fdprintf("ORGANIZER:%s\n", t->t->org) < 0;
	} else if (hnamez) {
		/* singleton, extend mailfrom by +HOSTNAME */
		const int hnamei = hnamez;
		rc -= fdprintf("ORGANIZER:echse+%.*s\n", hnamei, hname) < 0;
	} else {
		static const char eorg[] = "ORGANIZER:echse\n";
		rc -= fdwrite(eorg, strlenof(eorg)) < 0;
	}
	for (size_t j = 0U, natt = t->t->att ? t->t->att->nl : 0U;
	     j < natt; j++) {
		rc -= fdprintf("ATTENDEE:%s\n", t->t->att->l[j]) < 0;
	}
	if (UNLIKELY(rc < 0)) {
		goto out;
	}

	/* and finish with VTODO's footer followed by a nice flush */
	if (UNLIKELY(fdwrite(vtod_ftr, strlenof(vtod_ftr)) < 0)) {
		rc--;
		goto out;
	} else if (UNLIKELY(fdwrite(vcal_ftr, strlenof(vcal_ftr)) < 0)) {
		rc--;
		goto out;
	}
	rc -= fdflush() < 0;
out:
	return rc;
}

static pid_t
run_task(_task_t t)
{
/* assumes ev_loop_fork() has been called */
	static char *args[] = {
		"echsx",
		/* we want a vjournal log, defo defo */
		"-v",
		/* we maybe want to indicate a --no-run */
		NULL,
		NULL
	};
	char *const *env = deconst(t->t->env);
	/* echsx's stdin */
	int xin[2U];
	/* for the journal */
	posix_spawn_file_actions_t fa;
	char vjfn[PATH_MAX];
	int vjfd;
	off_t vjof;
	/* the actual process */
	pid_t r;

	/* set up pipe for echsx(1) */
	if (UNLIKELY(pipe(xin) < 0)) {
		ECHS_ERR_LOG("cannot set up pipe to echsx: %s", STRERR);
		return -1;
	}

	/* prepare the forking */
	if (UNLIKELY(posix_spawn_file_actions_init(&fa) < 0)) {
		/* shit, what are we gonna do?*/
		ECHS_ERR_LOG("cannot prepare forking to echsx: %s", STRERR);
		return -1;
	}

	if (!(t->nsim < (unsigned int)t->t->max_simul)) {
		args[2U] = "-nd";
	}

	/* prep the IPC with echsx */
	posix_spawn_file_actions_adddup2(&fa, xin[0U], STDIN_FILENO);
	posix_spawn_file_actions_addclose(&fa, xin[0U]);
	posix_spawn_file_actions_addclose(&fa, xin[1U]);

	/* get the journalling on the way */
	if (snprintf(vjfn, sizeof(vjfn),
			    "echsj_%u.ics", t->dflt_cred.u) < 0) {
		/* couldn't care less */
		vjfd = -1;
	} else if ((vjfd = openat(qdirfd, vjfn, O_RDWR | O_CREAT, 0600)) < 0) {
		/* brilliant */
		;
	} else if ((vjof = lseek(vjfd, 0, SEEK_END)) < 0) {
		/* make sure we don't shed a tear over this one */
		close(vjfd);
		vjfd = -1;
	} else {
		/* all's well in either case */
		posix_spawn_file_actions_adddup2(&fa, vjfd, STDOUT_FILENO);
		posix_spawn_file_actions_addclose(&fa, vjfd);
	}

	/* finally fork out our child */
	if (UNLIKELY(posix_spawn(&r, echsx, &fa, NULL, args, env) < 0)) {
		ECHS_ERR_LOG("cannot fork: %s", STRERR);
		r = -1;
	}
	/* free spawn resources */
	posix_spawn_file_actions_destroy(&fa);

	if (LIKELY(!(vjfd < 0))) {
		close(vjfd);
	}
	/* close read side of the pipe to echsx(1) */
	close(xin[0U]);

	/* and splice our VTODO onto xin[1U] */
	(void)vtodoify(xin[1U], t);
	/* we're finished aren't we? */
	close(xin[1U]);
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
	       instant_to_tstamp(e.from) < t) {
		(void)echs_evstrm_pop(x);
	}
	return e;
}


/* child pool */
#define ECHS_CHLD_POOL_INIZ	(256U)
static ev_child *free_chlds;
static size_t nfree_chlds;
static size_t zfree_chlds;

/* pool list */
struct clst_s {
	ev_child *_1st;
	size_t size;
};

static struct clst_s *cpools;
static size_t ncpools;
static size_t zcpools;

static ev_child*
make_chld_pool(size_t n)
{
/* generate a pile of ev_childs and chain them up */
	ev_child *res;

	if (UNLIKELY((res = malloc(sizeof(*res) * n)) == NULL)) {
		return NULL;
	}
	/* chain them up */
	res[n - 1U].data = NULL;
	for (size_t i = n - 1U; i > 0; i--) {
		res[i - 1U].data = res + i;
	}
	/* also add res to the list of pools (for freeing them later) */
	if (ncpools >= zcpools) {
		const size_t nuz = (zcpools * 2U) ?: 16U;
		void *nup = realloc(cpools, nuz * sizeof(*cpools));

		if (UNLIKELY(nup == NULL)) {
			/* I *KNEW* IT */
			free(res);
			return NULL;
		}
		/* surprise */
		cpools = nup;
		zcpools = nuz;
	}
	cpools[ncpools]._1st = res;
	cpools[ncpools].size = n;
	ncpools++;
	return res;
}

static void
free_chld_pools(void)
{
	if (cpools) {
		for (size_t i = 0U; i < ncpools; i++) {
			free(cpools[i]._1st);
		}
		free(cpools);
	}
	cpools = NULL;
	ncpools = zcpools = 0UL;
	return;
}

static ev_child*
make_chld(void)
{
/* create one child */
	ev_child *res;

	if (UNLIKELY(!nfree_chlds)) {
		/* put some more ev_child objects into the pool */
		const size_t adz = zfree_chlds ?: ECHS_CHLD_POOL_INIZ;

		free_chlds = make_chld_pool(adz);
		if (UNLIKELY(free_chlds == NULL)) {
			/* grrrr */
			return NULL;
		}
		nfree_chlds = adz;
		zfree_chlds = zfree_chlds ? adz * 2U : ECHS_CHLD_POOL_INIZ;
	}
	/* pop off the free list */
	res = free_chlds;
	free_chlds = free_chlds->data;
	nfree_chlds--;
	return res;
}

static void
free_chld(ev_child *c)
{
/* hand chld C over to free list */
	c->data = free_chlds;
	free_chlds = c;
	nfree_chlds++;
	return;
}


/* checkpoint handling */
typedef struct ndnd_s ndnd_t;

struct ndnd_s {
	NEDTRIE_ENTRY(ndnd_t) link;
	uid_t key;
	int fd;
};

NEDTRIE_HEAD(ndtr_t, ndnd_t);

/* this one's quite limited, just 16 slots wide, but it's static and
 * instead of introducing complexity by managing this array we just
 * say that if all checkpoint slots have been used we make a complete
 * dump of every single user. */
static ndtr_t chkpntr;
static ndnd_t chkpnts[16U];
static size_t ichkpnts;

static inline uid_t
ndnd_key(const ndnd_t *r)
{
	return r->key;
}

NEDTRIE_GENERATE(
	static, ndtr_t, ndnd_t, link, ndnd_key, NEDTRIE_NOBBLEZEROS(ndtr_t));

static inline void
seen_init(ndtr_t *restrict x)
{
	NEDTRIE_INIT(x);
	return;
}

static int
seenp(ndtr_t *tr, uid_t u)
{
	ndnd_t *tmp;

	if ((tmp = NEDTRIE_FIND(ndtr_t, tr, &(ndnd_t){.key = u})) == NULL) {
		return -1;
	}
	return tmp->fd;
}

static void
add_seen(ndtr_t *tr, ndnd_t *nd)
{
	NEDTRIE_INSERT(ndtr_t, tr, nd);
	return;
}

static inline bool
chkpntedp(uid_t u)
{
	if (NEDTRIE_FIND(ndtr_t, &chkpntr, &(ndnd_t){.key = u}) != NULL) {
		return true;
	}
	return false;
}

static void
add_chkpnt(uid_t u)
{
	if (LIKELY(ichkpnts < countof(chkpnts))) {
		const size_t i = ichkpnts++;
		chkpnts[i].key = u;
		NEDTRIE_INSERT(ndtr_t, &chkpntr, chkpnts + i);
	}
	return;
}

static int
chkpnt1(uid_t u)
{
	char fn[PATH_MAX];
	const int fl = O_WRONLY | O_CREAT | O_TRUNC;
	bool inittedp = false;
	int fd;

	if (UNLIKELY(snprintf(fn, sizeof(fn), ".echsq_%u.ics", u) < 0)) {
		goto err;
	} else if ((fd = openat(qdirfd, fn, fl, 0600)) < 0) {
		goto err;
	}

	for (size_t i = 0U; i < ztask_ht; i++) {
		if (!task_ht[i].oid) {
			continue;
		} else if (echs_task_owner(task_ht[i].t->t) != u) {
			continue;
		} else if (!inittedp) {
			echs_instruc_t ins = {
				INSVERB_SCHE, 0U,
				.t = task_ht[i].t->t,
			};
			echs_icalify_init(fd, ins);
			inittedp = true;
		}
		/* let evical module handle the printing */
		echs_task_icalify(fd, task_ht[i].t->t);
	}
	if (UNLIKELY(!inittedp)) {
		echs_icalify_init(fd, (echs_instruc_t){INSVERB_UNK});
	}
	echs_icalify_fini(fd);
	if (close(fd) < 0 || renameat(qdirfd, fn, qdirfd, fn + 1) < 0) {
		int x = errno;
		(void)unlinkat(qdirfd, fn, 0);
		errno = x;
		goto err;
	}
	ECHS_NOTI_LOG("checkpointed user %u", u);
	return 0;
err:
	ECHS_ERR_LOG("\
cannot checkpoint user %u's queue", u);
	return -1;
}

static int
chkpnta(void)
{
	const int fl = O_WRONLY | O_CREAT | O_TRUNC;
	char fn[PATH_MAX];
	/* seen tree and seen nodes */
	ndtr_t sntr;
	ndnd_t *snds;
	size_t nsnds = 0UL;
	size_t zsnds = countof(chkpnts);
	int rc = 0;

	if (UNLIKELY((snds = malloc(zsnds * sizeof(*snds))) == NULL)) {
		/* no use starting the whole procedure */
		return -1;
	}

	seen_init(&sntr);
	for (size_t i = 0U; i < ztask_ht; i++) {
		int fd;
		uid_t u;

		if (!task_ht[i].oid) {
			continue;
		} else if ((u = echs_task_owner(
				    task_ht[i].t->t)) == NOT_A_UID) {
			/* grml, no owner */
			continue;
		} else if ((fd = seenp(&sntr, u)) >= 0) {
			/* seen him */
			;
		} else if (fd < -1) {
			/* there's been a problem opening this user's file */
			continue;
		} else if (snprintf(fn, sizeof(fn), ".echsq_%u.ics", u) < 0 ||
			   (fd = openat(qdirfd, fn, fl, 0600)) < 0) {
			/* oh my god */
			fd = INT_MIN;
			goto bang;
		} else {
			echs_instruc_t ins = {
				INSVERB_SCHE, 0U,
				.t = task_ht[i].t->t,
			};

			echs_icalify_init(fd, ins);

		bang:
			/* boast about having seen this one */
			if (UNLIKELY(nsnds >= zsnds)) {
				/* resize this guy */
				const size_t nuz = zsnds * 2U;
				void *nup;

				nup = realloc(snds, nuz * sizeof(*snds));
				if (UNLIKELY(nup == NULL)) {
					/* finish up */
					rc = -1;
					break;
				}
				/* reassign */
				snds = nup;
				zsnds = nuz;
			}
			snds[nsnds] = (ndnd_t){.key = u, .fd = fd};
			add_seen(&sntr, snds + nsnds++);
		}

		/* let evical module handle the printing */
		echs_task_icalify(fd, task_ht[i].t->t);
	}
	for (size_t i = 0U; i < nsnds; i++) {
		const int fd = snds[i].fd;
		const uid_t u = snds[i].key;

		if (UNLIKELY(fd < -1)) {
			/* just do them one by one here
			 * and keep our fingers crossed that we
			 * closed enough file descriptors already */
			for (; i < nsnds; i++) {
				rc += chkpnt1(u);
			}
			break;
		}
		echs_icalify_fini(fd);
		if (snprintf(fn, sizeof(fn), ".echsq_%u.ics", u) < 0) {
			/* oh fuck, there's really nothing we can do */
			rc = -1;
			continue;
		}
		if (close(fd) < 0 || renameat(qdirfd, fn, qdirfd, fn + 1) < 0) {
			ECHS_ERR_LOG("\
cannot checkpoint user %u's queue", u);
			(void)unlinkat(qdirfd, fn, 0);
			rc = -1;
		} else {
			ECHS_NOTI_LOG("\
checkpointed user %u", u);
		}
	}
	free(snds);
	return rc;
}

static int
chkpnt(void)
{
	int rc = 0;

	ECHS_NOTI_LOG("checkpoint");
	if (ichkpnts >= countof(chkpnts)) {
		rc = chkpnta();
		goto fin;
	}
	/* otherwise just go through the list of checkpoint users */
	for (size_t i = 0U; i < ichkpnts; i++) {
		rc += chkpnt1(chkpnts[i].key);
	}
fin:
	/* all checkpoints cleared hopefully */
	ichkpnts = 0U;
	return rc;
}

static void
cptim_cb(EV_P_ ev_timer *UNUSED(w), int UNUSED(revents))
{
	chkpnt();
	return;
}


/* s2c communication */
typedef enum {
	ECHS_CMD_UNK,
	ECHS_CMD_HTTP,
	ECHS_CMD_ICAL,
} echs_cmd_t;

struct echs_cmd_http_s {
	/* routine */
	enum {
		ECHS_HTTP_UNK,
		ECHS_HTTP_QUEUE,
		ECHS_HTTP_SCHED,
	} rou;
	/* uid the user wants accessed */
	uid_t uid;
	const char *params;
	size_t paramz;
};

struct echs_cmdparam_s {
	echs_cmd_t cmd;
	union {
		struct echs_cmd_http_s http;
		ical_parser_t ical;
	};
};

static int _inject_task1(EV_P_ echs_task_t t, uid_t u);
static int _eject_task1(EV_P_ echs_toid_t o, uid_t u);

static echs_cmd_t
cmd_http_p(struct echs_cmdparam_s param[static 1U], const char *buf, size_t bsz)
{
	static const char ht_verb[] = "GET /";
	static const char ht_vers[] = " HTTP/1.1\r\n";
	static const char sched[] = "sched";
	static const char queue[] = "queue";
	const char *const eob = buf + bsz;
	const char *bp;
	const char *vp;

	if (UNLIKELY(param->cmd == ECHS_CMD_HTTP)) {
		return ECHS_CMD_HTTP;
	} else if ((bp = buf,
		    memcmp(bp, ht_verb, strlenof(ht_verb)))) {
		/* not a GET / request, make sure to exit quickly */
		return ECHS_CMD_UNK;
	} else if (bp += strlenof(ht_verb),
		   bp >= eob ||
		   (vp = xmemmem(bp, eob - bp,
				 ht_vers, strlenof(ht_vers))) == NULL) {
		/* wrong http version number, we're arseholes today */
		return ECHS_CMD_UNK;
	}
	/* now then, let's see if they want the per-user view on things */
	if (bp[0U] == 'u' && bp[1U] == '/') {
		/* yep */
		char *on = NULL;
		long unsigned int x;

		if (x = strtoul(bp += 2U, &on, 10), *on != '/') {
			/* they've fucked it */
			return ECHS_CMD_UNK;
		}
		/* otherwise proceed */
		param->http.uid = x;
		bp = ++on;
	} else {
		param->http.uid = -1;
	}
	if (!memcmp(bp, queue, strlenof(queue))) {
		param->http.rou = ECHS_HTTP_QUEUE;
		bp += strlenof(queue);
	} else if (!memcmp(bp, sched, strlenof(sched))) {
		param->http.rou = ECHS_HTTP_SCHED;
		bp += strlenof(sched);
	} else {
		param->http.rou = ECHS_HTTP_UNK;
	}
	if (*bp++ == '?') {
		param->http.params = bp;
		param->http.paramz = vp - bp;
	} else {
		param->http.params = NULL;
		param->http.paramz = 0U;
	}
	return ECHS_CMD_HTTP;
}

static echs_cmd_t
cmd_ical_p(struct echs_cmdparam_s param[static 1U], const char *buf, size_t bsz)
{
	if (echs_evical_push(&param->ical, buf, bsz) < 0) {
		/* pushing won't help */
		return ECHS_CMD_UNK;
	}
	/* aaaah ical stuff, just hand back control and let the pull parser
	 * do its job, he's more magnificent than us anyway */
	return ECHS_CMD_ICAL;
}


static void
echs_http_send_sched(_task_t t, const char *tuid, size_t tusz)
{
	char rng[64U];
	size_t rnz;

	with (echs_range_t r = {t->cur, echs_instant_add(t->cur, t->dur)}) {
		rnz = range_strf(rng, sizeof(rng), r);
	}
	fdwrite(tuid, tusz);
	fdputc('\t');
	fdwrite(rng, rnz);
	fdputc('\n');
	return;
}

static ssize_t
cmd_http(EV_P_ int ofd, const struct echs_cmd_http_s cmd[static 1U], ncred_t c)
{
	static const char rpl403[] = "\
HTTP/1.1 403 Forbidden\r\n\r\n";
	static const char rpl404[] = "\
HTTP/1.1 404 Not Found\r\n\r\n";
	static const char rpl500[] = "\
HTTP/1.1 500 Internal Server Error\r\n\r\n";
	static const char rpl200[] = "\
HTTP/1.1 200 Ok\r\n\r\n";
	const char *rpl;
	size_t rpz;
	ssize_t nwr = 0;
	int fd = -1;
	size_t fz = 0U;
	uid_t u;

	if (UNLIKELY((u = c.u & (unsigned)cmd->uid) != c.u)) {
		rpl = rpl403, rpz = strlenof(rpl403);
		goto hdr;
	}
	/* massage user */
	u = u ?: cmd->uid;

	switch (cmd->rou) {
		char fn[PATH_MAX];
		struct stat st;

	case ECHS_HTTP_QUEUE:
		if (cmd->params && cmd->paramz) {
			/* just go through stuff one by one, later on */
			rpl = rpl200, rpz = strlenof(rpl200);
		} else if (snprintf(fn, sizeof(fn), "echsq_%u.ics", u) < 0) {
			rpl = rpl500, rpz = strlenof(rpl500);
		} else if (chkpntedp(u) && chkpnt() < 0) {
			rpl = rpl500, rpz = strlenof(rpl500);
		} else if (fstatat(qdirfd, fn, &st, 0) < 0) {
			ECHS_NOTI_LOG("can't find echsq_%u.ics", u);
			rpl = rpl404, rpz = strlenof(rpl404);
		} else if ((fd = openat(qdirfd, fn, O_RDONLY)) < 0) {
			rpl = rpl403, rpz = strlenof(rpl403);
		} else {
			rpl = rpl200, rpz = strlenof(rpl200);
			fz = st.st_size;
		}
		break;
	case ECHS_HTTP_SCHED:
		rpl = rpl200, rpz = strlenof(rpl200);
		break;
	case ECHS_HTTP_UNK:
		rpl = rpl404, rpz = strlenof(rpl404);
		break;
	}

hdr:
	/* write reply header */
	nwr += write(ofd, rpl, rpz);

	/* and now content, if any */
	if (!(fd < 0)) {
#if defined HAVE_SENDFILE
		for (ssize_t nsf;
		     fz && (nsf = sendfile(ofd, fd, NULL, fz)) > 0;
		     fz -= nsf, nwr += nsf);
#endif	/* HAVE_SENDFILE */
		close(fd);
	}

	if (UNLIKELY(rpl != rpl200)) {
		/* just do nothing */
		;
	} else if (cmd->rou && cmd->params && cmd->paramz) {
		/* right, let's go through all tasks or the ones specified */
		static const char key[] = "tuid=";

		switch (cmd->rou) {
		case ECHS_HTTP_QUEUE:
			echs_icalify_init(ofd, (echs_instruc_t){INSVERB_SCHE});
			break;
		case ECHS_HTTP_SCHED:
			fdbang(ofd);
			break;
		default:
			break;
		}
		for (const char *pp = cmd->params,
			     *const ep = cmd->params + cmd->paramz, *np;
		     pp < ep; pp = np + 1U) {
			echs_oid_t oid = 0U;
			_task_t t = NULL;

			np = memchr(pp, '&', ep - pp) ?: ep;
			if (memcmp(pp, key, strlenof(key))) {
				continue;
			}
			/* otherwise go and look for him */
			pp += strlenof(key);

			if (!(oid = obint(pp, np - pp))) {
				/* nope */
				continue;
			} else if (UNLIKELY((t = get_task(oid)) == NULL)) {
				ECHS_ERR_LOG("\
cannot find task: no task with oid 0x%x found", oid);
				continue;
			} else if (UNLIKELY(!echs_task_owned_by_p(t->t, u))) {
				uid_t owner = echs_task_owner(t->t);
				ECHS_ERR_LOG("\
requesting task from user %d as user %d failed: permission denied", u, owner);
				continue;
			}

			switch (cmd->rou) {
			case ECHS_HTTP_QUEUE:
				echs_task_icalify(ofd, t->t);
				break;

			case ECHS_HTTP_SCHED:
				echs_http_send_sched(t, pp, np - pp);
				break;

			default:
				break;
			}
		}

		switch (cmd->rou) {
		case ECHS_HTTP_QUEUE:
			echs_icalify_fini(ofd);
			break;

		case ECHS_HTTP_SCHED:
			fdflush();
			break;

		default:
			break;
		}

	} else if (cmd->rou) {
		/* do something for all */
		switch (cmd->rou) {
		case ECHS_HTTP_QUEUE:
			/* fingers crossed the checkpointed file
			 * has been delivered via sendfile() above
			 * i.e. not doing nothing here */
			break;

		case ECHS_HTTP_SCHED:
			/* go through all the tasks */
			fdbang(ofd);
			for (size_t i = 0U; i < ztask_ht; i++) {
				const char *tu;
				size_t tz;

				if (!task_ht[i].oid) {
					continue;
				} else if (echs_task_owner(
						   task_ht[i].t->t) != u) {
					continue;
				}
				/* yep */
				tu = obint_name(task_ht[i].oid);
				tz = tu ? strlen(tu) : 0U;
				echs_http_send_sched(task_ht[i].t, tu, tz);
			}
			fdflush();
			break;

		default:
			break;
		}
	}
	return nwr;
}

static ssize_t
cmd_ical_rpl(int ofd, echs_instruc_t ins)
{
	static const char rpl_hdr[] = "\
BEGIN:VCALENDAR\n\
";
	static const char rpl_rpl[] = "\
METHOD:REPLY\n\
";
	static const char rpl_ftr[] = "\
END:VCALENDAR\n\
";
	static const char rpl_veh[] = "\
BEGIN:VEVENT\n\
";
	static const char rpl_vef[] = "\
END:VEVENT\n\
";
	static const char succ[] = "\
REQUEST-STATUS:2.0;Success\n\
";
	static const char fail[] = "\
REQUEST-STATUS:5.1;Service unavailable\n\
";
	static time_t now;
	static char stmp[32U];
	static size_t nrpl = 0U;
	ssize_t nwr = 0;

	fdbang(ofd);
	if (UNLIKELY(!ins.v)) {
#define cmd_ical_rpl_flush(x)	cmd_ical_rpl(x, (echs_instruc_t){INSVERB_UNK})
		if (nrpl) {
			nwr += fdwrite(rpl_ftr, strlenof(rpl_ftr));
			nrpl = 0U;
		}
		fdflush();
		return nwr;
	} else if (!nrpl) {
		/* we haven't sent the VCALENDAR thingie yet */
		nwr += fdwrite(rpl_hdr, strlenof(rpl_hdr));
		nwr += fdwrite(rpl_rpl, strlenof(rpl_rpl));
	}

	if_with (time_t tmp = time(NULL), tmp > now) {
		echs_instant_t nowi = epoch_to_echs_instant(tmp);

		dt_strf_ical(stmp, sizeof(stmp), nowi);
		now = tmp;
	}

	nwr += fdwrite(rpl_veh, strlenof(rpl_veh));

	nwr += fdprintf("UID:%s\n", obint_name(ins.o));
	nwr += fdprintf("DTSTAMP:%s\n", stmp);
	nwr += fdprintf("ATTENDEE:echse\n");
	switch (ins.v) {
	case INSVERB_SUCC:
		nwr += fdwrite(succ, strlenof(succ));
		break;
	case INSVERB_FAIL:
	default:
		nwr += fdwrite(fail, strlenof(fail));
		break;
	}
	nwr += fdwrite(rpl_vef, strlenof(rpl_vef));

	/* just for the next iteration */
	nrpl++;
	(void)fdputc;
	return nwr;
}

static ssize_t
cmd_ical(EV_P_ int ofd, ical_parser_t cmd[static 1U], ncred_t cred)
{
	ssize_t nwr = 0;
	bool need_dump_p = false;

	do {
		echs_instruc_t ins = echs_evical_pull(cmd);

		switch (ins.v) {
		case INSVERB_SCHE:
		case INSVERB_RESC:
			if (UNLIKELY(ins.t == NULL)) {
				continue;
			}
			/* and otherwise inject him */
			if (UNLIKELY(_inject_task1(EV_A_ ins.t, cred.u) < 0)) {
				/* reply with REQUEST-STATUS:x */
				ins.v = INSVERB_FAIL;
				break;
			}
			/* reply with REQUEST-STATUS:2.0;Success */
			ins.v = INSVERB_SUCC;
			break;

		case INSVERB_UNSC:
			/* cancel request
			 * cancel the whole shebang */
			if (UNLIKELY(_eject_task1(EV_A_ ins.o, cred.u) < 0)) {
				/* reply with REQUEST-STATUS:x */
				ins.v = INSVERB_FAIL;
				break;
			}
			/* reply with REQUEST-STATUS:2.0;Success */
			ins.v = INSVERB_SUCC;
			break;

		default:
			ECHS_NOTI_LOG("\
unknown instruction received from %d", ofd);
		case INSVERB_UNK:
			goto fini;
		}
		/* now serialise the actual reply */
		nwr += cmd_ical_rpl(ofd, ins);
		/* and keep track whether to checkpoint this user soon */
		if (ins.v == INSVERB_SUCC) {
			need_dump_p = true;
		}
	} while (1);
fini:
	/* this flushes all replies */
	cmd_ical_rpl_flush(ofd);
	/* keep a note about checkpointing */
	if (need_dump_p) {
		add_chkpnt(cred.u);
	}
	return nwr;
}

static echs_cmd_t
feed_cmd(struct echs_cmdparam_s param[static 1U], const char *buf, size_t bsz)
{
	echs_cmd_t r;

	switch (param->cmd) {
	case ECHS_CMD_UNK:
	case ECHS_CMD_HTTP:
		if ((r = cmd_http_p(param, buf, bsz))) {
			break;
		}
	case ECHS_CMD_ICAL:
		if ((r = cmd_ical_p(param, buf, bsz))) {
			break;
		}
	default:
		r = ECHS_CMD_UNK;
		break;
	}
	return param->cmd = r;
}

static void
shut_cmd(struct echs_cmdparam_s param[static 1U])
{
	switch (param->cmd) {
	default:
		break;

	case ECHS_CMD_ICAL:
		if (LIKELY(param->ical == NULL)) {
			break;
		}
		/* ical needs a special massage */
		echs_instruc_t ins;

		switch ((ins = echs_evical_last_pull(&param->ical)).v) {
		case INSVERB_SCHE:
			if (LIKELY(ins.t == NULL)) {
				break;
			}
			/* that can't be right, we should have got
			 * the last task in the loop above, this means
			 * this is a half-finished thing and we don't
			 * want no half-finished things */
			free_echs_task(ins.t);
			break;
		default:
			break;
		}
		break;
	}
	return;
}


/* libev conn handling */
#define MAX_CONNS	(sizeof(free_conns) * 8U)
#define MAX_QUEUE	MAX_CONNS
typedef char echs_iobuf_t[4096U];
static uint64_t free_conns = -1;
static struct echs_conn_s {
	ev_io r;
	/* i/o buffer, its size and an offset */
	char *buf;
	size_t bsz;
	off_t bix;

	/* socket credentials, established upon accepting() */
	ncred_t cred;

	/* the command we're building up
	 * this contains a partial or full parse of all parameters */
	struct echs_cmdparam_s cmd[1U];
} conns[MAX_CONNS];
static echs_iobuf_t bufs[MAX_CONNS];

static struct echs_conn_s*
make_conn(void)
{
	int i = ffs(free_conns & 0xffffffffU)
		?: ffs(free_conns >> 32U & 0xffffffffU);

	if (LIKELY(i-- > 0)) {
		/* toggle bit in free conns */
		free_conns ^= 1ULL << i;

		conns[i].buf = bufs[i];
		conns[i].bsz = sizeof(bufs[i]);
		conns[i].bix = 0;
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
unsched(EV_P_ ev_periodic *w, int UNUSED(revents))
{
	_task_t t = (void*)w;

	ECHS_NOTI_LOG("taking event off of schedule");
	add_chkpnt(echs_task_owner(t->t));
	ev_periodic_stop(EV_A_ w);
	free_task(t);
	return;
}

static void
chld_cb(EV_P_ ev_child *c, int UNUSED(revents))
{
	_task_t t = c->data;

	ECHS_NOTI_LOG("chld %d coughed: %d", c->rpid, c->rstatus);
	ev_child_stop(EV_A_ c);
	c->rpid = c->pid = 0;
	t->nsim--;

	if (UNLIKELY(t->w.reschedule_cb == NULL)) {
		/* we promised taskB_cb to kill this guy */
		unsched(EV_A_ &t->w, 0);
	}
	free_chld(c);
	return;
}

static void
task_cb(EV_P_ ev_periodic *w, int UNUSED(revents))
{
/* B tasks always run under supervision of our event loop, should the task
 * be scheduled again while max_simul other tasks are still running, cancel
 * the execution and reschedule for the next time. */
	_task_t t = (void*)w;

	/* the task context holds the number of currently running children
	 * as well as the maximum number of simultaneous children
	 * if the maximum is running, defer the execution of this task */
	if (t->nsim < (unsigned int)t->t->max_simul - 1U) {
		pid_t p;

		/* indicate that we might want to reuse the loop */
		ev_loop_fork(EV_A);

		if (LIKELY((p = run_task(t)) > 0)) {
			ev_child *c = make_chld();

			/* consider us running already */
			t->nsim++;
			c->data = t;

			/* keep track of the spawned child pid and register
			 * a watcher for status changes */
			ECHS_NOTI_LOG("supervising pid %d", p);
			ev_child_init(c, chld_cb, p, false);
			ev_child_start(EV_A_ c);
		}
	} else {
		/* ooooh, we can't run, call run task with the warning
		 * flag and use fire and forget */
		(void)run_task(t);
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
	echs_evstrm_t s = t->t->strm;
	echs_event_t e = unwind_till(s, now);
	ev_tstamp soon;
	char stmp[32];

	if (UNLIKELY(echs_event_0_p(e) && !t->nrun)) {
		/* this has never been run in the first place */
		ECHS_NOTI_LOG("event in the past, not scheduling");
		w->reschedule_cb = NULL;
		w->cb = unsched;
		t->cur = echs_nul_instant();
		return now;
	} else if (UNLIKELY(echs_event_0_p(e))) {
		/* we need to unschedule AFTER the next run */
		ECHS_NOTI_LOG("event completed, will not reschedule");
		w->reschedule_cb = NULL;
		t->cur = echs_nul_instant();
		return now + 1.e+30;
	}

	/* store the current event range and calculate tstamp for libev */
	t->cur = e.from;
	t->dur = e.dur;
	soon = instant_to_tstamp(e.from);
	t->nrun++;

	(void)dt_strf(stmp, sizeof(stmp), e.from);

	ECHS_NOTI_LOG("next run %f (%s)", soon, stmp);
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
	const int fd = w->fd;
	ssize_t nrd;

	/* just have a peek at what's there */
	if (UNLIKELY((nrd = recv(fd, c->buf, c->bsz, 0)) < 0)) {
		goto shut;
	}
	/* check the command we're supposed to obey */
	switch (feed_cmd(c->cmd, c->buf, nrd)) {
	case ECHS_CMD_HTTP:
		(void)cmd_http(EV_A_ fd, &c->cmd->http, c->cred);
		/* always shut him down */
		goto shut;

	case ECHS_CMD_ICAL:
		(void)cmd_ical(EV_A_ fd, &c->cmd->ical, c->cred);
		if (UNLIKELY(nrd == 0)) {
			goto shut;
		}
		break;

	default:
	case ECHS_CMD_UNK:
		/* just shut him down */
		ECHS_NOTI_LOG("unknown command");
		goto shut;
	}
	return;

shut:
	shut_cmd(c->cmd);
	ev_io_stop(EV_A_ w);
	ECHS_NOTI_LOG("freeing connection %d", w->fd);
	shut_conn((struct echs_conn_s*)w);
	return;
}

static void
sock_conn_cb(EV_P_ ev_io *w, int UNUSED(revents))
{
	struct echs_conn_s *c;
	struct sockaddr_un sa;
	socklen_t z = sizeof(sa);
	ncred_t cred;
	int s;

	if ((s = accept(w->fd, (struct sockaddr*)&sa, &z)) < 0) {
		ECHS_ERR_LOG("connection vanished: %s", STRERR);
		return;
	} else if (UNLIKELY(set_peereuid(s) < 0) ||
		   UNLIKELY(get_peereuid(&cred, s) < 0)) {
		ECHS_ERR_LOG("\
authenticity of connection %d cannot be established: %s", s, STRERR);
		close(s);
		return;
	}

	/* very good, get us an io watcher */
	ECHS_NOTI_LOG("connection %d from %u/%u", s, cred.u, cred.g);
	if (UNLIKELY((c = make_conn()) == NULL)) {
		ECHS_ERR_LOG("too many concurrent connections");
		close(s);
		return;
	}
	/* pass on our findings about the connection credentials */
	c->cred = cred;

	ev_io_init(&c->r, sock_data_cb, s, EV_READ);
	ev_io_start(EV_A_ &c->r);
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
	nul:
		return NULL;
	} else if (EV_A == NULL) {
	fre:
		free(res);
		goto nul;
	} else if (ini_task_ht() < 0) {
		goto fre;
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

	/* just do minutely checkpointing */
	ev_timer_init(&res->cptim, cptim_cb, 60.0, 60.0);
	ev_timer_start(EV_A_ &res->cptim);

	res->loop = EV_A;
	return res;
}

static void
free_echsd(struct _echsd_s *ctx)
{
	/* final checkpointing */
	chkpnt();

	if (UNLIKELY(ctx == NULL)) {
		return;
	}
	if (LIKELY(ctx->loop != NULL)) {
		ev_break(ctx->loop, EVBREAK_ALL);
		ev_loop_destroy(ctx->loop);
	}
	free_task_pools();
	free_chld_pools();
	free_task_ht();
	free(ctx);
	return;
}

static int
_inject_task1(EV_P_ echs_task_t t, uid_t u)
{
	_task_t res;
	ncred_t uc;
	ncred_t oc;

	/* massage owner and u
	 * we allow U to be set to NOT_A_UID in which case the
	 * uid will be taken from the compl'd T->owner slot */
	oc = compl_owner(t->owner);
	uc = compl_uid(u);

	/* big checking */
	if (uc.u == NOT_A_UID && oc.u == NOT_A_UID) {
		/* can't have both unset, bugger off */
		ECHS_ERR_LOG("\
ignoring task update with no user nor owner specified");
		return -1;
	} else if (uc.u == NOT_A_UID && meself.uid && oc.u != meself.uid) {
		ECHS_ERR_LOG("\
need root privileges to run task as user %d", oc.u);
			return -1;
	} else if (oc.u == NOT_A_UID && meself.uid && uc.u != meself.uid) {
		ECHS_ERR_LOG("\
need root privileges to run task as user %d", uc.u);
		return -1;
	} else if (uc.u != NOT_A_UID && oc.u != NOT_A_UID && oc.u != uc.u) {
		/* we've caught him, call the police!!! */
		ECHS_ERR_LOG("\
task update from user %d for task from user %d failed: permission denied",
			     uc.u, oc.u);
		return -1;
	} else if (oc.u == NOT_A_UID && (oc.u = uc.u, false)) {
		/* not reached */

	} else if (uc.u == NOT_A_UID && (uc.u = oc.u, false)) {
		/* not reached */

	} else if (UNLIKELY(t->strm == NULL)) {
		ECHS_ERR_LOG("submitted ical object is not a task");
		return -1;
	} else if ((res = get_task(t->oid)) != NULL &&
		   !echs_task_owned_by_p(res->t, oc.u)) {
		/* we've caught him, call the police!!! */
		ECHS_ERR_LOG("\
task update from user %d for task from user %d failed: permission denied",
			     uc.u, echs_task_owner(res->t));
		return -1;
	} else if (res != NULL) {
		ECHS_NOTI_LOG("task update, unscheduling old task");
		ev_periodic_stop(EV_A_ &res->w);
		free(deconst(res->dflt_cred.wd));
		free(deconst(res->dflt_cred.sh));
		free_echs_task(res->t);
	} else if (UNLIKELY((res = make_task(t->oid)) == NULL)) {
		ECHS_ERR_LOG("cannot submit new task");
		return -1;
	}
	/* refresh the actual credentials again */
	if (UNLIKELY((uc = compl_uid(uc.u)).u == NOT_A_UID)) {
		ECHS_ERR_LOG("user %u has vanished", oc.u);
		return -1;
	}
	/* massage away the owner in the task and
	 * replace by the connection credentials */
	echs_task_rset_ownr(t, uc.u);
	/* bang libechse task into our _task */
	res->t = t;
	/* run all tasks as U and the default group of U */
	res->dflt_cred.u = uc.u;
	res->dflt_cred.g = uc.g;
	/* also, by default, in U's home dir using U's shell */
	res->dflt_cred.wd = strdup(uc.wd);
	res->dflt_cred.sh = strdup(uc.sh);

	ECHS_NOTI_LOG("scheduling task for user %u(%u)", uc.u, uc.g);
	ev_periodic_init(&res->w, task_cb, 0./*ignored*/, 0., resched);
	ev_periodic_start(EV_A_ &res->w);
	return 0;
}

static int
_eject_task1(EV_P_ echs_toid_t oid, uid_t uid)
{
	_task_t res;

	if (UNLIKELY((res = get_task(oid)) == NULL)) {
		ECHS_ERR_LOG("\
cannot update task: no task with oid 0x%x found", oid);
		return -1;
	} else if (UNLIKELY(!echs_task_owned_by_p(res->t, uid))) {
		/* we've caught him, call the police!!! */
		ECHS_ERR_LOG("\
task update from user %d for task from user %d failed: permission denied",
			     uid, echs_task_owner(res->t));
		return -1;
	}
	/* otherwise proceed with the evacuation */
	ECHS_NOTI_LOG("cancelling task 0x%x", oid);
	ev_periodic_stop(EV_A_ &res->w);
	free_task(res);
	return 0;
}


static void
_inject_file(struct _echsd_s *ctx, const char *fn)
{
	char buf[65536U];
	ical_parser_t pp = NULL;
	ssize_t nrd;
	int fd;

	if ((fd = openat(qdirfd, fn, O_RDONLY)) < 0) {
		return;
	}

more:
	switch ((nrd = read(fd, buf, sizeof(buf)))) {
		echs_instruc_t ins;

	default:
		if (echs_evical_push(&pp, buf, nrd) < 0) {
			/* pushing more brings nothing */
			break;
		}
		/*@fallthrough@*/
	case 0:
		do {
			ins = echs_evical_pull(&pp);

			if (UNLIKELY(ins.v != INSVERB_SCHE)) {
				break;
			} else if (UNLIKELY(ins.t == NULL)) {
				continue;
			} else if (UNLIKELY(!ins.t->oid)) {
				/* not even an OID */
				free_echs_task(ins.t);
				continue;
			}
			/* and otherwise inject him */
			_inject_task1(ctx->loop, ins.t, NOT_A_UID);
		} while (1);
		if (LIKELY(nrd > 0)) {
			goto more;
		}
		/*@fallthrough@*/
	case -1:
		/* last ever pull this morning */
		ins = echs_evical_last_pull(&pp);

		if (UNLIKELY(ins.v != INSVERB_SCHE)) {
			break;
		} else if (UNLIKELY(ins.t != NULL)) {
			/* that can't be right, we should have got
			 * the last task in the loop above, this means
			 * this is a half-finished thing and we don't
			 * want no half-finished things */
			free_echs_task(ins.t);
		}
		break;
	}

	close(fd);
	return;
}

static void
echsd_inject_queues(struct _echsd_s *ctx, const char *qd)
{
	/* we are a super-echsd, load all .ics files we can find */
	if_with (DIR *d, d = opendir(qd)) {
		static const char prfx[] = "echsq_";
		static const char sufx[] = ".ics";

		for (struct dirent *dp; (dp = readdir(d)) != NULL;) {
			const char *const fn = dp->d_name;
			const size_t fz = strlen(fn);

			/* check if it's the right prefix */
			if (strncmp(fn, prfx, strlenof(prfx))) {
				/* not our thing */
				continue;
			}
			/* check suffix
			 * no length check needed as prefix beats
			 * suffix in terms of string-length */
			if (strncmp(fn + fz - strlenof(sufx),
				    sufx, strlenof(sufx))) {
				/* nope */
				continue;
			}
			/* otherwise, try and load it */
			_inject_file(ctx, fn);
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
	int esok = -1;
	int rc = 0;

	/* best not to be signalled for a minute */
	block_sigs();

	if (yuck_parse(argi, argc, argv) < 0) {
		rc = 1;
		goto out;
	}

	/* who are we? */
	meself.uid = geteuid();
	meself.gid = getegid();

	/* populate with hostname we're running on */
	if (UNLIKELY(gethostname(hname, sizeof(hname)) < 0)) {
		perror("Error: cannot get hostname");
		rc = 1;
		goto out;
	} else {
		hnamez = strlen(hname);
	}

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
	} else if (UNLIKELY((qdirfd = open(qdir, O_RDONLY)) < 0)) {
		perror("Error: cannot open echsd spool directory");
		rc = 1;
		goto out;
	} else if (UNLIKELY(fd_cloexec(qdirfd) < 0)) {
		perror("Error: cannot set FD_CLOEXEC on spool directory");
		rc = 1;
		goto out;
	} else if (UNLIKELY((esok = make_socket()) < 0)) {
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

	/* we need to get to know ourself  */
	if ((meself.pid = getpid()) < (pid_t)0) {
		ECHS_ERR_LOG("cannot obtain current pid %d", meself.pid);
		rc = 2;
		goto clo;
	}

	/* obtain our context */
	if (UNLIKELY((ctx = make_echsd()) == NULL)) {
		ECHS_ERR_LOG("cannot instantiate echsd context");
		goto clo;
	}

	/* write pid file if requested */
	if (argi->pidfile_arg) {
		const int fl = O_CREAT | O_TRUNC | O_WRONLY;
		const char *pfn = argi->pidfile_arg;
		int pfd;

		if (UNLIKELY((pfd = open(pfn, fl, 0666)) < 0)) {
			/* right, so this fails and we quit? */
			ECHS_ERR_LOG("cannot write pid file `%s'", pfn);
			rc = 1;
			goto fre;
		}
		/* otherwise */
		dprintf(pfd, "%d\n", meself.pid);
		close(pfd);
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

fre:
	/* free context and associated resources */
	free_echsd(ctx);
	/* remove pidfile */
	if (argi->pidfile_arg) {
		(void)unlink(argi->pidfile_arg);
	}

clo:
	/* stop them log files */
	echs_closelog();

out:
	if (esok >= 0) {
		(void)free_socket(esok);
	}
	if (qdirfd >= 0) {
		close(qdirfd);
	}
	yuck_free(argi);
	return rc;
}

/* echsd.c ends here */
