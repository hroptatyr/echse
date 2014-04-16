Echse
=====

A simple toolset to display, unroll, merge or select events and
recurring events from RFC 5545 calendar files (aka iCalendar), with a
particular emphasis on financial applications, specifically holiday
calendars or exchange trading hours.

As such, only the VEVENT component and therewithin only a limited number
of fields are meaningful to echse.  For a full-fledged command-line
icalendar editor have a look at [calcurse][1]

Resources
---------
echse is hosted primarily on github:

+ github: <https://github.com/hroptatyr/echse>
+ issues: <https://github.com/hroptatyr/echse/issues>
+ releases: <https://github.com/hroptatyr/echse/releases>

echse comes preloaded with a bunch of holiday files and trading hours in
iCal format (in the [beef][2] branch).

Similar Projects
================

A project focussing on the holiday problem as well is [jollyday][3].
However, their holiday files are quite inaccurate (as of early 2014).
Also, they use a non-standard XML-based format that I found to be too
inflexible, especially when it comes to single exceptional holidays or
exceptional observance/omittance of holidays.

  [1]: http://calcurse.org
  [2]: https://github.com/hroptatyr/echse/tree/beef
  [3]: http://jollyday.sourceforge.net/
