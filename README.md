Index Filesystem for FUSE
=========================

This is a useful little filesystem which takes a static index of files and remote
URLs for those files. Locally you see the file tree you define, but any accesses to those
files are then forwarded through CURL to the remote URLs.

There is only a small amount of in-memory caching:

* Any redirects are stored for later use
* The remote file size is saved on first query


