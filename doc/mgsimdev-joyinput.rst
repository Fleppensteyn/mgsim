==================
 mgsimdev-joyinput
==================

-------------------------------------------------------------
 Pseudo-device providing input from joystick, mouse or touch
-------------------------------------------------------------

:Author: MGSim was created by Mike Lankamp. MGSim is now under
   stewardship of the Microgrid project. This manual page was written
   by Raphael 'kena' Poss.
:Date: October 2016
:Copyright: Copyright (C) 2008-2014 the Microgrid project.
:Version: PACKAGE_VERSION
:Manual section: 7


DESCRIPTION
===========

The joyinput device connects to joysticks, mouse and touch devices through
The SDL library and can provide input events and immediate data on device
state depending on the connected device.
This device usually requires a functioning SDL window to receive events due
to the way SDL works, therefore you probably have to use a gfx device to make
this work.

An I/O device of this type can be specified in MGSim using the device
type ``JoyInput``.

Event structure is described in ``MGInputEvents.h``.

CONFIGURATION
=============

``<dev>:JoyInputDeviceType``
   Indicates the type of device and can be one of the following.

   ``JOYSTICK``
      Connects to a Joystick through SDL. You can specify which one
      you want to connect to by setting the ``<dev>:InputJoystickIndex``
      to the number you want.

   ``MOUSE``
      Connects to the mouse as presented by SDL, which means that all
      available mice on the system are combined into one. This mode
      provides information about the pointer as it goes over a display
      connected to the simulation. This may include mouse events generated
      by a touch device, but those will not be included if there is another
      JoyInput component listening to only touch events.

   ``TOUCH``
      This mode only provides events and is untested. It will get all
      touch related events from SDL, convert them and send them through.

   ``REPLAY``
      A special mode that uses a previously recorded replay and sends the exact
      same responses as the previous time as long as the requests are the same.
      Currently this will not stall the simulation to make sure responses are not
      sent in earlier cycles than the time of recording.

``<dev>:JoyInputReplayFile``
   Indicates the filename to save/read a recording of all the read/write requests
   made to the device and what it responded with. If the device type is set to
   ``REPLAY`` then the file will be used to replay the events of the recording
   and otherwise it will write out a recording of the current run which can be
   replayed at a later time.


PROTOCOL
========

The device can be controlled and accessed through the interface.
The memory space is separated into several parts for controlling
the device and information about the device itself, general information about
the connected joystick/mouse, access to the events produced by the device, and
access to the current state of the connected device.

Controlling the device and interface info
-----------------------------------------
The first part of memory serves to control the device and is the only part of memory
that can be written to. This part can turn the device on, activate events, configure
and enable notifications and pop the event queue. It can tell you what kind of device
is connected through SDL, what the notification channel is and how many events are
queued.
For enabling and disabling the device, events and notifications writing a nonzero value
to their respective adresses will enable those respective functionalities while writing
zero will disable them. Reading from these adresses will return either 0 for disabled
or either the device type (for the adress that enables the device) or 1 to indicate if
any of the features are enabled.
Values for device type can be found at the top of ``Joyinput.cpp``, but currently they are starting from 1 and numbered sequentially: Joystick, Mouse, Touch.
The notification channel can be set by writing to the adress while reading from it returns the current channel.
The event queue can be popped by writing to the designated adress while reading from it will return the current queue length truncated to 255.

Getting information about the connected device
----------------------------------------------
This is the next region in memory and it can provide you with information about the SDL
device and how to access latter regions in memory that provide direct access to the current device state.
There are currently 4 different types of absolute value to be read from latter regions.
These are absolute axes, binary state buttons, 4bit state hats, and two axis relative balls.
This information can be read as a 4 byte value which contains 4 distinct byte sized values. These 4 values stand for the number of a certain kind of part, the access width
used to access the region for this, the amount of bits that represent a single value, and the amount of items per value.
This seems very unclear so I'll include an example.
For instance there are 6 axes on the standard Xbox 360 controller, you can read values
from the memory region for axes with 2 byte widths and every axe is represented by a
16 bit value which represents 1 axis. Thus the bytes, from most significant to least
significant, in the 4 byte value will be 0x06 0x02 0x10 0x01.
Buttons are represented as single bits in a byte and thus you can get the state of
8 buttons with a single read from the interface.
Hats have their state information in the 4 least significant bits of the byte read
from the interface and the more significant bits are not used at this point in time.
Balls are sligthly different from the other categories since they have to represent a
relative value for both the x- and y-axis at the same time, so these are presented individually as 2 byte values with two entries per ball (x and y respectively).

Reading events
--------------
Events are read with aligned 4 byte reads from the region assigned to it and they take the form described in ``MGInputEvents.h``. Events can be popped using the method described in the "Controlling the device and interface info" section.

Reading the absolute state
--------------------------
The regions for absolute state information can be accessed using the information provided in the "Getting information about the connected device" section.


INTERFACE
=========

The device presents itself with an interface with regions switched on certain bits in
the adress. The table below will tell you more about access widths and what certain
regions contain. Keep in mind that any non 1 width memory accesses should be aligned.
+----------------------+--------------+-------+-----+-----------------------------------------------+
| Adress bit values    |              |       |     |                                               |
+-------+----+---+-----+ Hex          | Width | R/W | Description                                   |
| 11-13 | 10 | 9 | 1-8 |              |       |     |                                               |
+-------+----+---+-----+--------------+-------+-----+-----------------------------------------------+
|     0 |  0 | 0 |   0 |            0 |   1   |  R  | 0 if disabled, device type otherwise          |
|       |    |   |     |              |       +-----+-----------------------------------------------+
|       |    |   |     |              |       |  W  | 0 disables, non-zero enables device           |
|       |    |   +-----+--------------+-------+-----+-----------------------------------------------+
|       |    |   |   1 |            1 |   1   |  R  | 1 if events are enabled                       |
|       |    |   |     |              |       +-----+-----------------------------------------------+
|       |    |   |     |              |       |  W  | 0 disables, non-zero enables events           |
|       |    |   +-----+--------------+-------+-----+-----------------------------------------------+
|       |    |   |   2 |            2 |   1   |  R  | 1 if notifications are enabled                |
|       |    |   |     |              |       +-----+-----------------------------------------------+
|       |    |   |     |              |       |  W  | 0 disables, non-zero enables notifications    |
|       |    |   +-----+--------------+-------+-----+-----------------------------------------------+
|       |    |   |   3 |            3 |   1   |  R  | The current notification channel              |
|       |    |   |     |              |       +-----+-----------------------------------------------+
|       |    |   |     |              |       |  W  | Set the notification channel                  |
|       |    |   +-----+--------------+-------+-----+-----------------------------------------------+
|       |    |   |   4 |            4 |   1   |  R  | The amount of queued events (up to 255)       |
|       |    |   |     |              |       +-----+-----------------------------------------------+
|       |    |   |     |              |       |  W  | Pop an event from the front of the queue      |
|       |    +---+-----+--------------+-------+-----+-----------------------------------------------+
|       |    | 1 |   0 |          100 |   4   |  R  | Information on the axes section               |
|       |    |   +-----+--------------+-------+-----+-----------------------------------------------+
|       |    |   |   4 |          104 |   4   |  R  | Information on the buttons section            |
|       |    |   +-----+--------------+-------+-----+-----------------------------------------------+
|       |    |   |   8 |          108 |   4   |  R  | Information on the hats section               |
|       |    |   +-----+--------------+-------+-----+-----------------------------------------------+
|       |    |   |  12 |          10C |   4   |  R  | Information on the balls section              |
|       +----+---+-----+--------------+-------+-----+-----------------------------------------------+
|       |  1 | 0,4..16 |      200-210 |   4   |  R  | Read from the event in 4-byte chunks          |
+-------+----+---------+--------------+-------+-----+-----------------------------------------------+
|     1 |  0,2,4...510 |      400-4FE |   2   |  R  | Direct access to axis states                  |
+-------+--------------+--------------+-------+-----+-----------------------------------------------+
|     2 |   0,1,2...31 |      800-81F |   1   |  R  | Direct access to bitsets with button states   |
+-------+--------------+--------------+-------+-----+-----------------------------------------------+
|     3 |  0,1,2...255 |      C00-CFF |   1   |  R  | Direct access to hat states                   |
+-------+--------------+--------------+-------+-----+-----------------------------------------------+
|     4 |  0,2,4..1022 |    1000-13FE |   2   |  R  | Direct access to ball states                  |
+-------+--------------+--------------+-------+-----+-----------------------------------------------+


SEE ALSO
========

mgsim(1), mgsimdoc(7)

BUGS
====

Report bugs & suggest improvements to PACKAGE_BUGREPORT.
