/*** echs-lisp.c -- echse collection file parser
 *
 * Copyright (C) 2013 Sebastian Freundt
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
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <ctype.h>

#include "echs-lisp.h"

#if !defined LIKELY
# define LIKELY(_x)	__builtin_expect((_x), 1)
#endif	/* !LIKELY */
#if !defined UNLIKELY
# define UNLIKELY(_x)	__builtin_expect((_x), 0)
#endif	/* UNLIKELY */
#if !defined UNUSED
# define UNUSED(x)	__attribute__((unused)) x
#endif	/* UNUSED */

#define P_FL		(PROT_READ)
#define M_FL		(MAP_PRIVATE)

typedef enum {
	UNK,
	L_PAREN,
	R_PAREN,
	USYMBOL,
	QSYMBOL,
	STRING,
	COMMENT,
	KEYWORD,
} toktyp_t;

struct token_s {
	toktyp_t tt;
	const char *s;
	size_t z;
};

struct tokon_s {
	struct token_s t;
	const char *on;
};


/* reserved keywords and symbols */
#include "echs-lisp-keys.c"
#include "echs-lisp-syms.c"
#include "echs-lisp-funs.c"


static const char*
next_non_space(const char *p, const char *ep)
{
	for (; p < ep && isspace(*p); p++);
	return p;
}

static const char*
next_non_graph(const char *p, const char *ep)
{
	for (; p < ep && (isalnum(*p) || *p == '-' || *p == '_'); p++);
	return p;
}

static const char*
next_line(const char *p, const char *ep)
{
	if (UNLIKELY((p = memchr(p, '\n', ep - p)) == NULL)) {
		return ep;
	}
	return p + 1;
}

static const char*
next_quot(const char *p, const char *ep)
{
	for (; p < ep && (*p == '\\' && ++p < ep || *p != '"'); p++);
	return p;
}


static struct tokon_s
next_token(const char *p, const char *ep)
{
	struct tokon_s res;

	/* overread whitespace */
	p = next_non_space(p, ep);

	if (UNLIKELY(p == ep)) {
		return (struct tokon_s){{UNK}, ep};
	}

	switch (*p) {
	case '(':
		res.t.tt = L_PAREN;
		res.t.s = p;
		res.t.z = 1;
		res.on = p + 1;
		break;
	case ')':
		res.t.tt = L_PAREN;
		res.t.s = p;
		res.t.z = 1;
		res.on = p + 1;
		break;
	case ';':
		res.t.tt = COMMENT;
		res.t.s = p;
		res.on = next_line(p + 1, ep);
		res.t.z = res.on - res.t.s - 1;
		break;
	case '\'':
		res.t.tt = QSYMBOL;
		res.t.s = p + 1;
		res.on = next_non_graph(res.t.s, ep);
		res.t.z = res.on - res.t.s;
		break;
	case '"':
		res.t.tt = STRING;
		res.t.s = p + 1;
		res.on = next_quot(res.t.s, ep) + 1;
		res.t.z = res.on - p - 2;
		break;
	case ':':
		res.t.tt = KEYWORD;
		res.t.s = p + 1;
		res.on = next_non_graph(res.t.s, ep);
		res.t.z = res.on - res.t.s;
		break;
	default:
		res.t.tt = USYMBOL;
		res.t.s = p;
		res.on = next_non_graph(res.t.s, ep);
		res.t.z = res.on - res.t.s;
		break;
	}
	return res;
}

#if defined DEBUG_FLAG
static void
pr_token(struct token_s t)
{
	printf("%u\t", t.tt);
	if (fwrite(t.s, 1, t.z, stdout) < t.z) {
		fputc('!', stdout);
	}
	fputc('\n', stdout);
	return;
}
#endif	/* DEBUG_FLAG */

static void
parse_file(const char *p, size_t z)
{
	const char *const ep = p + z;
	toktyp_t olty = UNK;

	for (struct tokon_s nxt; (nxt = next_token(p, ep)).t.tt;
	     p = nxt.on, olty = nxt.t.tt) {
		struct token_s t = nxt.t;

#if defined DEBUG_FLAG
		pr_token(t);
#endif	/* DEBUG_FLAG */

		switch (t.tt) {
		default:
			break;
		case USYMBOL: {
			const struct echs_lisp_fung_s *try;

			if (olty != L_PAREN) {
				break;
			}
			/* yes, function call */
			if ((try = echs_lisp_fung(t.s, t.z)) != NULL) {
				printf("(funcall #'%u)\n", try->klit);
			}
			break;
		}
		case KEYWORD: {
			const struct echs_lisp_keyg_s *try;

			/* keyword */
			if ((try = echs_lisp_keyg(t.s, t.z)) != NULL) {
				printf("keyword :%u\n", try->klit);
			}
			break;
		}
		case QSYMBOL: {
			const struct echs_lisp_symg_s *try;

			if ((try = echs_lisp_symg(t.s, t.z)) != NULL) {
				printf("qsymbol '%u\n", try->klit);
			}
			break;
		}
		}
	}
	return;
}


int
echs_lisp(const char *fn)
{
	struct stat st;
	size_t fz;
	int fd;
	void *fm;

	if (stat(fn, &st) < 0 || st.st_size < 0U) {
		return -1;
	} else if ((fz = st.st_size) == 0U) {
		return 0;
	} else if ((fd = open(fn, O_RDONLY)) < 0) {
		return -1;
	} else if ((fm = mmap(NULL, fz, P_FL, M_FL, fd, 0)) == MAP_FAILED) {
		return -1;
	}

	/* just call the parser */
	parse_file(fm, fz);

	(void)munmap(fm, fz);
	return close(fd);
}

/* echs-lisp.c ends here */
