---
title: echse beef
layout: subpage
project: echse
---

echse beef
==========

While [echse][1] is a tool set to merge, filter and unroll recurrence
definitions in iCalendar files the actual USP of echse lies within its
database of holidays and trading hours, the stuff that we call beef,
in contrast to [echse][1] playing the role of cutlery.

Holidays ISO 3166
-----------------
- Domain: ISO 3166-1 and ISO 3166-2 codes
- Author: [Sebastian Freundt][5]
- Source: government web sites
- Licence: [Creative Commons Attribution 3.0 License][6]

Country and country subdivision holidays can be found at
[`holidays/iso-3166/`][4].  The ISO 3166-2 files complement their 3166-1
file.  Use `echse merge` or `echse select --format=ical` to obtain a
single calendar.


Holidays ISO 10383
------------------
- Domain: ISO 10383 market identifier codes
- Author: [Sebastian Freundt][5]
- Source: exchange web sites
- Licence: [Creative Commons Attribution 3.0 License][6]

Operating exchange and market holidays can be found at
[`holidays/iso-10383/`][7].  The operating exchange takes precedence
over its market places.  Use [echse][1] tools to obtain a single
calendar.


Development
-----------

The iCalendar files are managed in the [`beef` branch][2] of the
[echse repository][3].  Patches are welcome.

Seeing as the iCalendar files are largely independent, there will be no
releases.  The latest version can always be downloaded using git.

  [1]: http://www.fresse.org/echse/
  [2]: https://github.com/hroptatyr/echse/tree/beef
  [3]: https://github.com/hroptatyr/echse
  [4]: https://github.com/hroptatyr/echse/tree/beef/holidays/iso-3166
  [5]: http://www.fresse.org/
  [6]: http://creativecommons.org/licenses/by/3.0/
  [7]: https://github.com/hroptatyr/echse/tree/beef/holidays/iso-10383
