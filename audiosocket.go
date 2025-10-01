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

	// KindSlin indicates that the message contains 16-bit, 8 kbit/s signed-linear audio data
	KindSlin = 0x10

	// KindSlin indicates that the message contains 16-bit, 12 kbit/s signed-linear audio data
	KindSlin12 = 0x11

	// KindSlin indicates that the message contains 16-bit, 16 kbit/s signed-linear audio data
	KindSlin16 = 0x12

	// KindSlin indicates that the message contains 16-bit, 24 kbit/s signed-linear audio data
	KindSlin24 = 0x13

	// KindSlin indicates that the message contains 16-bit, 32 kbit/s signed-linear audio data
	KindSlin32 = 0x14

	// KindSlin indicates that the message contains 16-bit, 44.1 kbit/s signed-linear audio data
	KindSlin44 = 0x15

	// KindSlin indicates that the message contains 16-bit, 48 kbit/s signed-linear audio data
	KindSlin48 = 0x16

	// KindSlin indicates that the message contains 16-bit, 96 kbit/s signed-linear audio data
	KindSlin96 = 0x17

	// KindSlin indicates that the message contains 16-bit, 192 kbit/s signed-linear audio data
	KindSlin192 = 0x18

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

// AudioFormat defines codec-specific parameters for audio transmission
type AudioFormat struct {
	Kind      Kind
	ChunkSize int
}

var (
	// FormatSlin represents 8kHz signed linear audio format
	FormatSlin = AudioFormat{
		Kind:      KindSlin,
		ChunkSize: 320, // 8000Hz * 20ms * 2 bytes
	}

	// FormatSlin12 represents 12kHz signed linear audio format
	FormatSlin12 = AudioFormat{
		Kind:      KindSlin12,
		ChunkSize: 480, // 12000Hz * 20ms * 2 bytes
	}

	// FormatSlin16 represents 16kHz signed linear audio format
	FormatSlin16 = AudioFormat{
		Kind:      KindSlin16,
		ChunkSize: 640, // 16000Hz * 20ms * 2 bytes
	}

	// FormatSlin24 represents 24kHz signed linear audio format
	FormatSlin24 = AudioFormat{
		Kind:      KindSlin24,
		ChunkSize: 960, // 24000Hz * 20ms * 2 bytes
	}

	// FormatSlin32 represents 32kHz signed linear audio format
	FormatSlin32 = AudioFormat{
		Kind:      KindSlin32,
		ChunkSize: 1280, // 32000Hz * 20ms * 2 bytes
	}

	// FormatSlin44 represents 44kHz signed linear audio format
	FormatSlin44 = AudioFormat{
		Kind:      KindSlin44,
		ChunkSize: 1764, // 44100Hz * 20ms * 2 bytes
	}

	// FormatSlin48 represents 48kHz signed linear audio format
	FormatSlin48 = AudioFormat{
		Kind:      KindSlin48,
		ChunkSize: 1920, // 48000Hz * 20ms * 2 bytes
	}

	// FormatSlin96 represents 96kHz signed linear audio format
	FormatSlin96 = AudioFormat{
		Kind:      KindSlin96,
		ChunkSize: 3840, // 96000Hz * 20ms * 2 bytes
	}

	// FormatSlin192 represents 192kHz signed linear audio format
	FormatSlin192 = AudioFormat{
		Kind:      KindSlin192,
		ChunkSize: 7680, // 192000Hz * 20ms * 2 bytes
	}
)

// AudioFormat returns the AudioFormat for this Kind
func (k Kind) AudioFormat() (AudioFormat, error) {
	switch k {
	case KindSlin:
		return FormatSlin, nil
	case KindSlin12:
		return FormatSlin12, nil
	case KindSlin16:
		return FormatSlin16, nil
	case KindSlin24:
		return FormatSlin24, nil
	case KindSlin32:
		return FormatSlin32, nil
	case KindSlin44:
		return FormatSlin44, nil
	case KindSlin48:
		return FormatSlin48, nil
	case KindSlin96:
		return FormatSlin96, nil
	case KindSlin192:
		return FormatSlin192, nil
	default:
		return AudioFormat{}, fmt.Errorf("unsupported audio format: %d", k)
	}
}

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

// AudioMessage creates a new Message from audio data with the specified kind
// If the input is larger than 65535 bytes, this function will panic.
func AudioMessage(in []byte, kind Kind) Message {
	if len(in) > 65535 {
		panic("audiosocket: message too large")
	}

	out := make([]byte, 3, 3+len(in))
	out[0] = byte(kind)
	binary.BigEndian.PutUint16(out[1:], uint16(len(in)))
	out = append(out, in...)
	return out
}
