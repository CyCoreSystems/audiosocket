# AudioSocket

AudioSocket is a simple TCP-based protocol for sending and receiving realtime
audio streams.

There exists a protocol definition (below), a Go library, and an Asterisk
application.

## Protocol definition

The singular design goal of AudioSocket is to present the simplest possible
audio streaming protocol, initially based on the constraints of Asterisk audio.
Each packet contains a three-byte header and a variable payload.  The header is
composed of a one-byte type and a two-byte length indicator.

The minimum message length is three bytes:  type and payload-length.  Hangup
indication, for instance, is `0x00 0x00 0x00`.

### Types

  - `0x00` - Terminate the connection (socket closure is also sufficient)
  - `0x01` - Payload will contain the UUID (16-byte binary representation) for the audio stream
  - `0x10` - Payload is signed linear, 16-bit, 8kHz, mono PCM (little-endian)
  - `0xff` - An error has occurred; payload is the (optional)
    application-specific error code.  Asterisk-generated error codes are listed
    below.

### Payload length

The payload length is a 16-bit unsigned integer (big endian) indicating how many bytes are
in the payload.

### Payload

The content of the payload is defined by the header: type and length.

### Asterisk error codes

Error codes are application-specific.  The error codes for Asterisk are
single-byte, bit-packed error codes:

  - `0x01` - hangup of calling party
  - `0x02` - frame forwarding error
  - `0x04` - memory (allocation) error

