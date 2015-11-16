/*** echsq.c -- echse queue manager
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
#if defined HAVE_VERSION_H
# include "version.h"
#endif	/* HAVE_VERSION_H */
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <poll.h>
#include <assert.h>
#if defined HAVE_SENDFILE
# include <sys/sendfile.h>
#endif	/* HAVE_SENDFILE */
#if defined HAVE_PATHS_H
# include <paths.h>
#endif	/* HAVE_PATHS_H */
#include <pwd.h>
#include <spawn.h>
#include "nifty.h"
#include "sock.h"
#include "evical.h"
#include "fdprnt.h"
#include "intern.h"

extern char **environ;

#if !defined _PATH_TMP
# define _PATH_TMP	"/tmp/"
#endif	/* _PATH_TMP */

static const char vcal_hdr[] = "\
BEGIN:VCALENDAR\n\
VERSION:2.0\n\
PRODID:-//GA Financial Solutions//echse//EN\n\
METHOD:PUBLISH\n\
CALSCALE:GREGORIAN\n";
static const char vcal_ftr[] = "\
END:VCALENDAR\n";


static __attribute__((format(printf, 1, 2))) void
serror(const char *fmt, ...)
{
	va_list vap;
	va_start(vap, fmt);
	vfprintf(stderr, fmt, vap);
	va_end(vap);
	if (errno) {
		fputc(':', stderr);
		fputc(' ', stderr);
		fputs(strerror(errno), stderr);
	}
	fputc('\n', stderr);
	return;
}

static inline size_t
xstrlncpy(char *restrict dst, size_t dsz, const char *src, size_t ssz)
{
	if (UNLIKELY(dsz == 0U)) {
		return 0U;
	} else if (UNLIKELY(ssz > dsz)) {
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

static size_t
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
	if (UNLIKELY(ndlz == 0UL)) {
		return 0U;
	} else if ((hay = memchr(hay, *ndl, hayz)) == NULL) {
		/* trivial */
		return (size_t)-1;
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
		return (size_t)-1;
	} else if (eqp) {
		/* found a match */
		return hay - (eoh - hayz);
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
			return cand - (eoh - hayz);
		}
	}
	return (size_t)-1;
}


static int
get_ssock(int s, bool anonp)
{
	static const char sockfn[] = "\0/var/run/echse/=echsd";
	struct sockaddr_un sa = {AF_UNIX};
	struct stat st;
	socklen_t sz;

	if (UNLIKELY(sizeof(sa.sun_path) < sizeof(sockfn))) {
		/* not gonna happen */
		return -1;
#if !defined __linux__
	} else if (anonp) {
		/* don't work on non-linuxen */
		return -1;
#endif	/* !__linux__ */
	}

	memcpy(sa.sun_path, sockfn + !anonp, sizeof(sockfn) - !anonp);
	sz = strlenof(sockfn) - !anonp + sizeof(sa.sun_family);

	if (!anonp && (stat(sa.sun_path, &st) < 0 || !S_ISSOCK(st.st_mode))) {
		/* indicate failure */
		return -1;
	} else if (UNLIKELY(connect(s, (struct sockaddr*)&sa, sz) < 0)) {
		return -1;
	}
	/* otherwise it's been a success */
	return 0;
}

static int
get_usock(int s, bool anonp)
{
	static char hname[HOST_NAME_MAX];
	static size_t hnamez;
#if defined __linux__
	static const char asokfn[] = "\0/var/run/echse/=echsd";
#endif	/* __linux__ */
	static const char appdir[] = ".echse/";
	static const char sockfn[] = "=echsd";
	struct sockaddr_un sa = {AF_UNIX};
	uid_t u = getuid();
	struct stat st;
	socklen_t sz;

	if (anonp) {
#if defined __linux__
		int rc;

		if (UNLIKELY(sizeof(sa.sun_path) < sizeof(asokfn) + 6U)) {
			/* not enough room is there */
			return -1;
		}
		memcpy(sa.sun_path, asokfn, sz = strlenof(asokfn));
		rc = snprintf(
			sa.sun_path + sz, sizeof(sa.sun_path) - sz, "-%u", u);
		if (UNLIKELY(rc <= 0)) {
			return -1;
		}
		/* otherwise it's looking pretty good */
		sz += rc;
		goto conn;
#else  /* !__linux__ */
		/* don't work on non-linuxen */
		return -1;
#endif	/* __linux__ */
	}

	/* populate with hostname we're running on */
	if (UNLIKELY(!hnamez && gethostname(hname, sizeof(hname)) < 0)) {
		perror("Error: cannot get hostname");
		return -1;
	} else if (!hnamez) {
		hnamez = strlen(hname);
	}

	with (struct passwd *pw) {
		if ((pw = getpwuid(u)) == NULL || pw->pw_dir == NULL) {
			/* there's nothing else we can do */
			return -1;
		}
		sz = xstrlcpy(sa.sun_path, pw->pw_dir, sizeof(sa.sun_path));
		if (LIKELY(sz < sizeof(sa.sun_path))) {
			sa.sun_path[sz++] = '/';
		}
		/* append appdir */
		sz += xstrlncpy(
			sa.sun_path + sz, sizeof(sa.sun_path) - sz,
			appdir, strlenof(appdir));
		/* now the machine name */
		sz += xstrlncpy(
			sa.sun_path + sz, sizeof(sa.sun_path) - sz,
			hname, hnamez);
		if (LIKELY(sz < sizeof(sa.sun_path))) {
			sa.sun_path[sz++] = '/';
		}
		/* and the =echsd bit */
		sz += xstrlncpy(
			sa.sun_path + sz, sizeof(sa.sun_path) - sz,
			sockfn, strlenof(sockfn));
	}

conn:
	/* wind SZ up just a little more */
	sz += sizeof(sa.sun_family);

	/* check for physical presence of the socket file */
	if (!anonp && (stat(sa.sun_path, &st) < 0 || !S_ISSOCK(st.st_mode))) {
		/* indicate failure */
		return -1;
	} else if (UNLIKELY(connect(s, (struct sockaddr*)&sa, sz) < 0)) {
		return -1;
	}
	return 0;
}

static int
get_esock(bool systemp)
{
/* return a candidate for the echsd socket, system-wide or user local */
	int s;

	if (UNLIKELY((s = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)) {
		return s;
	}

	if (systemp) {
		/* try anonymous then filesystem socket */
		if (get_ssock(s, true) < 0 && get_ssock(s, false) < 0) {
			goto fail;
		}
	} else {
		/* again, first anonymously then filesystem sock */
		if (get_usock(s, true) < 0 && get_usock(s, false) < 0) {
			goto fail;
		}
	}
	/* make sure we don't molest our children with this socket  */
	(void)fd_cloexec(s);
	return s;

fail:
	close(s);
	return -1;
}

static int
free_conn(int s)
{
	return close(s);
}


/* counter for outstanding requests */
static size_t nout;

static int massage(echs_task_t t);

static int
poll1(int fd, int timeo)
{
	static ical_parser_t rp;
	struct pollfd rfd[] = {{.fd = fd, .events = POLLIN}};
	echs_instruc_t ins;
	char buf[4096U];
	ssize_t nrd;

	if (fd < 0) {
		/* secret message to clear up the reader */
		goto clear;
	}
	/* just read them replies */
	if (poll(rfd, countof(rfd), timeo) <= 0) {
		return -1;
	}

	/* it MUST be our fd */
	assert(rfd[0U].revents);

	switch ((nrd = read(fd, buf, sizeof(buf)))) {
	default:
		if (echs_evical_push(&rp, buf, nrd) < 0) {
			/* pushing more is insane */
			break;
		}
		/*@fallthrough@*/
	case 0:
	drain:
		ins = echs_evical_pull(&rp);
		switch (ins.v) {
		case INSVERB_RESC:
			/* that means success */
			nout--;
			puts("SUCCESS");
			/* I'm sure there's more */
			goto drain;
		case INSVERB_UNSC:
			nout--;
			puts("FAILURE");
			/* there might be more */
			goto drain;
		default:
			break;
		}
		break;
	case -1:
	clear:
		ins = echs_evical_last_pull(&rp);
		switch (ins.v) {
		case INSVERB_RESC:
			/* that means success */
			nout--;
			puts("SUCCESS");
			break;
		case INSVERB_UNSC:
			nout--;
			puts("FAILURE");
			break;
		default:
			break;
		}
		break;
	}
	return 0;
}

static void
add_fd(int tgt_fd, int src_fd)
{
	char buf[32768U];
	ical_parser_t pp = NULL;
	size_t nrd;

more:
	switch ((nrd = read(src_fd, buf, sizeof(buf)))) {
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
			} else if (UNLIKELY(massage(ins.t) < 0)) {
				continue;
			}
			/* and otherwise inject him */
			echs_task_icalify(tgt_fd, ins.t);
			nout++;

			poll1(tgt_fd, 0);
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

		poll1(tgt_fd, 0);
		break;
	}
	return;
}

static const char*
get_editor(void)
{
#if !defined DEFAULT_EDITOR
# define DEFAULT_EDITOR	"vi"
#endif	/* !DEFAULT_EDITOR */
	const char *editor;
	const char *term;

	/* check for term */
	if (UNLIKELY((term = getenv("TERM")) == NULL)) {
		/* ah well, less work to do */
		;
	} else if (!strcmp(term, "dumb")) {
		/* I see */
		term = NULL;
	}
	/* check for editor */
	if (term && (editor = getenv("VISUAL"))) {
		/* brilliant */
		;
	} else if ((editor = getenv("EDITOR"))) {
		/* very well then */
		;
	} else if (term) {
		/* can always use the default editor */
		editor = DEFAULT_EDITOR;
	}
	return editor;
}

static int
run_editor(const char *fn)
{
/* run $EDITOR on FN. */
	static const char sh[] = "/bin/sh";
	static char *args[] = {"sh", "-c", NULL, NULL};
	const char *editor;
	int rc = 0;
	pid_t p;

	if (UNLIKELY((editor = get_editor()) == NULL)) {
		errno = 0, serror("Error: cannot find EDITOR to use");
		return -1;
	} else if (editor[0U] == ':' && (uint8_t)editor[1U] <= ' ') {
		/* aaah, they want us to fucking hurry up ... aye aye */
		return 0;
	}
	/* start EDITOR through sh */
	with (size_t elen = strlen(editor), flen = strlen(fn)) {
		/* we need an extra space and 2 quote characters */
		const size_t z =
			elen + 1U/*SPC*/ + 2U/*quotes*/ + flen + 1U/*nul*/;

		if (UNLIKELY((args[2U] = malloc(z)) == NULL)) {
			serror("Error: cannot start EDITOR");
			return -1;
		}
		memcpy(args[2U], editor, elen);
		args[2U][elen + 0U] = ' ';
		args[2U][elen + 1U] = '\'';
		memcpy(args[2U] + elen + 2U, fn, flen);
		args[2U][elen + 2U + flen + 0U] = '\'';
		args[2U][elen + 2U + flen + 1U] = '\0';
	}

	/* call /bin/sh with the above proto-task as here-document
	 * and stdout redir'd to FD */
	if (UNLIKELY(posix_spawn(&p, sh, NULL, NULL, args, environ) < 0)) {
		serror("Error: cannot run /bin/sh");
		rc = -1;
	} else {
		int st;
		while (waitpid(p, &st, 0) != p);
		if (!WIFEXITED(st) || WEXITSTATUS(st) != EXIT_SUCCESS) {
			rc = -1;
		}
	}
	free(args[2U]);
	return rc;
}

static int
add_tmpl(const char *fn)
{
	static const char builtin_proto[] = "\
BEGIN:VCALENDAR\n\
VERSION:2.0\n\
PRODID:-//GA Financial Solutions//echse//EN\n\
METHOD:PUBLISH\n\
CALSCALE:GREGORIAN\n\
BEGIN:VEVENT\n\
SUMMARY:\n\
ATTENDEE:$USER\n\
X-ECHS-MAIL-OUT:true\n\
X-ECHS-MAIL-ERR:true\n\
LOCATION:$PWD\n\
DTSTART:\n\
DURATION:P1D\n\
RRULE:FREQ=DAILY\n\
END:VEVENT\n\
END:VCALENDAR\n";
	static char tmpfn[] = "/tmp/taskXXXXXXXX";
	static const char sh[] = "/bin/sh";
	static char *args[] = {
		"sh", "-s", NULL
	};
	posix_spawn_file_actions_t fa;
	size_t fz;
	int pd[2U] = {-1, -1};
	int rc = 0;
	pid_t p;
	int ifd;
	int tmpfd;

	/* check input file */
	if (UNLIKELY(fn == NULL)) {
		fz = 0U;
	} else {
		struct stat st;

		if (stat(fn, &st) < 0) {
			return -1;
		} else if (UNLIKELY((ifd = open(fn, O_RDONLY)) < 0)) {
			return -1;
		}
		/* otherwise keep track of size */
		fz = st.st_size;
	}

	/* get ourselves a temporary file */
	if ((tmpfd = mkstemp(tmpfn)) < 0) {
		serror("Error: cannot create temporary file `%s'", tmpfn);
		goto clo;
	} else if (pipe(pd) < 0) {
		serror("Error: cannot set up pipe to shell");
		goto clo;
	} else if (posix_spawn_file_actions_init(&fa) < 0) {
		serror("Error: cannot initialise file actions");
		goto clo;
	}
	/* fiddle with the child's descriptors */
	rc += posix_spawn_file_actions_adddup2(&fa, pd[0U], STDIN_FILENO);
	rc += posix_spawn_file_actions_adddup2(&fa, tmpfd, STDOUT_FILENO);
	rc += posix_spawn_file_actions_adddup2(&fa, tmpfd, STDERR_FILENO);
	rc += posix_spawn_file_actions_addclose(&fa, tmpfd);
	rc += posix_spawn_file_actions_addclose(&fa, pd[0U]);
	rc += posix_spawn_file_actions_addclose(&fa, pd[1U]);

	/* call /bin/sh with the above proto-task as here-document
	 * and stdout redir'd to FD */
	if (UNLIKELY(posix_spawn(&p, sh, &fa, NULL, args, environ) < 0)) {
		serror("Error: cannot run /bin/sh");
		posix_spawn_file_actions_destroy(&fa);
		goto clo;
	}
	/* also get rid of the file actions resources */
	posix_spawn_file_actions_destroy(&fa);
	/* close read-end of pipe */
	close(pd[0U]);

	/* feed the shell */
	write(pd[1U], "cat <<EOF\n", sizeof("cat <<EOF\n"));
	if (!(ifd < 0)) {
#if defined HAVE_SENDFILE
		for (ssize_t nsf;
		     fz && (nsf = sendfile(pd[1U], ifd, NULL, fz)) > 0;
		     fz -= nsf);
#else  /* !HAVE_SENDFILE */
		char buf[16U * 4096U];

		for (ssize_t nrd; (nrd = read(ifd, buf, sizeof(buf))) > 0;) {
			for (ssize_t nwr;
			     nrd > 0 && (nwr = write(pd[1U], buf, nrd)) > 0;
			     nrd -= nwr);
		}
		(void)fz;
#endif	/* HAVE_SENDFILE */
	} else {
		/* just write the built-in proto */
		const char *const bp = builtin_proto;
		const size_t bz = strlenof(builtin_proto);
		size_t tot = 0U;

		for (ssize_t nwr;
		     tot < bz && (nwr = write(pd[1U], bp + tot, bz - tot)) > 0;
		     tot += nwr);
	}
	/* feed more to shell */
	write(pd[1U], "EOF\n", sizeof("EOF\n"));
	close(pd[1U]);

	/* let's hang around and have a beer */
	with (int st) {
		while (waitpid(p, &st, 0) != p);
		if (!WIFEXITED(st) || WEXITSTATUS(st) != EXIT_SUCCESS) {
			goto clo;
		}
	}
	/* now set the CLOEXEC bit so the editor isn't terribly confused */
	(void)fd_cloexec(tmpfd);
	/* launch the editor so the user can peruse the proto-task */
	if (run_editor(tmpfn) < 0) {
		goto clo;
	}
	/* don't keep this file, we talk descriptors */
	unlink(tmpfn);
	/* rewind it though */
	lseek(tmpfd, 0, SEEK_SET);
	return tmpfd;
clo:
	unlink(tmpfn);
	close(pd[0U]);
	close(pd[1U]);
	close(tmpfd);
	return -1;
}

static int
massage(echs_task_t t)
{
	struct echs_task_s *_t = deconst(t);

	if (_t->run_as.wd == NULL) {
		/* fill in pwd */
		size_t z = (size_t)pathconf(".", _PC_PATH_MAX);
		char *p;

		if (LIKELY((p = malloc(z)) != NULL)) {
			_t->run_as.wd = getcwd(p, z);
		}
	}
	if (_t->run_as.sh == NULL) {
		/* use /bin/sh */
		_t->run_as.sh = strdup("/bin/sh");
	}
	if (_t->umsk > 0777) {
		/* use current umask */
		mode_t cur = umask(0777);
		(void)umask(cur);
		_t->umsk = cur;
	}
	return 0;
}

static size_t
get_dirs(char *restrict buf, size_t bsz, char **eoh, char **eoe)
{
/* return /home/USER/.echse/MACHINE and set pointer EOH to .echse
 * and EOE to MACHINE.
 * Return the size of the whole path in bytes. */
	static char dotechs[] = ".echse";
	uid_t u = getuid();
	struct passwd *pw;
	size_t bi;

	if (UNLIKELY((pw = getpwuid(u)) == NULL || pw->pw_dir == NULL)) {
		/* there's nothing else we can do */
		return 0U;
	}
	if (UNLIKELY((bi = xstrlcpy(buf, pw->pw_dir, bsz)) + 8U >= bsz)) {
		/* not even space for two /'s? */
		return 0U;
	}
	buf[bi++] = '/';
	if (UNLIKELY(bi >= bsz)) {
		goto out;
	}
	*eoh = buf + bi;
	bi += xstrlncpy(buf + bi, bsz - bi, dotechs, strlenof(dotechs));
	buf[bi++] = '/';
	if (UNLIKELY(bi >= bsz)) {
		goto out;
	}
	*eoe = buf + bi;
	if (UNLIKELY(gethostname(buf + bi, bsz - bi) < 0)) {
		return 0U;
	}
	bi += strlen(*eoe);
	if (UNLIKELY(bi >= bsz)) {
		goto out;
	}
	buf[bi++] = '/';
	return bi;

out:
	*eoh = NULL;
	*eoe = NULL;
	return 0U;
}

static int
use_tmpl(void)
{
	/* look for templates in queuedir first, then ~/.echse
	 * then fall back to the built-in template */
	static const char proto_fn[] = "proto";
	char fn[PATH_MAX];
	char *h, *m;
	size_t fi;
	int fd;

	if (!(fi = get_dirs(fn, sizeof(fn), &h, &m))) {
		goto builtin;
	}
	/* massage PROTO behind all of FN */
	xstrlncpy(fn + fi, sizeof(fn) - fi, proto_fn, strlenof(proto_fn));
	if ((fd = add_tmpl(fn)) >= 0) {
		return fd;
	}
	/* massage PROTO behind .echse portion of FN */
	xstrlncpy(m, sizeof(fn) - (m - fn), proto_fn, strlenof(proto_fn));
	if ((fd = add_tmpl(fn)) >= 0) {
		return fd;
	}
builtin:
	/* fallback to built-in */
	return add_tmpl(NULL);
}

static ssize_t
http_hdr_len(char *restrict buf, size_t bsz)
{
	static const char http_sep[] = "\r\n\r\n";
	static const char http_hdr[] = "HTTP/";
	size_t res;

	if (UNLIKELY(bsz <= strlenof(http_hdr))) {
		/* nope */
		return -1;
	} else if (memcmp(buf, http_hdr, strlenof(http_hdr))) {
		/* not really */
		return -1;
	}

	/* find header separator */
	res = xmemmem(buf, bsz, http_sep, strlenof(http_sep));
	if (UNLIKELY(res >= bsz)) {
		/* bad luck aye */
		return -1;
	}
	/* \nul-ify the region BI points to */
	memset(buf + res, 0, strlenof(http_sep));
	return res + strlenof(http_sep);
}

static int
http_ret_cod(const char *buf, char *cod[static 1U], size_t bsz)
{
	long unsigned int rc;
	const char *spc;

	(void)bsz;
	if (UNLIKELY((spc = strchr(buf, ' ')) == NULL)) {
		*cod = NULL;
		return -1;
	}
	rc = strtoul(spc, cod, 10);
	while (**cod == ' ') {
		(*cod)++;
	}
	return rc;
}

static int
brief_list(int tgtfd, int srcfd)
{
	char buf[4096U];
	ical_parser_t pp = NULL;
	/* trial read, just to see if the 200/OK has come */
	ssize_t nrd;
	ssize_t beef;
	char *cod;
	int rc;

	if (UNLIKELY((nrd = read(srcfd, buf, sizeof(buf))) < 0)) {
		errno = 0, serror("\
Error: no reply from server");
		return -1;
	} else if (UNLIKELY((beef = http_hdr_len(buf, nrd)) < 0)) {
		errno = 0, serror("\
Error: invalid reply from server");
		return -1;
	} else if (UNLIKELY((rc = http_ret_cod(buf, &cod, beef)) < 0)) {
		errno = 0, serror("\
Error: invalid reply from server");
		return -1;
	} else if (UNLIKELY(rc != 200)) {
		errno = 0, serror("\
server returned: %s", cod);
		return -1;
	}

	if (UNLIKELY(nrd - beef <= 0)) {
		/* have to do another roundtrip to the reader */
		nrd = read(srcfd, buf, sizeof(buf));
		beef = 0;
	}
	if (UNLIKELY(nrd - beef <= 0)) {
		/* no chance then, bugger off */
		errno = 0, serror("\
Error: incomplete reply from server");
		return -1;
	} else if (echs_evical_push(&pp, buf + beef, nrd - beef) < 0) {
		/* pushing won't help */
		errno = 0, serror("\
Error: incomplete reply from server");
		return -1;
	}
	/* alright jump into it */
	fdbang(tgtfd);
	goto brief;
more:
	switch ((nrd = read(srcfd, buf, sizeof(buf)))) {
		echs_instruc_t ins;

	default:
		if (echs_evical_push(&pp, buf, nrd) < 0) {
			/* pushing more brings nothing */
			break;
		}
		/*@fallthrough@*/
	case 0:
	brief:
		do {
			ins = echs_evical_pull(&pp);

			if (UNLIKELY(ins.v != INSVERB_SCHE)) {
				break;
			} else if (UNLIKELY(ins.t == NULL)) {
				continue;
			}
			/* otherwise print him briefly */
			if (ins.t->oid) {
				const char *const str = obint_name(ins.t->oid);
				const size_t len = strlen(str);
				fdwrite(str, len);
			}
			fdputc('\t');
			{
				const char *const str = ins.t->cmd;
				const size_t len = strlen(str);
				fdwrite(str, len);
			}
			fdputc('\n');

			/* and free him */
			free_echs_task(ins.t);
		} while (1);
		if (LIKELY(nrd > 0)) {
			goto more;
		}
		/*@fallthrough@*/
	case -1:
		/* last ever pull this one */
		ins = echs_evical_last_pull(&pp);

		if (UNLIKELY(ins.v != INSVERB_SCHE)) {
			break;
		} else if (UNLIKELY(ins.t != NULL)) {
			/* not on my turf */
			free_echs_task(ins.t);
		}
		break;
	}
	fdflush();
	return 0;
}

static int
ical_list(int tgtfd, int srcfd)
{
	char buf[4096U];
	/* trial read, just to see if the 200/OK has come */
	ssize_t nrd;
	ssize_t beef;
	char *cod;
	int rc;

	if (UNLIKELY((nrd = read(srcfd, buf, sizeof(buf))) < 0)) {
		errno = 0, serror("\
Error: no reply from server");
		return -1;
	} else if (UNLIKELY((beef = http_hdr_len(buf, nrd)) < 0)) {
		errno = 0, serror("\
Error: invalid reply from server");
		return -1;
	} else if (UNLIKELY((rc = http_ret_cod(buf, &cod, beef)) < 0)) {
		errno = 0, serror("\
Error: invalid reply from server");
		return -1;
	} else if (UNLIKELY(rc != 200)) {
		errno = 0, serror("\
server returned: %s", cod);
		return -1;
	}
	/* off we go */
	fdbang(tgtfd);

	/* write out initial portion of data we've got */
	fdwrite(buf + beef, nrd - beef);

	while ((nrd = read(srcfd, buf, sizeof(buf))) > 0) {
		fdwrite(buf, nrd);
	}
	fdflush();
	return 0;
}


#if defined STANDALONE
#include "echsq.yucc"

static int
cmd_list(struct yuck_cmd_list_s argi[static 1U])
{
/* try and request the schedules */
	static const char verb[] = "GET /";
	static const char vers[] = " HTTP/1.1\r\n\r\n";
	static const char sche[] = "sched";
	static const char queu[] = "queue";
	char buf[4096U];
	size_t bix;
	size_t i = 0U;
	size_t nfail;
	uid_t u;

#define BUF	(buf + bix)
#define BSZ	(sizeof(buf) - bix)
#define CHK_BIX()				\
	if (UNLIKELY(bix > sizeof(buf))) {	\
		goto reqstr_err;		\
	} else (void)0				\

	/* first of all transmogrify user name to uid */
	if (argi->user_arg) {
		char *on = NULL;
		u = strtoul(argi->user_arg, &on, 10);

		if (on == NULL || *on) {
			struct passwd *p;

			if ((p = getpwnam(argi->user_arg)) == NULL) {
				goto pwnam_err;
			}
			u = p->pw_uid;
		}
	}

	if (!argi->cmd) {
		/* echsq with no command equal list --brief */
		argi->brief_flag = 1;
	}

more:
	nfail = 0U;
	bix = 0U;
	/* right lets start with the http verb and stuff */
	bix += xstrlncpy(BUF, BSZ, verb, strlenof(verb));
	CHK_BIX();

	if (argi->user_arg) {
		bix += snprintf(BUF, BSZ, "u/%u/", u);
		CHK_BIX();
	}

	/* paste the routine we want invoked */
	if (argi->next_flag) {
		bix += xstrlncpy(BUF, BSZ, sche, strlenof(sche));
	} else {
		bix += xstrlncpy(BUF, BSZ, queu, strlenof(queu));
	}
	CHK_BIX();

	/* now if there's tasks put a ?tuid=x,y,z */
	if (i < argi->nargs) {
		bix += snprintf(BUF, BSZ, "?tuid=%s", argi->args[i++]);
		CHK_BIX();
	}
	for (int n; i < argi->nargs; i++, bix += n) {
		n = snprintf(BUF, BSZ, "&tuid=%s", argi->args[i]);
		if (UNLIKELY(bix + n >= sizeof(buf))) {
			/* just do him next time */
			break;
		} else if (UNLIKELY(bix + strlenof(vers) >= sizeof(buf))) {
			/* we can't have no space for the version suffix */
			break;
		}
	}
	/* version and stuff */
	bix += xstrlncpy(BUF, BSZ, vers, strlenof(vers));

	/* let's try the local echsd and then the system-wide one */
	with (int s) {
		if (UNLIKELY((s = get_esock(false)) < 0)) {
			nfail++;
			break;
		}
		/* send off our command */
		write(s, buf, bix);
		/* listing */
		if (!argi->brief_flag) {
			ical_list(STDOUT_FILENO, s);
		} else {
			brief_list(STDOUT_FILENO, s);
		}
		/* drain and close S */
		free_conn(s);
	}

	/* global queue */
	with (int s) {
		if (UNLIKELY((s = get_esock(true)) < 0)) {
			nfail++;
			break;
		}
		/* send off our command */
		write(s, buf, bix);
		/* listing */
		if (!argi->brief_flag) {
			ical_list(STDOUT_FILENO, s);
		} else {
			brief_list(STDOUT_FILENO, s);
		}
		/* drain and close S */
		free_conn(s);
	}
	if (UNLIKELY(nfail >= 2U)) {
		goto sock_err;
	} else if (UNLIKELY(i < argi->nargs)) {
		goto more;
	}
	return 0;

#undef BUF
#undef BSZ
#undef CHK_BIX

pwnam_err:
	serror("\
Error: cannot resolve user name `%s'", argi->user_arg);
	return 1;

sock_err:
	errno = 0, serror("\
Error: cannot connect to echsd, is it running?");
	return 1;

reqstr_err:
	serror("\
Error: cannot build request string");
	return 1;
}

static int
cmd_add(const struct yuck_cmd_add_s argi[static 1U])
{
/* scan for BEGIN:VEVENT/END:VEVENT pairs */
	size_t i = 0U;
	int fd;
	int s;

	if (!argi->nargs && isatty(STDIN_FILENO)) {
		/* ah, use a template and fire up an editor */
		if (UNLIKELY((fd = use_tmpl()) < 0)) {
			return 1;
		}
	}

	/* let's try the local echsd and then the system-wide one */
	if (UNLIKELY(argi->dry_run_flag)) {
		s = STDOUT_FILENO;
	} else if ((s = get_esock(false)) < 0 && (s = get_esock(true)) < 0) {
		errno = 0, serror("Error: cannot connect to echsd");
		return 1;
	}

	write(s, vcal_hdr, strlenof(vcal_hdr));
	if (!argi->nargs && isatty(STDIN_FILENO)) {
		/* template mode */
		goto proc;
	} else if (!argi->nargs) {
		fd = STDIN_FILENO;
		goto proc;
	}
	for (; i < argi->nargs; i++) {
		const char *const fn = argi->args[i];

		if (UNLIKELY(fn[0U] == '-' && !fn[1U])) {
			/* that's stdin in disguise */
			fd = STDIN_FILENO;
		} else if (UNLIKELY((fd = open(fn, O_RDONLY)) < 0)) {
			serror("\
Error: cannot open file `%s'", fn);
			continue;
		}

	proc:
		add_fd(s, fd);
		close(fd);
	}
	write(s, vcal_ftr, strlenof(vcal_ftr));

	if (argi->dry_run_flag) {
		/* nothing is outstanding in dry-run mode */
		return 0;
	}

	/* wait for all the season greetings and congrats ... */
	while (nout && !(poll1(s, 5000) < 0));

	free_conn(s);
	return 0;
}

static int
cmd_edit(const struct yuck_cmd_edit_s argi[static 1U])
{
/* try and request the schedules */
	static const char verb[] = "GET /";
	static const char vers[] = " HTTP/1.1\r\n\r\n";
	static const char queu[] = "queue";
	static char tmpfn[] = "/tmp/taskXXXXXXXX";
	char buf[4096U];
	size_t bix = 0U;
	bool realm = 0;
	int tmpfd;
	int s;

	if (!argi->nargs) {
		errno = 0, serror("Error: TUID argument is mandatory");
		return 1;
	} else if (UNLIKELY((tmpfd = mkstemp(tmpfn)) < 0)) {
		serror("Error: cannot create temporary file `%s'", tmpfn);
		return 1;
	} else if ((s = get_esock(realm)) < 0 && (s = get_esock(++realm)) < 0) {
		errno = 0, serror("Error: cannot connect to echsd");
		return 1;
	}

#define BUF	(buf + bix)
#define BSZ	(sizeof(buf) - bix)
#define CHK_BIX()				\
	if (UNLIKELY(bix > sizeof(buf))) {	\
		goto reqstr_err;		\
	} else (void)0				\

	/* right lets start with the http verb and stuff */
	bix += xstrlncpy(BUF, BSZ, verb, strlenof(verb));
	CHK_BIX();

	/* paste the routine we want invoked */
	bix += xstrlncpy(BUF, BSZ, queu, strlenof(queu));
	CHK_BIX();

	/* put the ?tuid=x,y,z */
	bix += snprintf(BUF, BSZ, "?tuid=%s", argi->args[0U]);
	CHK_BIX();

	for (size_t i = 1U, n; i < argi->nargs; i++, bix += n) {
		n = snprintf(BUF, BSZ, "&tuid=%s", argi->args[i]);
		if (UNLIKELY(bix + n >= sizeof(buf))) {
			/* user can go fuck themself */
			break;
		} else if (UNLIKELY(bix + strlenof(vers) >= sizeof(buf))) {
			/* there'd be no space for the version suffix */
			break;
		}
	}
	/* version and stuff */
	bix += xstrlncpy(BUF, BSZ, vers, strlenof(vers));

	/* send off our command */
	write(s, buf, bix);
	/* and push results to tmpfd */
	ical_list(tmpfd, s);
	/* drain and close */
	free_conn(s);
	/* lest we molest our EDITOR child ... */
	fd_cloexec(tmpfd);
	/* now the editing bit */
	run_editor(tmpfn);

	/* don't keep this file, we talk descriptors */
	unlink(tmpfn);
	/* rewind tmpfd ... */
	lseek(tmpfd, 0, SEEK_SET);
	/* ... and get the socket back we had to let go of */
	if (argi->dry_run_flag) {
		s = STDOUT_FILENO;
	} else if ((s = get_esock(realm)) < 0) {
		errno = 0, serror("Error: cannot connect to echsd");
		close(tmpfd);
		return 1;
	}
	/* ... and add the stuff back to echsd */
	write(s, vcal_hdr, strlenof(vcal_hdr));
	add_fd(s, tmpfd);
	write(s, vcal_ftr, strlenof(vcal_ftr));
	close(tmpfd);

	if (argi->dry_run_flag) {
		/* nothing is outstanding in dry-run mode */
		return 0;
	}

	/* wait for all the season greetings and congrats ... */
	while (nout && !(poll1(s, 5000) < 0));

	/* drain and close */
	free_conn(s);
	return 0;

#undef BUF
#undef BSZ
#undef CHK_BIX

reqstr_err:
	serror("\
Error: cannot build request string");
	return 1;
}

static int
cmd_cancel(const struct yuck_cmd_cancel_s argi[static 1U])
{
/* scan for BEGIN:VEVENT/END:VEVENT pairs */
	static const char hdr[] = "\
BEGIN:VCALENDAR\n\
VERSION:2.0\n\
PRODID:-//GA Financial Solutions//echse//EN\n\
METHOD:CANCEL\n\
CALSCALE:GREGORIAN\n";
	static const char ftr[] = "\
END:VCALENDAR\n";
	static const char beg[] = "BEGIN:VEVENT\n";
	static const char end[] = "END:VEVENT\n";
	static const char sta[] = "STATUS:CANCELLED\n";
	int s;

	/* let's try the local echsd and then the system-wide one */
	if (!argi->nargs) {
		errno = 0, serror("Error: TUID argument is mandatory");
		return 1;
	} else if (argi->dry_run_flag) {
		s = STDOUT_FILENO;
	} else if ((s = get_esock(false)) < 0 && (s = get_esock(true)) < 0) {
		errno = 0, serror("Error: cannot connect to echsd");
		return 1;
	}
	/* we'll be writing to S, better believe it */
	fdbang(s);

	fdwrite(hdr, strlenof(hdr));
	for (size_t i = 0U; i < argi->nargs; i++) {
		const char *tuid = argi->args[i];

		fdwrite(beg, strlenof(beg));
		fdprintf("UID:%s\n", tuid);
		fdwrite(sta, strlenof(sta));
		fdwrite(end, strlenof(end));
		nout++;
	}
	fdwrite(ftr, strlenof(ftr));

	(void)fdputc;
	fdflush();

	if (argi->dry_run_flag) {
		/* nothing is outstanding in dry-run mode */
		return 0;
	}

	/* wait for outstanding beer offers */
	while (nout && !(poll1(s, 5000) < 0));

	free_conn(s);
	return 0;
}

int
main(int argc, char *argv[])
{
	/* command line options */
	yuck_t argi[1U];
	int rc = 0;

	if (yuck_parse(argi, argc, argv)) {
		rc = 1;
		goto out;
	}

	switch (argi->cmd) {
	case ECHSQ_CMD_NONE:
	case ECHSQ_CMD_LIST:
		/* with no command just list queues */
		rc = cmd_list((struct yuck_cmd_list_s*)argi);
		break;


	case ECHSQ_CMD_ADD:
		rc = cmd_add((struct yuck_cmd_add_s*)argi);
		break;

	case ECHSQ_CMD_CANCEL:
		rc = cmd_cancel((struct yuck_cmd_cancel_s*)argi);
		break;

	case ECHSQ_CMD_EDIT:
		rc = cmd_edit((struct yuck_cmd_edit_s*)argi);
		break;

	default:
		break;
	}
out:
	yuck_free(argi);
	return rc;
}
#endif	/* STANDALONE */

/* echsq.c ends here */
