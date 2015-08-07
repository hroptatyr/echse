echse
=====

In a nutshell: a cron daemon on [RFC 5545][1] calendar files
(aka iCalendar aka ics) with execution resolution up to the
second and which can be run as user daemon.


Resources
---------

echse is hosted primarily on github:

+ github: <https://github.com/hroptatyr/echse>
+ issues: <https://github.com/hroptatyr/echse/issues>
+ releases: <https://github.com/hroptatyr/echse/releases>

echse comes preloaded with a bunch of holiday files and trading hours in
iCal format (in the [beef][2] branch).


Features
--------

* [X] Tasks can be run with second precision
* [X] More flexible recurrence (anything ical RRULEs can express)
* [X] Per-task configuration of mail
* [X] Per-task configuration of working directory and shell
* [X] Per-task configuration of simultaneity
* [X] Automatic and shell-independent capture of timing information
* [ ] Per-task configuration of timezones
* [ ] Per-task configuration of the environment
* [ ] Built-in timeouts by constraining a task through DTEND/DURATION


iCalendar extensions
--------------------

While one of echse' objectives is to define recurring events in a
widely-adopted format (for obvious reasons of interoperability),
another, even more important objective is to concisely and accurately
define recurring events.

[RFC 5545][1] RRULEs are powerful but yet not powerful enough to capture
common recurrences such as Easter, or too verbose to capture recurrences
like the weekday after the fourth Sunday every month (which is either
the fourth or fifth Monday or the first Monday of the following month).

For that matter, we decided to extend RFC 5545 by additional RRULE
parts:

+ BYEASTER=N[,...]  for N in 0 to 366 or -1 to -366, denotes the N-th
  day after/before easter
+ BYADD=N[,...]  for N in 0 to 366 or -1 to -366, denotes to add N days
  to all dates in the current set


Pronunciation
-------------

echse rhymes with hexe, however that is pronounced in your language.


Similar Projects
================

A project focussing on the holiday problem as well is [jollyday][3].
However, their holiday files are quite inaccurate (as of 2015).
Also, they use a non-standard XML-based format that I found to be too
inflexible, especially when it comes to single exceptional holidays or
exceptional observance/omittance of holidays.


  [1]: http://tools.ietf.org/html/rfc5545
  [2]: https://github.com/hroptatyr/echse/tree/beef
  [3]: http://jollyday.sourceforge.net/
