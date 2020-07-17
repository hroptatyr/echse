---
title: echse
project: echse
layout: project
---

echse
=====

In a nutshell: a cron daemon on [RFC 5545][1] calendar files
(aka iCalendar aka ics) with execution resolution up to one second,
per-task configuration of everything, and which can be run in
daemon mode by any user.


Features
--------

* Tasks can be run with second precision
* More flexible recurrence (anything ical RRULEs can express)
* Per-task configuration of mail
* Per-task configuration of working directory and shell
* Per-task configuration of simultaneity
* Automatic and shell-independent capture of timing information
* Per-task configuration of timezones
* Per-task configuration of the environment
* Built-in timeouts by constraining a task through DTEND/DURATION
* Job metadata logging as VJOURNAL


Red tape
--------

+ licence: [BSD3c][2]
+ github: <https://github.com/hroptatyr/echse>
+ issues: <https://github.com/hroptatyr/echse/issues>
+ releases: <https://github.com/hroptatyr/echse/releases>

The [beef branch][4] contains iCal files for holidays and trading hours
licensed under [CC BY 3.0][3].


iCalendar extensions
--------------------

While one of echse' objectives is to define recurring events in a
widely-adopted format (for obvious reasons of interoperability),
another, even more important objective is to concisely and accurately
express a task's surroundings.

[RFC 5545][1] comes preloaded with oodles of fields and syntax to
describe calendar events, how to display them in a PIM application, who
they affect and how and when and whatnot.  Needless to say that's not
enough for an automated task executor.

The following list maps RFC 5545 fields into our problem domain:

SUMMARY
: Conveys the command line to execute.

ORGANIZER
: Specifies from whom sent mails appear to be.

ATTENDEE
: Specifies to whom mail output is sent.

DTSTART
: Start date/time of the job.

DTEND
: If the job is still running by then kill it.

DURATION
: Another way to express the time limit on a job.

LOCATION
: Run the job with the working directory set to this.

X-ECHS-SHELL
: Execute the command line through this shell (must support `-c CMD`).

X-ECHS-IFILE
: Pass this file to the command as stdin.

X-ECHS-OFILE
: Write any output on stdout into this file.

X-ECHS-EFILE
: Write any output on stderr into this file.

X-ECHS-MAIL-OUT
: If set to non-0 any output written on stdout is included in the mail.

X-ECHS-MAIL-ERR
: If set to non-0 any output written on stderr is included in the mail.

X-ECHS-MAIL-RUN
: If set to non-0 send a mail with the status information, this flag is
  implied when X-ECHS-MAIL-OUT or X-ECHS-MAIL-ERR is set.

X-ECHS-MAX-SIMUL
: A job is only run this many times simultaneously.

Also, [RFC 5545][1] RRULEs are powerful but yet not powerful enough to
capture common recurrences such as Easter, or too verbose to capture
recurrences like the weekday after the fourth Sunday every month (which
is either the fourth or fifth Monday or the first Monday of the
following month).

For that matter, we decided to extend RFC 5545 by additional RRULE
parts:

+ `BYEASTER=N[,...]`  for N in 0 to 366 or -1 to -366, denotes the N-th
  day after/before easter
+ `SHIFT=N[B]`  for N in 0 to 366 or -1 to -366, denotes to add N
  ([B]usiness) days to all dates in the current set, -0B is allowed and
  means to go back to Friday if the date is on Saturday or Sunday 
+ `SCALE=GREGORIAN|HIJRI` to change the calendar scale for RRULES.
  Note: The *output* calendar scale is `GREGORIAN` as per [RFC 5545][1].


Pronunciation
-------------

echse rhymes with *hexe*, however that is pronounced in your language.


Similar Projects
================

None.

  [1]: http://tools.ietf.org/html/rfc5545
  [2]: http://tldrlegal.com/license/bsd-3-clause-license-%28revised%29
  [3]: http://creativecommons.org/licenses/by/3.0/
  [4]: http://github.com/hroptatyr/echse/tree/beef
