# AudiCDCEmulator
An Arduino-based CDC emulator that allows other audio sources to play on an Audi Concert head unit.

If you have a late 90's or early 2000's Audi with the factory stereo, chances you can't use it to play streaming music from your phone. Cassette adapters and FM transmitters are one solution, but they're cumbersome and typically have sub-optimal sound quality.

If your car came with a trunk-mounted CD changer (or you can use a VAG-COM tool to configure the head unit to work with one), you can use this project to enable the line-level inputs normally used by the CD changer. In my case I used a Bluetooth audio module to stream audio from my phone, but it can also work with signals from a headphone jack, etc. In addition enabling the inputs, certain key presses are also detected so that next and previous song functions can be triggered fro the head unit.





