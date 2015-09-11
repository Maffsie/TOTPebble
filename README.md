# TOTPebble (pebble-authenticator fork)

TOTPebble generates [TOTP](http://en.wikipedia.org/wiki/Time-based_One-time_Password_Algorithm) tokens for use with any service supporting the TOTP standard for two-factor authentication, including Google, Microsoft and many other companies and services.

This is an SDK3/bugfix/minor feature update/fork of pebble-authenticator by [Neal](https://github.com/Neal).

Changes from pebble-authenticator:
* Removed JS configuration, as it added bulk
* Added a very short pulse when a TOTP token expires, indicating that it has changed
* Added a 1Password-style validity timer/ticker
* Changed the font to Helvetica Neue Ultralight because I thought it looked nicer

Plans for the future:
* Maybe a new icon
* Maybe add in JS configuration if I can figure out a way of avoiding secrets passing through network
* Pebble Time-specific niceties (colours, antialiasing and whatnot)
* At present I -think- the wrong functions are being used for getting the current time, messing up timezone stuff, so I want to fix that

## Requirements

Requires that Pebble SDK 3.x be installed.

## Configuration

* Copy `configuration-sample.txt` to `configuration.txt` and add your TOTP secrets.

## Install

	$ pebble build
	$ pebble install
