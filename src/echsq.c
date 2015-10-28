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
#include "evical.h"
#include "fdprnt.h"
#include "intern.h"

extern char **environ;

#if !defined _PATH_TMP
# define _PATH_TMP	"/tmp/"
#endif	/* _PATH_TMP */


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


static const char*
get_esock(bool systemp)
{
/* return a candidate for the echsd socket, system-wide or user local */
	static char hname[HOST_NAME_MAX];
	static size_t hnamez;
	static const char rundir[] = "/var/run";
	static const char sockfn[] = ".echse/=echsd";
	struct stat st;
	static char d[PATH_MAX];
	size_t di;

	/* populate with hostname we're running on */
	if (UNLIKELY(!hnamez && gethostname(hname, sizeof(hname)) < 0)) {
		perror("Error: cannot get hostname");
		return NULL;
	} else if (!hnamez) {
		hnamez = strlen(hname);
	}

	if (systemp) {
		di = xstrlncpy(d, sizeof(d), rundir, strlenof(rundir));
		d[di++] = '/';
		di += xstrlncpy(
			d + di, sizeof(d) - di,
			sockfn + 1U, strlenof(sockfn) - 1U);

		if (stat(d, &st) < 0 || !S_ISSOCK(st.st_mode)) {
			return NULL;
		}
	} else {
		static const char esokfn[] = "=echsd";
		struct passwd *pw;
		uid_t u = getuid();

		if ((pw = getpwuid(u)) == NULL || pw->pw_dir == NULL) {
			/* there's nothing else we can do */
			return NULL;
		} else if (stat(pw->pw_dir, &st) < 0 || !S_ISDIR(st.st_mode)) {
			/* gimme a break! */
			return NULL;
		}

		di = xstrlcpy(d, pw->pw_dir, sizeof(d));
		d[di++] = '/';
		di += xstrlncpy(
			d + di, sizeof(d) - di,
			sockfn, strlenof(sockfn) - strlenof(esokfn));
		/* now the machine name */
		di += xstrlncpy(d + di, sizeof(d) - di, hname, hnamez);
		d[di++] = '/';
		/* and the =echsd bit again */
		di += xstrlncpy(
			d + di, sizeof(d) - di,
			esokfn, strlenof(esokfn));

		if (stat(d, &st) < 0 || !S_ISSOCK(st.st_mode)) {
			return NULL;
		}
	}
	/* success */
	return d;
}

static int
make_conn(const char *path)
{
/* run LIST command on socket PATH */
	struct sockaddr_un sa = {AF_UNIX};
	int s;
	size_t z;

	if (UNLIKELY((s = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)) {
		return -1;
	}
	z = xstrlcpy(sa.sun_path, path, sizeof(sa.sun_path));
	z += sizeof(sa.sun_family);
	if (UNLIKELY(connect(s, (struct sockaddr*)&sa, z) < 0)) {
		goto fail;
	}
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
run_editor(const char *fn, int fd)
{
	static const char sh[] = "/bin/sh";
	static char *args[] = {"sh", "-c", NULL, NULL};
	const char *editor;
	posix_spawn_file_actions_t fa;
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

	if (posix_spawn_file_actions_init(&fa) < 0) {
		serror("Error: cannot initialise file actions");
		rc = -1;
		goto out;
	}

	/* fiddle with the child's descriptors */
	rc += posix_spawn_file_actions_addclose(&fa, fd);

	/* call /bin/sh with the above proto-task as here-document
	 * and stdout redir'd to FD */
	if (UNLIKELY(posix_spawn(&p, sh, &fa, NULL, args, environ) < 0)) {
		serror("Error: cannot run /bin/sh");
		rc = -1;
	} else {
		int st;
		while (waitpid(p, &st, 0) != p);
		if (!WIFEXITED(st) || WEXITSTATUS(st) != EXIT_SUCCESS) {
			rc = -1;
		}
	}

	/* also get rid of the file actions resources */
	posix_spawn_file_actions_destroy(&fa);
out:
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
	static char tmpf[] = "/tmp/taskXXXXXXXX";
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
	int fd;

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
	if ((fd = mkstemp(tmpf)) < 0) {
		serror("Error: cannot create temporary file `%s'", tmpf);
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
	rc += posix_spawn_file_actions_adddup2(&fa, fd, STDOUT_FILENO);
	rc += posix_spawn_file_actions_adddup2(&fa, fd, STDERR_FILENO);
	rc += posix_spawn_file_actions_addclose(&fa, fd);
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

	/* launch the editor so the user can peruse the proto-task */
	if (run_editor(tmpf, fd) < 0) {
		goto clo;
	}
	/* don't keep this file, we talk descriptors */
	unlink(tmpf);
	/* rewind it though */
	lseek(fd, 0, SEEK_SET);
	return fd;
clo:
	unlink(tmpf);
	close(pd[0U]);
	close(pd[1U]);
	close(fd);
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
	*eoh = buf + bi;
	bi += xstrlncpy(buf + bi, bsz - bi, dotechs, strlenof(dotechs));
	buf[bi++] = '/';
	*eoe = buf + bi;
	if (UNLIKELY(gethostname(buf + bi, bsz - bi) < 0)) {
		return 0U;
	}
	return bi + strlen(*eoe);
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
brief_list(int fd)
{
	char buf[4096U];
	ical_parser_t pp = NULL;
	/* trial read, just to see if the 200/OK has come */
	ssize_t nrd;
	ssize_t beef;
	char *cod;
	int rc;

	if (UNLIKELY((nrd = read(fd, buf, sizeof(buf))) < 0)) {
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
		nrd = read(fd, buf, sizeof(buf));
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
	goto brief;
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
				fputs(obint_name(ins.t->oid), stdout);
			}
			fputc('\t', stdout);
			fputs(ins.t->cmd, stdout);
			fputc('\n', stdout);

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
	return 0;
}

static int
ical_list(int fd)
{
	char buf[4096U];
	/* trial read, just to see if the 200/OK has come */
	ssize_t nrd;
	ssize_t beef;
	char *cod;
	int rc;

	if (UNLIKELY((nrd = read(fd, buf, sizeof(buf))) < 0)) {
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

	/* write out initial portion of data we've got */
	write(STDOUT_FILENO, buf + beef, nrd - beef);

	while ((nrd = read(fd, buf, sizeof(buf))) > 0) {
		write(STDOUT_FILENO, buf, nrd);
	}
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
	size_t conn;
	const char *e;
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
	conn = 0U;
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
		if (UNLIKELY((e = get_esock(false)) == NULL)) {
			conn++;
			break;
		} else if (UNLIKELY((s = make_conn(e)) < 0)) {
			serror("Error: cannot connect to `%s'", e);
			goto conn_err;
		}
		/* send off our command */
		write(s, buf, bix);
		/* listing */
		if (!argi->brief_flag) {
			ical_list(s);
		} else {
			brief_list(s);
		}
		/* drain and close S */
		free_conn(s);
	}

	/* global queue */
	with (int s) {
		if (UNLIKELY((e = get_esock(true)) == NULL)) {
			conn++;
			break;
		} else if (UNLIKELY((s = make_conn(e)) < 0)) {
			goto conn_err;
		}
		/* send off our command */
		write(s, buf, bix);
		/* listing */
		if (!argi->brief_flag) {
			ical_list(s);
		} else {
			brief_list(s);
		}
		/* drain and close S */
		free_conn(s);
	}
	if (UNLIKELY(conn >= 2U)) {
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

conn_err:
	serror("\
Error: cannot connect to `%s'", e);
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
	static const char hdr[] = "\
BEGIN:VCALENDAR\n\
VERSION:2.0\n\
PRODID:-//GA Financial Solutions//echse//EN\n\
METHOD:PUBLISH\n\
CALSCALE:GREGORIAN\n";
	static const char ftr[] = "\
END:VCALENDAR\n";
	const char *e;
	int s = -1;
	size_t i = 0U;
	int fd;

	if (!argi->nargs && isatty(STDIN_FILENO)) {
		/* ah, use a template and fire up an editor */
		if (UNLIKELY((fd = use_tmpl()) < 0)) {
			return 1;
		}
	}

	/* let's try the local echsd and then the system-wide one */
	if (UNLIKELY(argi->dry_run_flag)) {
		s = STDOUT_FILENO;
	} else if (!((e = get_esock(false)) || (e = get_esock(true)))) {
		errno = 0, serror("Error: cannot connect to echsd");
		return 1;
	} else if ((s = make_conn(e)) < 0) {
		serror("Error: cannot connect to `%s'", e);
		return 1;
	} else {
		/* otherwise it's time for a `yay' */
		errno = 0, serror("connected to %s ...", e);
	}

	write(s, hdr, strlenof(hdr));
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
	write(s, ftr, strlenof(ftr));

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
	const char *e;
	int s = -1;

	/* let's try the local echsd and then the system-wide one */
	if (argi->dry_run_flag) {
		s = STDOUT_FILENO;
	} else if (!((e = get_esock(false)) || (e = get_esock(true)))) {
		errno = 0, serror("Error: cannot connect to echsd");
		return 1;
	} else if ((s = make_conn(e)) < 0) {
		serror("Error: cannot connect to `%s'", e);
		return 1;
	} else {
		/* otherwise it's time for a `yay' */
		errno = 0, serror("connected to %s ...", e);
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

	default:
		break;
	}
out:
	yuck_free(argi);
	return rc;
}
#endif	/* STANDALONE */

/* echsq.c ends here */
