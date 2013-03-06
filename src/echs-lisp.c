/*** echs-lisp.c -- bindings for echs-lisp scripts
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
#define HAVE_INTTYPES_H	1
#define HAVE_STDINT_H	1
#include <libguile.h>
#include "strdef.h"
#include "fltdef.h"
#include "instant.h"
#include "dt-strpf.h"

#if !defined LIKELY
# define LIKELY(_x)	__builtin_expect((_x), 1)
#endif	/* !LIKELY */
#if !defined UNLIKELY
# define UNLIKELY(_x)	__builtin_expect((_x), 0)
#endif	/* UNLIKELY */
#if !defined UNUSED
# define UNUSED(x)		__attribute__((unused)) x
#endif	/* UNUSED */

#define GUILE_VERSION				\
	((SCM_MAJOR_VERSION * 100U +		\
	  SCM_MINOR_VERSION) * 100U +		\
	 SCM_MICRO_VERSION)

#if GUILE_VERSION >= 20000U
# define MYSCM_SYNTAX(RANAME, STR, CFN)					\
SCM_SNARF_HERE(static SCM CFN(SCM xorig, SCM env))			\
SCM_SNARF_INIT(scm_c_define(STR, scm_i_make_primitive_macro(STR, CFN)))

#elif GUILE_VERSION >= 10800U
# define MYSCM_SYNTAX(RANAME, STR, CFN)				\
SCM_SNARF_HERE(static const char RANAME[]=STR)			\
SCM_SNARF_INIT(scm_make_synt(RANAME, scm_i_makbimacro, CFN))

#else
# error unsupported guile version
#endif	/* GUILE_VERSION */

typedef void(*pset_f)(echs_filter_t, const char*, struct filter_pset_s);

struct echs_mod_smob_s {
	enum {
		EM_TYP_UNK,
		EM_TYP_STRM,
		EM_TYP_FILT,
	} typ;
	union {
		echs_strdef_t s;
		echs_fltdef_t f;
	};

	SCM fn;
};

struct echse_clo_s {
	size_t nstrms;
	struct {
		echs_stream_t s;
		echs_event_t ev;
	} *strms;
	/* last event served */
	echs_event_t last;
};


#if defined __INTEL_COMPILER
# pragma warning (disable:1418)
# pragma warning (disable:1419)
# pragma warning (disable:981)
# pragma warning (disable:2415)
#endif	/* __INTEL_COMPILER */

MYSCM_SYNTAX(s_defstrm, "defstrm", scm_m_defstrm);
MYSCM_SYNTAX(s_deffilt, "deffilt", scm_m_deffilt);

SCM_KEYWORD(k_from, "from");
SCM_KEYWORD(k_args, "args");

SCM_SYMBOL(sym_load, "load");
SCM_SYMBOL(sym_begin, "begin");
SCM_SYMBOL(sym_rset, "read-set!");
SCM_SYMBOL(sym_keywords, "keywords");
SCM_SYMBOL(sym_prefix, "prefix");
#if defined DEBUG_FLAG
SCM_SYMBOL(sym_top_repl, "top-repl");
#endif	/* DEBUG_FLAG */

static scm_t_bits scm_tc16_echs_mod;
/* date range to scan through */
static echs_instant_t from;
static echs_instant_t till;

SCM_SYMBOL(scm_sym_load_strm, "load-strm");
SCM_DEFINE(
	load_strm, "load-strm", 1, 0, 0,
	(SCM dso),
	"Load the stream from DSO.")
{
#define FUNC_NAME	"load-strm"
	SCM XSMOB;
	struct echs_mod_smob_s *smob;
	char *fn;

	SCM_VALIDATE_STRING(1, dso);
	fn = scm_to_locale_string(dso);

	/* alloc and ... */
	smob = scm_gc_malloc(sizeof(*smob), "echs-mod");
	/* init */
	smob->typ = EM_TYP_STRM;
	smob->s = echs_open(from, fn);
	smob->fn = dso;
	SCM_NEWSMOB(XSMOB, scm_tc16_echs_mod, smob);

	free(fn);
	return XSMOB;
#undef FUNC_NAME
}

static void
__load_filt_ass_kv(pset_f pset, echs_filter_t f, const char *key, SCM val)
{
	if (scm_is_string(val)) {
		size_t z;
		char *s = scm_to_locale_stringn(val, &z);

		pset(f, key, (struct filter_pset_s){PSET_TYP_STR, s, z});
		free(s);
	} else if (scm_is_pair(val)) {
		for (SCM h = val; !scm_is_null(h); h = SCM_CDR(h)) {
			__load_filt_ass_kv(pset, f, key, SCM_CAR(h));
		}
	}
	return;
}

static const char*
__stringify(SCM kw)
{
	static char buf[64] = ":";
	SCM kw_str;
	size_t z;

	kw_str = scm_symbol_to_string(scm_keyword_to_symbol(kw));
	z = scm_to_locale_stringbuf(kw_str, buf + 1, sizeof(buf) - 2);
	buf[++z] = '\0';
	return buf;
}

SCM_SYMBOL(scm_sym_load_filt, "load-filt");
SCM_DEFINE(
	load_filt, "load-filt", 1, 0, 1,
	(SCM dso, SCM rest),
	"Load the filter from DSO.")
{
#define FUNC_NAME	"load-filt"
	SCM XSMOB;
	struct echs_mod_smob_s *smob;
	pset_f pset;
	char *fn;

	SCM_VALIDATE_STRING(1, dso);
	fn = scm_to_locale_string(dso);

	/* alloc and ... */
	smob = scm_gc_malloc(sizeof(*smob), "echs-mod");
	/* init */
	smob->typ = EM_TYP_FILT;
	smob->f = echs_open_fltdef(from, fn);
	smob->fn = dso;
	SCM_NEWSMOB(XSMOB, scm_tc16_echs_mod, smob);

	free(fn);

	/* have we got a pset fun in our filter? */
	if ((pset = echs_fltdef_psetter(smob->f)) == NULL) {
		goto skip;
	}

	/* pass the keywords to the psetter */
	for (SCM k, v = rest; !scm_is_null(v); v = SCM_CDR(v)) {
		const char *key;

		if (!scm_is_keyword(k = SCM_CAR(v))) {
			continue;
		}
		v = SCM_CDR(v);

		key = __stringify(k);
		__load_filt_ass_kv(pset, smob->f.f, key, SCM_CAR(v));
	}
skip:
	return XSMOB;
#undef FUNC_NAME
}

static SCM
mark_echs_mod(SCM obj)
{
	struct echs_mod_smob_s *smob = (void*)SCM_SMOB_DATA(obj);

	return smob->fn;
}

static int
print_echs_mod(SCM obj, SCM port, scm_print_state *UNUSED(pstate))
{
	struct echs_mod_smob_s *smob = (void*)SCM_SMOB_DATA(obj);

	scm_puts("#<echs-", port);
	switch (smob->typ) {
	case EM_TYP_STRM:
		scm_puts("strm", port);
		break;
	case EM_TYP_FILT:
		scm_puts("filt", port);
		break;
	default:
		scm_puts("mod", port);
		break;
	}
	scm_puts(" :from ", port);
	scm_write(smob->fn, port);
	scm_puts(">", port);

	/* non-zero means success */
	return 1;
}

static size_t
free_echs_mod(SCM obj)
{
	struct echs_mod_smob_s *smob = (void*)SCM_SMOB_DATA(obj);

	switch (smob->typ) {
	case EM_TYP_STRM:
		if (smob->s.m != NULL) {
			/* probably a DSO */
			echs_close(smob->s);
		} else {
			/* probably our shit */
			void *sc = smob->s.s.clo;

			scm_gc_free(sc, sizeof(struct echse_clo_s), "echs-mod");
			smob->s.s = (echs_stream_t){NULL, NULL};;
		}
		break;
	default:
		/* bad sign */
		echs_close_fltdef(smob->f);
		break;
	}
	scm_gc_free(smob, sizeof(*smob), "echs-mod");
	return 0;
}

static int
scm_is_echs_strm(SCM x)
{
	struct echs_mod_smob_s *smob;

	if (SCM_TYP16_PREDICATE(scm_tc16_echs_mod, x) &&
	    (smob = (void*)SCM_SMOB_DATA(x))->typ == EM_TYP_STRM) {
		return 1;
	}
	return 0;
}

#define SCM_VALIDATE_ECHS_STRM(pos, obj)				\
	do {								\
		SCM_ASSERT(scm_is_echs_strm(obj), obj, pos, FUNC_NAME);	\
	} while (0)


/* some macros */
/* helpers */
static SCM
__begin(SCM form)
{
	return scm_cons(sym_begin, form);
}

static SCM
__load(const char *fn)
{
	return scm_cons2(sym_load, scm_from_locale_string(fn), SCM_EOL);
}

static __attribute__((unused)) SCM
_load(const char *fn)
{
	return scm_cons(__load(fn), SCM_EOL);
}

static SCM
__pset(SCM curr, SCM UNUSED(sym), SCM what, SCM val)
{
	SCM_SETCDR(curr, scm_cons2(what, val, SCM_EOL));
	return SCM_CDR(SCM_CDR(curr));
}

static SCM
__define(SCM sym, SCM what)
{
	return scm_list_3(scm_sym_define, sym, what);
}

static SCM
__rset(void)
{
	return scm_list_3(
		sym_rset,
		sym_keywords,
		scm_cons2(scm_sym_quote, sym_prefix, SCM_EOL));
}

static SCM
_rset(void)
{
	return scm_cons(__rset(), SCM_EOL);
}

static SCM
scm_m_defstrm(SCM expr, SCM UNUSED(env))
{
#define FUNC_NAME	"defstrm"
	SCM tail = SCM_CDR(expr);
	SCM dso = SCM_EOL;
	SCM sym;

	sym = SCM_CAR(tail);
	SCM_VALIDATE_SYMBOL(1, sym);

	if (!scm_is_null((tail = SCM_CDR(tail)))) {
		SCM tmp;

		if (scm_is_keyword(tmp = SCM_CAR(tail)) &&
		    scm_is_eq(tmp, k_from)) {
			tail = SCM_CDR(tail);
			dso = SCM_CAR(tail);
		}
	}

	if (scm_is_null(dso)) {
		dso = scm_symbol_to_string(sym);
	}
	expr = __define(sym, scm_cons2(scm_sym_load_strm, dso, SCM_EOL));

#if defined DEBUG_FLAG
	{
		SCM port = scm_current_output_port();
		scm_write(expr, port);
		scm_newline(port);
	}
#endif	/* DEBUG_FLAG */
	return expr;
#undef FUNC_NAME
}

static SCM
scm_m_deffilt(SCM expr, SCM UNUSED(env))
{
#define FUNC_NAME	"deffilt"
	SCM tail = SCM_CDR(expr);
	SCM dso = SCM_EOL;
	SCM pset;
	SCM sym;

	sym = SCM_CAR(tail);
	SCM_VALIDATE_SYMBOL(1, sym);

	expr = pset = __begin(SCM_EOL);

	while (!scm_is_null((tail = SCM_CDR(tail)))) {
		SCM tmp;

		if (scm_is_keyword(tmp = SCM_CAR(tail)) &&
		    scm_is_eq(tmp, k_from)) {
			tail = SCM_CDR(tail);
			dso = SCM_CAR(tail);
		} else if (scm_is_keyword(tmp)) {
			tail = SCM_CDR(tail);
			pset = __pset(pset, sym, tmp, SCM_CAR(tail));
		} else {
			/* must be args then innit? */
			pset = __pset(pset, sym, k_args, SCM_CAR(tail));
		}
	}

	if (scm_is_null(dso)) {
		dso = scm_symbol_to_string(sym);
	}

	/* bang the define */
	expr = __define(sym, scm_cons2(scm_sym_load_filt, dso, SCM_CDR(expr)));

#if defined DEBUG_FLAG
	{
		SCM port = scm_current_output_port();
		scm_write(expr, port);
		scm_newline(port);
	}
#endif	/* DEBUG_FLAG */
	return expr;
#undef FUNC_NAME
}


/* beef functionality */
static inline bool
__events_eq_p(echs_event_t e1, echs_event_t e2)
{
	return __inst_eq_p(e1.when, e2.when) &&
		(e1.what == e2.what || strcmp(e1.what, e2.what) == 0);
}

static int
materialise(echs_event_t e)
{
	static char buf[256];
	char *bp = buf;

	/* BEST has the guy */
	bp += dt_strf(buf, sizeof(buf), e.when);
	*bp++ = '\t';
	{
		size_t e_whaz = strlen(e.what);
		memcpy(bp, e.what, e_whaz);
		bp += e_whaz;
	}
	*bp++ = '\n';
	*bp = '\0';
	if (write(STDOUT_FILENO, buf, bp - buf) < 0) {
		return -1;
	}
	return 0;
}

static echs_event_t
__refill(echs_stream_t s, echs_instant_t last)
{
	echs_event_t e;

	do {
		if (__event_0_p(e = echs_stream_next(s))) {
			break;
		}
	} while (__event_lt_p(e, last));
	return e;
}

static echs_event_t
__stream(void *clo)
{
	struct echse_clo_s *x = clo;
	echs_instant_t bestinst;
	size_t bestindx;

	if (UNLIKELY(x->strms == NULL)) {
		return (echs_event_t){0};
	}
	/* start out with the best, non-0 index */
	bestindx = -1UL;
	bestinst = (echs_instant_t){.u = (uint64_t)-1};

	/* try and find the very next event out of all instants */
	for (size_t i = 0; i < x->nstrms; i++) {
		echs_instant_t inst = x->strms[i].ev.when;

		if (x->strms[i].s.f == NULL) {
			continue;
		} else if (__inst_0_p(inst)) {
		clos_0:
			memset(x->strms + i, 0, sizeof(*x->strms));
			continue;
		} else if (__inst_lt_p(inst, x->last.when) ||
			   __events_eq_p(x->strms[i].ev, x->last)) {
			echs_stream_t s = x->strms[i].s;
			echs_event_t e;

			if (__event_0_p(e = __refill(s, x->last.when))) {
				goto clos_0;
			}

			/* cache E */
			x->strms[i].ev = e;
			inst = e.when;
		}

		/* do the actual check */
		if (__inst_lt_p(inst, bestinst) || __inst_0_p(bestinst)) {
			bestindx = i;
			bestinst = x->strms[bestindx].ev.when;
		}
	}

	/* BEST has the guy */
	if (UNLIKELY(__event_0_p(x->last = x->strms[bestindx].ev))) {
		/* big fucking fuck */
		return (echs_event_t){0};
	}
	/* otherwise just use the cache */
	return x->strms[bestindx].ev;
}

SCM_DEFINE(
	make_stream, "make-stream", 0, 0, 1,
	(SCM s),
	"yep.")
{
#define FUNC_NAME	"make-stream"
	SCM XSMOB;
	SCM gath;
	struct echs_mod_smob_s *smob;
	struct echse_clo_s *x;
	echs_stream_t this;
	size_t k = 0;

	if (scm_is_null(s)) {
		SCM_WRONG_NUM_ARGS();
	}

	/* alloc and ... */
	smob = scm_gc_malloc(sizeof(*smob), "echs-mod");
	x = scm_gc_malloc(sizeof(*x), FUNC_NAME);
	this = (echs_stream_t){__stream, x};
	/* init */
	smob->typ = EM_TYP_STRM;
	smob->s = (echs_strdef_t){this, NULL};
	smob->fn = gath = scm_cons(SCM_BOOL_F, SCM_EOL);

	/* rinse */
	memset(x, 0, sizeof(*x));

	for (SCM tail = s; !scm_is_null(tail); tail = SCM_CDR(tail)) {
		struct echs_mod_smob_s *strm;
		SCM XSTRM = SCM_CAR(tail);

		SCM_VALIDATE_ECHS_STRM(++k, XSTRM);
		SCM_SETCDR(gath, scm_cons(XSTRM, SCM_EOL));
		gath = SCM_CDR(gath);
		/* deref */
		strm = (void*)SCM_SMOB_DATA(XSTRM);

		if (UNLIKELY((x->nstrms % 64U) == 0U)) {
			/* realloc the streams array */
			size_t ol_z = (x->nstrms + 0U) * sizeof(*x->strms);
			size_t nu_z = (x->nstrms + 64U) * sizeof(*x->strms);

			x->strms = scm_gc_realloc(
				x->strms, ol_z, nu_z, FUNC_NAME);
			memset(x->strms + x->nstrms, 0, nu_z - ol_z);
		}

		/* bang stream */
		x->strms[x->nstrms].s = strm->s.s;
		/* cache the next event */
		x->strms[x->nstrms].ev = echs_stream_next(strm->s.s);
		/* inc */
		x->nstrms++;
	}

	/* set last slot */
	x->last = (echs_event_t){.when = 0, .what = ""};

	/* reset the fn field */
	smob->fn = SCM_CDR(smob->fn);

	/* we're ready */
	SCM_NEWSMOB(XSMOB, scm_tc16_echs_mod, smob);
	return XSMOB;
#undef FUNC_NAME
}

SCM_DEFINE(
	stream, "stream", 0, 0, 1,
	(SCM s),
	"yep.")
{
#define FUNC_NAME	"stream"
	static struct echse_clo_s x[1];
	echs_stream_t this;
	size_t k = 0;

	if (scm_is_null(s)) {
		SCM_WRONG_NUM_ARGS();
	}

	for (SCM tail = s; !scm_is_null(tail); tail = SCM_CDR(tail)) {
		struct echs_mod_smob_s *smob;
		SCM strm = SCM_CAR(tail);

		SCM_VALIDATE_SMOB(++k, strm, echs_mod);
		smob = (void*)SCM_SMOB_DATA(strm);

		if (smob->typ != EM_TYP_STRM) {
			SCM_WRONG_TYPE_ARG(k, strm);
		}

		if (UNLIKELY((x->nstrms % 64U) == 0U)) {
			/* realloc the streams array */
			size_t ol_z = (x->nstrms + 0U) * sizeof(*x->strms);
			size_t nu_z = (x->nstrms + 64U) * sizeof(*x->strms);

			x->strms = realloc(x->strms, nu_z);
			memset(x->strms + x->nstrms, 0, nu_z - ol_z);
		}

		/* bang stream */
		x->strms[x->nstrms].s = smob->s.s;
		/* cache the next event */
		x->strms[x->nstrms].ev = echs_stream_next(smob->s.s);
		/* inc */
		x->nstrms++;
	}

	/* set last slot */
	x->last = (echs_event_t){.when = 0, .what = ""};

	/* set up this stream */
	this.f = __stream;
	this.clo = x;

	/* just iterate */
	for (echs_event_t e;
	     (e = echs_stream_next(this),
	      !__event_0_p(e) && __event_le_p(e, till));) {
		if (UNLIKELY(materialise(e) < 0)) {
			break;
		}
	}

	/* tidy up */
	free(x->strms);
	memset(x, 0, sizeof(*x));
	return SCM_BOOL_T;
#undef FUNC_NAME
}


void
init_echs_lisp(void)
{
#if !defined SCM_MAGIC_SNARFER
#	include "echs-lisp.x"
#endif	/* SCM_MAGIC_SNARFER */
	scm_tc16_echs_mod = scm_make_smob_type(
		"echs-mod", sizeof(struct echs_mod_smob_s));
	scm_set_smob_mark(scm_tc16_echs_mod, mark_echs_mod);
	scm_set_smob_print(scm_tc16_echs_mod, print_echs_mod);
	scm_set_smob_free(scm_tc16_echs_mod, free_echs_mod);
	return;
}


#if defined STANDALONE
#if defined __INTEL_COMPILER
# pragma warning (disable:593)
# pragma warning (disable:181)
#elif defined __GNUC__
# pragma GCC diagnostic ignored "-Wswitch"
# pragma GCC diagnostic ignored "-Wswitch-enum"
#endif /* __INTEL_COMPILER */
#include "echse-clo.h"
#include "echse-clo.c"
#if defined __INTEL_COMPILER
# pragma warning (default:593)
# pragma warning (default:181)
#elif defined __GNUC__
# pragma GCC diagnostic warning "-Wswitch"
# pragma GCC diagnostic warning "-Wswitch-enum"
#endif	/* __INTEL_COMPILER */

struct clo_s {
	unsigned int ninp;
	const char *const *inp;
};

static void*
boot(void *clo)
{
	const struct clo_s *c = clo;
	SCM form;
	SCM tail;

	/* initialise our system */
	init_echs_lisp();

	/* start out with a begin block */
	form = tail = __begin(SCM_EOL);
	SCM_SETCDR(tail, _rset());
	tail = SCM_CDR(tail);

	for (unsigned int i = 0; i < c->ninp; i++) {
		SCM_SETCDR(tail, _load(c->inp[i]));
		tail = SCM_CDR(tail);
	}

#if defined DEBUG_FLAG
	/* stay in the shell */
	SCM_SETCDR(tail, scm_cons(scm_cons(sym_top_repl, SCM_EOL), SCM_EOL));
#endif	/* DEBUG_FLAG */

	scm_eval_x(form, scm_current_module());
	return NULL;
}

int
main(int argc, char **argv)
{
	/* command line options */
	struct echs_args_info argi[1];
	int res = 0;

	if (echs_parser(argc, argv, argi)) {
		res = 1;
		goto out;
	}

	if (argi->from_given) {
		from = dt_strp(argi->from_arg);
	} else {
		from = (echs_instant_t){2000, 1, 1};
	}

	if (argi->till_given) {
		till = dt_strp(argi->till_arg);
	} else {
		till = (echs_instant_t){2037, 12, 31};
	}

	scm_with_guile(boot, &(struct clo_s){argi->inputs_num, argi->inputs});

out:
	echs_parser_free(argi);
	return res;
}
#endif	/* STANDALONE */

/* echs-lisp.c ends here */
