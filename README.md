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

# Notes

There is almost no concept of security here and it's very easy to make the client and server crash, read out of bounds and generally misbehave.


The program is agnostic of your ip address. We don't even need to record where msgs are coming from.


This could be developed to allow players from different networks to play with each other.
  - It would need a well-known server
  - a client on one net registers with server
  - a client on other net registers with server
  - they're friends so they swap exchange public ips using server
  - they msg each-other a few times, ignoring the content
  - one net is host net, the other is client net
  - all traffic of this program from client net is then sent to host net
  - all traffic from host net that didn't originate from client net is sent to client net

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
