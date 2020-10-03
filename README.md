# winter
Winter is coming

see https://github.com/mysteriumnetwork/winter-is-coming

# Building

Build with clang

Options -std=c++11

# Running

Any *nix will do as long as it has ifconfig

Usage:

  winter server [game-name]
  winter client [client-name]

This program will only work on the LAN

# Protocol

The maximum msg size is 2048.
The underlying protocol is UDP.

The msg layout is common across all ports.

[head] Six bytes, whose value MUST equal UTF-8[winter], required
[op] one byte, MUST be one of, required
  0x01 - ANNOUNCE
  0x05 - SHOOT
  0x06 - HIT
  0x07 - MISS
  0x09 - LOSE
[_game_name_size] one byte, the size of the game name
[game_name] A span of bytes, the name of the game this msg is about
[size] two bytes (short int), the size of the rest of the payload.
[payload] A span of bytes whose length is equal to [size]

0x05 - SHOOT
payload:
  [_name_size] two bytes (short int), the size of the name of the sender ((should just be one byte but oh well))
  [name] span of bytes, the name of the sender
  [x] one byte, the x coordinate
  [y] one byte, the y coordinate

0x06 - MISS
payload:
  MUST be exactly the same as the shoot msg that triggered this msg

0x07 - HIT
payload:
  MUST be exactly the same as the shoot msg that triggered this msg

0x09 - LOSE
payload:
  SHOULD not have any payload
