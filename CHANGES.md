# CHANGES - OpenPrinting cups-browsed v2.1.1 - 2025-01-08

## CHANGES IN V2.1.1 (8th January 2025)

- Do not use global HTTP connection to local CUPS
  cups-browsed used a simgle HTTP connection to CUPS and preserved it
  in a global variable throughout its whole life. With the addition of
  multi-threading this caused race-conditions and especially
  cups-browsed getting stuck in a busy loop. Now we create separate
  HTTP connections each time we need one, to eliminate this problem
  (Ubuntu bugs #2049315, #2067918, and #2073504, CUPS Issue #879).

- Fix uninitialized make_model in create_queue()
  Initialized the buffer by putting a terminating zero to its
  beginning, also removed a wrong use of sizeof() (applying to pointer
  no to buffer, Issue #42).


## CHANGES IN V2.1.0 (17th October 2024)

- Removed support for legacy CUPS browsing and for LDAP
  Legacy CUPS browsing is not needed any more and, our implementation
  accepting any UDP packet on port 631, causes vulnerabilities, and
  our LDAP support is does not comly with RFC 7612 and is therefore
  limited. Fixes CVE-2024-47176 and CVE-2024-47850

- Default `BrowseRemoteProtocols` should not include `cups` protocol
  Works around CVE-2024-47176, the fix is the complete removal of
  legacy CUPS Browsing functionality

- Do not generate PPD for remote raw queues
  If the destination queue is raw, the local queue generated by
  cups-browsed should also be raw (Pull request #44).

- `daemon/cups-browsed.service`: Add `system-cups.slice`
  The `system-cups.slice` file is not required. The `system-cups`
  slice will be automatically created if the file is missing (cups
  Pull request #1035, Pull request #35).

- `cups-browsed.c`: Remove duplicate `#include ...`
  Pull request #40


## CHANGES IN V2.0.1 (15th August 2024)

- Ignore attributes with `IPP_TAG_NOVALUE`
  We don't need to record attributes with `IPP_TAG_NOVALUE` value tag,
  cupsd will decide what to use based on the incoming document (Pull
  request #33).

- Ignore attributes with empty values
  Previously, if IPP attribute value was empty, cups-browsed saved the
  attribute as printer option "<name>=0", e.g. `orientation-requested=0`.
  Often `0` is invalid and as no cluster member printer has this value
  therefore a job with this value gets rejected (Pull request #32).

- README.md: Use MarkDown for link to bug tracker
  (Pull request #31)

- Initialize variables to not get used uninitialized
  (Pull request #26)

- Fix memory leak found by valgrind
  (Pull request #26)

- Fix the daemon crash when `get-printer-attributes` fails
  cups-browsed crashed when it found a remote CUPS queue shared by
  mDNS on local network, but IPP request for this queue failed. The
  empty `prattrs` data structure is later accessed, which causes the
  crash (Pull request #25).

- Better handle damage of queues created by cups-browsed
  cups-browsed-generated print queues are marked with a
  "cups-browsed=true" attribute. Sometimes it gets lost and the queue
  not considered cups-browsed-generated any more.
  As queues with "implicitclass://..." device URI are always from
  cups-browsed, ignore the missing flag and always restore them as
  cups-browsed queues, so that cups-browsed assigns a destination
  printer for each job (Issue #429)

- Fix build with Avahi disabled
  (Issue #19, Pull request #20)


## CHANGES IN V2.0.0 (22th September 2023)

- Ensure we always send a valid name to `remove_bad_chars()`
  In case the found queue is a CUPS remote queue shared via DNS-SD,
  the `rp_value` can be without '/', which leads to `cups-browsed`
  crash if it is set to create the local queue based on the remote
  name (Pull request #13, Fedora Bugzilla #2150035).

- Use description/location from server if available, otherwise from client
  When we create a local queue we first check whether we actually got
  description and location strings from the remote server/printer, if
  they are empty we do not set empty strings but use the IPP
  attributes saved locally for our local queue. This way, if the
  server does not provide description/location and the user sets their
  own, that one is conserved through reboots and daemon restarts
  (Issue #323).


## CHANGES IN V2.0rc2 (20th June 2023)

- Fixed cups-browsed getting stuck in busy loop
  When the function create_queue() fails to create a local print queue
  and the failure is not intermittent, it sets a global variable to
  stop the main thread's loop for updating local queues. With the
  variable not reset no queue updates happened ever again and
  cups-browsed fell into a busy loop taking up to 100% CPU. We have
  solved this by doing away with the variable and simply mark these
  printers as disappeared (Ubuntu bug
  [#2018504](https://bugs.launchpad.net/bugs/2018504).

- Do not record `*-default` IPP attributes of local CUPS queues
  Many of the `*-default` IPP attributes represent properties already
  covered by the PPD option defaults which we also record. In
  addition, there is also `print-quality-default` where IPP reports
  `draft`, `normal`, and `high` settings while CUPS only accepts `3`,
  `4`, and `5`, and on everything else it sets
  `print-quality-default=0` which is invalid and jobs do not get
  printed. So we stop saving and loading these attributes.

- Build system: Removed unnecessary lines in Makefile.am
  Removed the `TESTdir` and `TEST_SCRIPTS` entries in Makefile.am.
  They are not needed and let `make install` try to install
  `run-tests.sh` in the source directory, where it already is, causing
  an error.

- `run-tests.sh`: Use pkgconfig instead of deprecated cups-config
  (Pull request #9).


## CHANGES IN V2.0rc1 (12th April 2023)

- Prefer sending jobs in Apple Raster instead of in PDF
  If a destination printer supports both PDF and Apple Raster, and if
  it is not a remote CUPS queue, prefer sending the job in Apple
  Raster, as printers print this more reliably.

      https://bugs.launchpad.net/bugs/2014976

- run-tests.sh: Let emulated printers support PDF input
  To test that cups-browsed prefers Apple Raster when the printer
  supports both PDF and Apple Raster as input format, we let the
  printers emulated by ippeveprinter also support PDF.

- implicitclass backend: NULL-initialize filter data field for Raster header
  We are running a filter chain without PPD file, so we do not have
  Raster header, so initialize it to NULL.


## CHANGES IN V2.0b4 (16th March 2023)

- Added test script for `make test`/`make check`, CI, autopkgtest, ...
  The script test/run-tests.sh creates emulations of IPP printers via
  `ippeveprinter` (of CUPS 2.x) and checks whether cups-browsed
  creates corresponding CUPS queues, whether a job to such a queue
  gets actually printed, and whether cups-browsed removes the queues
  again when the printers are shut down.

- `implicitclass` backend: If no destination got reported by
  cups-browsed, retry after one minute, not the standard 5 minutes of
  CUPS.

- `debug_printf()`: Check for need of log rotation only if log file is
  set and opened, to avoid a crash.

- `on_printer_modified()`: Added NULL check to avoid a crash.

- `ipp_discoveries_add()`: Ignore duplicate entries. These are most
  probably caused by a bug in Avahi, having certain discoveries of a
  printer reported twice and others not. When the printer disappears
  Avahi reports the disappearal of each discovery correctly, leaving
  the duplicate entry untreated (removing only one instance of it) and
  cups-browsed assumes that the printer is still there, keeping its
  CUPS queue.

- `update_cups_queues()`: Reset counter for pausing CUPS queue updates.
  Otherwise after having updated the number of queues supposed to be
  the maximum for one run of `update_cups_queues()`, cups-browsed will
  never update any queue again.

- `resolve_callback()`/`resolver_wrapper()`: New thread only when
  printer found
  We move the check which resolver event we have (found/failure)
  already in the main thread (`resolver_wrapper()`) and launch a new
  thread only if we have found a new printer and have to investigate
  whether to add a queue for it or not. `resolve_callback()` only
  initiates this investigation now.  This way we do not need to pass
  the resolver data structure (of type `AvahiServiceResolver*`) into
  the new thread, which caused segfaults.

- `create_remote_printer_entry()`: Corrected some memory freeing when
  a printer data structure is deleted, but this has not caused a
  segfault in the recent tests.

- Fixed issues reported by Red Hat Coverity tool (Pull request #6)

- `configure.ac`: Change deprecated `AC_PROG_LIBTOOL` for `LT_INIT`
  (Pull request #5)

- `configure.ac`: cups-browsed doesn't need C++


## CHANGES IN V2.0b3 (31st January 2023)

- COPYING, NOTICE: Simplification for autotools-generated files
  autotools-generated files can be included under the license of the
  upstream code, and FSF copyright added to upstream copyright
  list. Simplified COPYING appropriately.

- Makefile.am: Include LICENSE in distribution tarball


## CHANGES IN V2.0b2 (8th January 2023)

- cups-browsed is not part of cups-filters any more. Reflect this in
  screen messages and comments in cups-browsed.c.

- Makefile.am: Include NOTICE in distribution tarball

- configure.ac: Added "foreign" to to AM_INIT_AUTOMAKE() call. Makes
  automake not require a file named README.

- Cleaned up .gitignore

- Tons of fixes in the source code documentation: README.md, INSTALL,
  DEVELOPING.md, CONTRIBUTING.md, COPYING, NOTICE, ... Adapted to the
  libcupsfilters component, added links.


## CHANGES IN V2.0b1 (18th November 2022)

- Added multi-threaded operation, the Avahi resolver callback (which
  examines the remote printer, registers it, checks whether we want a
  local queue for it, adds it to a cluster, ...) and the
  creation/modification of a local CUPS queue is now done in separate
  threads, so that these processes can get executed in parallel to
  keep the local queues up-to-date more timely and to not overload the
  system's resources.  Thanks a lot to Mohit Mohan who did this work
  as
  [Google Summer of Code 2020 project](https://github.com/mohitmo/GSoC-2020-Documentation).

- Let the implicitclass backend use filter functions instead of
  calling filter executables.

- Build system, README.md: Require CUPS 2.2.2+. Removed now unneeded
  ./configure switches for old CUPS versions.

- Build system: Remove '-D_PPD_DEPRECATED=""' from the compiling
  command lines of the source files which use libcups. The flag is not
  supported any more for longer times already and all the PPD-related
  functions deprecated by CUPS have moved into libppd now.

- Build system: Add files in gitignore that are generated by
  "autogen.sh", "configure", and "make" (Pull request #336).

- implicitclass: Added "#include <signal.h>" (Issue #335).
