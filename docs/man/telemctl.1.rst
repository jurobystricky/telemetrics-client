========
telemctl
========

-------------------------------------
Telemetry service administration tool
-------------------------------------

:Copyright: \(C) 2017 Intel Corporation, CC-BY-SA-3.0
:Manual section: 1


SYNOPSIS
========

``telemctl``

``/etc/telemetrics/opt-in``


DESCRIPTION
===========

Control actions for telemetry services. The command can be used to start,
restart, or stop ``telemprobd``\(1) and ``telempostd``\(1), or to opt-in or opt-out of telemetry delivery of records to a central telemetry service.


OPTIONS
=======

 * ``start``|``stop``|``restart``:
   Starts, stops or restarts all running telemetry services.

 * ``opt-in``:
   Opts in to telemetry and the opt-in file ``/etc/telemetrics/opt-in``
   is created. Note: this is a one time required operation before
   telemetry can be used the first time.

 * ``opt-out``:
   Opts out of telemetry, and stops telemetry services. The opt-in file
   ``/etc/telemetrics/opt-in`` is deleted.

 * ``is-active``:
   Checks if telemetry client daemons are active (telemprobd and telempostd).


RETURN VALUES
=============

0 on success. A non-zero exit code indicates a failure occurred.


SEE ALSO
========

* ``telemprobd``\(1)
* https://github.com/clearlinux/telemetrics-client
* https://clearlinux.org/documentation/
