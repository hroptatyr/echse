/*** evical.h -- icalendar event stremas
 *
 * Copyright (C) 2013-2014 Sebastian Freundt
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
#if !defined INCLUDED_evical_h_
#define INCLUDED_evical_h_

#include "event.h"
#include "evstrm.h"
#include "evrrul.h"

typedef void *ical_parser_t;


/**
 * Stream ctor, from .ics file FN. */
extern echs_evstrm_t make_echs_evical(const char *fn);

/**
 * Initialise ical printing. */
extern void echs_prnt_ical_init(void);

/**
 * Finish ical printing. */
extern void echs_prnt_ical_fini(void);

/**
 * Print a single event in ical format. */
extern void echs_prnt_ical_event(echs_event_t);

/**
 * For filters and command-line stuff. */
extern struct rrulsp_s echs_read_rrul(const char *str, size_t len);

/* The pull parser */
/**
 * Feed the pull parser P a buffer BUF of size BSZ.
 * Initially, the value of P shall be set to NULL to instantiate a new
 * parser state. */
extern int
echs_evical_push(ical_parser_t p[static 1U], const char *buf, size_t bsz);

/**
 * Parse buffer pushed into the pull parser P, return an echs_evstrm_t
 * object every time one becomes available, or NULL otherwise,
 * indicating that more data needs to be pushed. */
extern echs_evstrm_t echs_evical_pull(ical_parser_t p[static 1U]);

/**
 * Indicate that this will be the last pull. */
extern echs_evstrm_t echs_evical_last_pull(ical_parser_t p[static 1U]);

#endif	/* INCLUDED_evical_h_ */
