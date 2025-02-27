package audiosocket

import (
	"encoding/binary"
	"fmt"
	"io"

	"github.com/google/uuid"
)

// Message describes an audiosocket message/packet
type Message []byte

// Kind is a message type indicator
type Kind byte

const (
	// KindHangup indicates the message is a hangup signal
	KindHangup = 0x00

	// KindID indicates the message contains the unique identifier of the call
	KindID = 0x01

	// KindSilence indicates the presence of silence on the line
	KindSilence = 0x02

	// KindDTMF indicates the message contains DTMF data
	KindDTMF = 0x03

	// KindSlin indicates the message contains signed-linear audio data
	KindSlin = 0x10

	// KindError indicates the message contains an error code
	KindError = 0xff
)

// ErrorCode indicates an error, if present
type ErrorCode byte

const (
	// ErrNone indicates that no error is present
	ErrNone = 0x00

	// ErrAstHangup indicates that the call has hung up
	ErrAstHangup = 0x01

	// ErrAstFrameForwarding indicates that Asterisk had an error trying to forward an audio frame
	ErrAstFrameForwarding = 0x02

	// ErrAstMemory indicates that Asterisk had a memory/allocation erorr
	ErrAstMemory = 0x04

	// ErrUnknown indicates that the received error from Asterisk is unknown
	ErrUnknown = 0xff
)

// ContentLength returns the length of the payload of the message
func (m Message) ContentLength() uint16 {
	if len(m) < 3 {
		return 0
	}

	return binary.BigEndian.Uint16(m[1:3])
}

// Kind returns the type of the message
func (m Message) Kind() Kind {
	if len(m) < 1 {
		return KindError
	}
	return Kind(m[0])
}

// ErrorCode returns the coded error of the message, if present
func (m Message) ErrorCode() ErrorCode {
	if m.Kind() != KindError {
		return ErrNone
	}

	if len(m) < 4 {
		return ErrUnknown
	}

	return ErrorCode(m[3])
}

// Payload returns the data of the payload of the message
func (m Message) Payload() []byte {
	sz := m.ContentLength()
	if sz == 0 {
		return nil
	}

	return m[3:]
}

// ID returns the session's unique ID if and only if the Message is the initial
// ID message.  Normally, you would call GetID on the socket instead of
// manually running this function.
func (m Message) ID() (uuid.UUID, error) {
	if m.Kind() != KindID {
		return uuid.Nil, fmt.Errorf("wrong message type %d", m.Kind())
	}
	return uuid.FromBytes(m.Payload())
}

// GetID reads the unique ID from the first Message received.  This should only
// be called once per connection, and it must be called before anything else is
// read from the connection.
func GetID(r io.Reader) (uuid.UUID, error) {
	m, err := NextMessage(r)
	if err != nil {
		return uuid.Nil, fmt.Errorf("failed to read message: %w", err)
	}
	return m.ID()
}

// NextMessage reads and parses the next message from an audiosocket connection
func NextMessage(r io.Reader) (Message, error) {
	hdr := make([]byte, 3)

	_, err := io.ReadFull(r, hdr)
	if err != nil {
		return nil, fmt.Errorf("failed to read header: %w", err)
	}

	payloadLen := binary.BigEndian.Uint16(hdr[1:])
	if payloadLen < 1 {
		return hdr, nil
	}

	payload := make([]byte, payloadLen)
	_, err = io.ReadFull(r, payload)
	if err != nil {
		return nil, fmt.Errorf("failed to read payload: %w", err)
	}

	m := append(hdr, payload...)

	return m, nil
}

// MessageFromData parses an audiosocket message into a Message
func MessageFromData(in []byte) Message {
	return Message(in)
}

// HangupMessage creates a new Message indicating a hangup
func HangupMessage() Message {
	return []byte{KindHangup, 0x00, 0x00}
}

// IDMessage creates a new Message
func IDMessage(id uuid.UUID) Message {
	out := make([]byte, 3, 3+16)
	out[0] = KindID
	binary.BigEndian.PutUint16(out[1:], 16)
	return append(out, id[:]...)
}

// SlinMessage creates a new Message from signed linear audio data
// If the input is larger than 65535 bytes, this function will panic.
func SlinMessage(in []byte) Message {
	if len(in) > 65535 {
		panic("audiosocket: message too large")
	}

	out := make([]byte, 3, 3+len(in))
	out[0] = KindSlin
	binary.BigEndian.PutUint16(out[1:], uint16(len(in)))
	out = append(out, in...)
	return out
}
