# TOTPebble (pebble-authenticator fork)

TOTPebble generates [TOTP](http://en.wikipedia.org/wiki/Time-based_One-time_Password_Algorithm) tokens for use with any service supporting the TOTP standard for two-factor authentication, including Google, Microsoft and many other companies and services.

This is an SDK3/bugfix/minor feature update/fork of pebble-authenticator by [Neal](https://github.com/Neal).

Changes from pebble-authenticator:
* Removed JS configuration, as it added bulk
* Added a very short pulse when a TOTP token expires, indicating that it has changed
* Added a 1Password-style validity timer/ticker
* Changed the font to Helvetica Neue Ultralight because I thought it looked nicer

Plans for the future:
* Redesign based around the Cards UI paradigm, a-la [pebble's cards-example](https://github.com/pebble-examples/cards-example)
* Maybe add in JS configuration if I can figure out a way of avoiding secrets passing through network
* Make proper use of basalt-specific features (colours, namely)
* Add support for chalk (some UI changes necessary to fit the odd screen)

## Requirements

Requires that Pebble SDK 3.x be installed, and that your target platform supports SDK3. Aplite devices running a pebbleOS version that does not fully support SDK3 will not be able to run this watchapp.

## Configuration

* Copy `configuration-sample.txt` to `configuration.txt` and add your TOTP secrets.

## Install

	$ pebble build
	$ pebble install