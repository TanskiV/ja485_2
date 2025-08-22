# ModbusMaster-Electron [![Build Status](https://travis-ci.org/peergum/ModbusMaster-Electron.svg?branch=master)](https://travis-ci.org/peergum/ModbusMaster-Electron)
ModbusMaster library for the Electron

This is a derived work from 4-20's library for Arduinos.

Due to time constraint, specific parts/defines to Arduinos were removed, rather than adapted to the Electron environment.
Please feel free to improve, adapt or extend as necessary.

Cheers
Phil.

## Changelog
* 1.0.2: improved performance, added example, allowed parity configuration
* 1.0.3: increased inter-char timeout from 300ms to 500ms, and send a zero after setting speed for serial sync with sensors
