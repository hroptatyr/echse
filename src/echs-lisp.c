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
	tstrng_t s;
};

struct tokon_s {
	struct token_s t;
	const char *on;
};


/* helpers */
static tstrng_t
tstrngdup(tstrng_t s)
{
	return (tstrng_t){strndup(s.s, s.z), s.z};
}


/* reserved keywords and symbols */
#include "echs-lisp-keys.c"
#include "echs-lisp-syms.c"
#include "echs-lisp-funs.c"


/* token state machine */
static void
ass_keyw_u(struct collect_s *c, echs_lisp_key_t k, tstrng_t s)
{
	switch (c->f) {
	case ECHS_LISP_COLLECT:
		switch (k) {
		case ECHS_LISP__AS:
			c->as = tstrngdup(s);
			break;
		}
		break;
	default:
		break;
	}
	return;
}

static void
ass_keyw_q(struct collect_s *c, echs_lisp_key_t k, echs_lisp_sym_t s)
{
	switch (c->f) {
	case ECHS_LISP_COLLECT:
		switch (k) {
		case ECHS_LISP__ON_OVERLAP:
			c->on_olap = s;
			break;
		}
		break;
	default:
		break;
	}
	return;
}

static echs_lisp_sym_t
__get_sym(tstrng_t s)
{
	const struct echs_lisp_symg_s *try;

	if (s.z == 0U) {
		/* could be a quoted list or something */
		return ECHS_LISP_QUNK;
	} else if ((try = echs_lisp_symg(s.s, s.z)) == NULL) {
		return ECHS_LISP_QUNK;
	}
	return try->klit;
}

static echs_lisp_key_t
__get_key(tstrng_t s)
{
	const struct echs_lisp_keyg_s *try;

	if (UNLIKELY(s.z == 0U)) {
		/* could be a quoted list or something */
		return ECHS_LISP__UNK;
	} else if ((try = echs_lisp_keyg(s.s, s.z)) == NULL) {
		return ECHS_LISP__UNK;
	}
	return try->klit;
}

static echs_lisp_fun_t
__get_fun(tstrng_t s)
{
	const struct echs_lisp_fung_s *try;

	if (UNLIKELY(s.z == 0U)) {
		/* could be a quoted list or something */
		return ECHS_LISP_UNK;
	} else if ((try = echs_lisp_fung(s.s, s.z)) == NULL) {
		return ECHS_LISP_UNK;
	}
	return try->klit;
}

static int
record_state(struct token_s t)
{
	static struct token_s st;
	static struct collect_s curr[1];
	static echs_lisp_key_t lstkey;

	switch (st.tt) {
	case KEYWORD:
		switch (t.tt) {
		default:
			/* syntax error */
			break;
		case USYMBOL:
			/* aaah keyword value pair */
			ass_keyw_u(curr, lstkey, t.s);
			break;
		case QSYMBOL:
			ass_keyw_q(curr, lstkey, __get_sym(t.s));
			break;
		}
		st = (struct token_s){UNK};
		break;
	case UNK:
		switch (t.tt) {
		default:
		case COMMENT:
			break;
		case KEYWORD:
			st = t;
			lstkey = __get_key(t.s);
			break;
		case L_PAREN:
			/* prepare funcall */
			st = (struct token_s){L_PAREN};
			break;
		case R_PAREN:
			switch (curr->f) {
			case ECHS_LISP_COLLECT:
				add_collect(*curr);
				break;
			default:
				break;
			}
			st = (struct token_s){UNK};
			break;
		case QSYMBOL:
			st = t;
			break;
		case STRING:
			item_ll_add(curr->items, make_item(t.s));
			break;
		}
		break;
	case L_PAREN:
		switch (__get_fun(t.s)) {
		case ECHS_LISP_COLLECT:
			*curr = (struct collect_s){ECHS_LISP_COLLECT};
			st = (struct token_s){UNK};
			break;
		default:
			/* big error */
			return -1;
		}
		break;
	case QSYMBOL:
		switch (t.tt) {
		case L_PAREN:
			/* aah quoted list */
			break;
		case R_PAREN:
			/* quoted list end */
			st = (struct token_s){UNK};
			break;
		}
	}
	return 0;
}


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
		res.t.s = (tstrng_t){p, 1U};
		res.on = p + 1;
		break;
	case ')':
		res.t.tt = R_PAREN;
		res.t.s = (tstrng_t){p, 1U};
		res.on = p + 1;
		break;
	case ';':
		res.t.tt = COMMENT;
		res.on = next_line(p + 1, ep);
		res.t.s = (tstrng_t){p, res.on - p - 1};
		break;
	case '\'':
		res.t.tt = QSYMBOL;
		res.on = next_non_graph(p + 1, ep);
		res.t.s = (tstrng_t){p + 1, res.on - p - 1};
		break;
	case '"':
		res.t.tt = STRING;
		res.on = next_quot(p + 1, ep) + 1;
		res.t.s = (tstrng_t){p + 1, res.on - p - 2};
		break;
	case ':':
		res.t.tt = KEYWORD;
		res.on = next_non_graph(p + 1, ep);
		res.t.s = (tstrng_t){p + 1, res.on - p - 1};
		break;
	default:
		res.t.tt = USYMBOL;
		res.on = next_non_graph(p, ep);
		res.t.s = (tstrng_t){p, res.on - p};
		break;
	}
	return res;
}

#if defined DEBUG_FLAG
static void
pr_token(struct token_s t)
{
	printf("%u\t", t.tt);
	if (fwrite(t.s.s, 1, t.s.z, stdout) < t.s.z) {
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

	for (struct tokon_s nxt; (nxt = next_token(p, ep)).t.tt; p = nxt.on) {
		struct token_s t = nxt.t;

#if defined DEBUG_FLAG
		pr_token(t);
#endif	/* DEBUG_FLAG */

		if (record_state(t) < 0) {
			puts("syntax error");
			break;
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
