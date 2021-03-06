Index Filesystem for FUSE
=========================

This is a useful little filesystem which takes a static index of files and remote
URLs for those files. Locally you see the file tree you define, but any accesses to those
files are then forwarded through CURL to the remote URLs.

There is only a small amount of in-memory caching:

* Any redirects are stored for later use
* The remote file size is saved on first query

While the filesystem data is readonly the filesystem itself is readwrite.  You can:

* Create directories with mkdir
* Delete directories with rmdir
* Touch (and otherwise create) new empty files
* Set the URL of a file with `setfattr -n url -v <url> <filename>`
* Trigger a refresh of the file size with `setfattr -n refresh -v 1 <filename>`
* Manually set the file size with `setfattr -n size -v <size> <filename>`

The state of the filesystem is stored at unmount time and reloaded at mount time.

The format of the file is text based and consists of rows of:

```
D<tab>Directory name
```

And

```
F<tab>File name
```
or
```
F<tab>File name<tab>URL
```

Optionally the file size can be appended :

```
F<tab>File name<tab>URL<tab>Size
```

To execute:

```
mount.indexfs [-o options] /path/to/config /mount/point
```

If the config file doesn't exist you will start with an empty filesystem and the file
will be created the first time you unmount.

You may also use it through mount, as long as mount.indexfs is installed in /sbin:

```
mount -t indexfs [-o options] /path/to/config /mount/point
```

Or in ftsab:

```
/path/to/config /mount/point indexfs allow_other 0 0
```

The option `allow_other` lets other people access the filesystem. Without that
only root will be able to see the files.

----

Saving the configuration
------------------------

The configuration is automatically saved to the specified config file when the filesystem
is unmounted.  A manual save can also be triggered at any time by sending the mount.indexfs
process a HUP signal:

```
killall -HUP mount.indexfs
```

Reloading the configuration
---------------------------

You can manually reload the configuration by sending the USR1 signal. This will iteratively add any new files/directories, change any URLs, and remove any stale records.

```
killall -USR1 mount.indexfs
```

----

URLs
----

Thanks to CURL being the backend almost any standard URI works. For example:

* http://path and https://path
* file:///path
* ftp://user:password@site/path
