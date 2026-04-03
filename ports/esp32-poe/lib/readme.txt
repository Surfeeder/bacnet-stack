
This directory is for project-specific (private) libraries.
PlatformIO compiles them to static libraries and links them into the firmware.

Place each library's source in its own sub-directory, for example:

|--lib
|  |--bacnet          <-- BACnet stack files (copy from the repo root)
|  |  |--bacnet/      <-- all headers from include/bacnet/
|  |  |--*.c          <-- required .c files (see list in readme.txt)
|  |- readme.txt       <-- THIS FILE

Required BACnet stack files (copy from src/ and apps/demo/handler/):

  abort.c
  address.c
  apdu.c
  bacaddr.c
  bacapp.c
  bacdcode.c
  bacerror.c
  bacint.c
  bacreal.c
  bacstr.c
  bvlc.c
  cov.c
  datetime.c
  bacdevobjpropref.c
  dcc.c
  debug.c
  h_bbmd.c
  h_cov.c
  h_ucov.c
  h_npdu.c
  h_rp.c
  h_rpm.c
  h_whois.c
  h_wp.c
  iam.c
  hostnport.c
  memcopy.c
  noserv.c
  npdu.c
  proplist.c
  reject.c
  rp.c
  rpm.c
  s_iam.c
  tsm.c
  whois.c
  wp.c
  device.c            (from bacnet/basic/object/)
  ai.c                (optional — Analog Input object)
  bo.c                (optional — Binary Output object)

Copy all headers from include/ keeping the directory structure, e.g.:
  lib/bacnet/bacnet/bacdef.h
  lib/bacnet/bacnet/datalink/bip.h
  ...

More information about PlatformIO Library Dependency Finder:
http://docs.platformio.org/page/librarymanager/ldf.html
