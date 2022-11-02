# ClockThing
> An Arduino Alarm Clock for LILYGO LILY Pi ESP32

ClockThing lets you have a clock that's uses NTP to set the time, and (almost) fetches iCal feeds to handle timezones and when to alarm.

Current interim version fetches a custom feed generated from an iCal feed. The feed format looks like this:

```TZ	1647165600	-25200	PDT
TZ	1667725200	-28800	PST
ALARM	1667313900	wake up
ALARM	1667400300	wake up
ALARM	1667486700	wake up
ALARM	1667573100	wake up
ALARM	1667835900	wake up
ALARM	1667922300	wake up
COMPLETE	1667347021
```

The TZ lines indicate the start of DST changes, the offset on or after that time, and the three letter abbreviation when that time comes into effect.

The ALARM lines indicate the time of the alarm and the text

The COMPLETE line signals when the file was generated, and that the data was indeed complete.

A future version will parse iCal directly, eliminating the need for the external server (current server script available on request)

## Hardware

I'm currently using this with the LILYGO® TTGO LILY Pi with the ILI9481 screen. If you have the other screen, change config.h.

## Building

This should build with PlatformIO

## Meta

Richard Russo - wakingup@enslaves.us

[https://github.com/russor/ClockThing](https://github.com/russor/ClockThing)