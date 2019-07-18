package main

import (
	"context"
	"io"
	"io/ioutil"
	"log"
	"net"
	"time"

	"github.com/CyCoreSystems/audiosocket"
	"github.com/gofrs/uuid"
	"github.com/pkg/errors"
)

// MaxCallDuration is the maximum amount of time to allow a call to be up before it is terminated.
const MaxCallDuration = 2 * time.Minute

const listenAddr = ":8080"
const languageCode = "en-US"

// slinChunkSize is the number of bytes which should be sent per Slin
// audiosocket message.  Larger data will be chunked into this size for
// transmission of the AudioSocket.
//
// This is based on 8kHz, 20ms, 16-bit signed linear.
const slinChunkSize = 320 // 8000Hz * 20ms * 2 bytes

var fileName string

var audioData []byte

func init() {
}

// ErrHangup indicates that the call should be terminated or has been terminated
var ErrHangup = errors.New("Hangup")

func main() {
	var err error

	ctx := context.Background()

	// load the audio file data
	if fileName == "" {
		fileName = "test.slin"
	}
	audioData, err = ioutil.ReadFile(fileName)
	if err != nil {
		log.Fatalln("failed to read audio file:", err)
	}

	log.Println("listening for AudioSocket connections on", listenAddr)
	if err = Listen(ctx); err != nil {
		log.Fatalln("listen failure:", err)
	}
	log.Println("exiting")
}

// Listen listens for and responds to AudioSocket connections
func Listen(ctx context.Context) error {
	l, err := net.Listen("tcp", listenAddr)
	if err != nil {
		return errors.Wrapf(err, "failed to bind listener to socket %s", listenAddr)
	}

	for {
		conn, err := l.Accept()
		if err != nil {
			log.Println("failed to accept new connection:", err)
			continue
		}

		go Handle(ctx, conn)
	}
}

// Handle processes a call
func Handle(pCtx context.Context, c net.Conn) {
	ctx, cancel := context.WithTimeout(pCtx, MaxCallDuration)

	defer func() {
		cancel()

		if _, err := c.Write(audiosocket.HangupMessage()); err != nil {
			log.Println("failed to send hangup message:", err)
		}

	}()

	id, err := audiosocket.GetID(c)
	if err != nil {
		log.Println("failed to get call ID:", err)
		return
	}
	log.Printf("processing call %s", id.String())

	go processDataFromAsterisk(ctx, cancel, c)

	log.Println("sending audio")
	if err = sendAudio(c, audioData); err != nil {
		log.Println("failed to send audio to Asterisk:", err)
	}
	log.Println("completed audio send")
	return
}

func getCallID(c net.Conn) (uuid.UUID, error) {
	m, err := audiosocket.NextMessage(c)
	if err != nil {
		return uuid.Nil, err
	}

	if m.Kind() != audiosocket.KindID {
		return uuid.Nil, errors.Errorf("invalid message type %d getting CallID", m.Kind())
	}

	return uuid.FromBytes(m.Payload())
}

func processDataFromAsterisk(ctx context.Context, cancel context.CancelFunc, in io.Reader) {
	var err error
	var m audiosocket.Message

	defer cancel()

	for ctx.Err() == nil {
		m, err = audiosocket.NextMessage(in)
		if errors.Cause(err) == io.EOF {
			log.Println("audiosocket closed")
			return
		}
		switch m.Kind() {
		case audiosocket.KindHangup:
			log.Println("audiosocket received hangup command")
			return
		case audiosocket.KindError:
			log.Println("error from audiosocket")
		case audiosocket.KindSlin:
			if m.ContentLength() < 1 {
				log.Println("no audio data")
			}
		default:
		}
	}
}

func sendAudio(w io.Writer, data []byte) error {

	var i, chunks int

	t := time.NewTicker(20 * time.Millisecond)
	defer t.Stop()

	for range t.C {
		if i >= len(data) {
			return nil
		}

		var chunkLen = slinChunkSize
		if i+slinChunkSize > len(data) {
			chunkLen = len(data) - i
		}
		if _, err := w.Write(audiosocket.SlinMessage(data[i : i+chunkLen])); err != nil {
			return errors.Wrap(err, "failed to write chunk to audiosocket")
		}
		chunks++
		i += chunkLen

	}
	return errors.New("ticker unexpectedly stopped")
}
