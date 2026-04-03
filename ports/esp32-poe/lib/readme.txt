
This directory is for project-specific (private) libraries.
PlatformIO compiles them to static libraries and links them into the firmware.

Create a sub-directory lib/bacnet/ and copy the files listed below from the
repository root, keeping the same relative paths so that #include directives
resolve correctly.

Required BACnet stack .c files (paths relative to repository root):

  src/bacnet/abort.c
  src/bacnet/bacaddr.c
  src/bacnet/bacapp.c
  src/bacnet/bacdcode.c
  src/bacnet/bacdevobjpropref.c
  src/bacnet/bacerror.c
  src/bacnet/bacint.c
  src/bacnet/bacreal.c
  src/bacnet/bacstr.c
  src/bacnet/cov.c
  src/bacnet/datetime.c
  src/bacnet/dcc.c
  src/bacnet/hostnport.c
  src/bacnet/iam.c
  src/bacnet/memcopy.c
  src/bacnet/npdu.c
  src/bacnet/proplist.c
  src/bacnet/reject.c
  src/bacnet/rp.c
  src/bacnet/rpm.c
  src/bacnet/whois.c
  src/bacnet/wp.c
  src/bacnet/datalink/bvlc.c
  src/bacnet/basic/binding/address.c
  src/bacnet/basic/bbmd/h_bbmd.c
  src/bacnet/basic/npdu/h_npdu.c
  src/bacnet/basic/object/device.c
  src/bacnet/basic/service/h_apdu.c
  src/bacnet/basic/service/h_cov.c
  src/bacnet/basic/service/h_noserv.c
  src/bacnet/basic/service/h_rp.c
  src/bacnet/basic/service/h_rpm.c
  src/bacnet/basic/service/h_ucov.c
  src/bacnet/basic/service/h_whois.c
  src/bacnet/basic/service/h_wp.c
  src/bacnet/basic/service/s_iam.c
  src/bacnet/basic/sys/debug.c
  src/bacnet/basic/tsm/tsm.c

Optional object files (add if you need these BACnet object types):
  src/bacnet/basic/object/ai.c   (Analog Input)
  src/bacnet/basic/object/bo.c   (Binary Output)

Copy all headers from include/ keeping the directory structure, e.g.:
  lib/bacnet/include/bacnet/bacdef.h
  lib/bacnet/include/bacnet/datalink/bip.h
  ...

More information about PlatformIO Library Dependency Finder:
http://docs.platformio.org/page/librarymanager/ldf.html
