Installation
============

echse is a standard autotools project, so installation steps are:

When using a tarball distribution:
----------------------------------
1. ./configure
2. make
3. make install

When using the git repository:
------------------------------
1. autoreconf -fi  (only when using the git repository)
2. ./configure
3. make
4. make install


Starting echsd
--------------

Unlike other cron implementations echse explicitly allows being run as
non-privileged user.  To do so invoke:

    $ echsd

Queue files and journals are placed in `~/.echse/<HOSTNAME>/`.  This
directory will be created if need be.  The socket file to communicate
with the echse daemon will be put into `/var/run/user/<UID>/echse/`.

In contrast, when invoked as superuser queue files and journals are
placed in `/var/spool/echse`.  Again, this directory will be created
if need be.  In superuser mode the socket file will be put into
`/var/run/echse/`.


Queuing
-------

To add tasks to or delete tasks from the queue use the `echsq(1)` tool.
This tool is aware of both running modes (non-privileged and privileged)
and will look for the communication socket file in both places.
