package main

import (
	"context"
	"errors"
	"fmt"
	"io"
	"log"
	"net"
	"os"
	"time"

	"github.com/CyCoreSystems/audiosocket"
)

// maxCallDuration is the maximum amount of time to allow a call to be up before it is terminated.
const maxCallDuration = 2 * time.Minute

const listenAddr = ":8080"

// slinChunkSize is the number of bytes which should be sent per Slin
// audiosocket message.  Larger data will be chunked into this size for
// transmission of the AudioSocket.
//
// This is based on 8kHz, 20ms, 16-bit signed linear.
const slinChunkSize = audiosocket.DefaultSlinChunkSize // 8000Hz * 20ms * 2 bytes

var fileName string
var audioData []byte

func main() {
	var err error

	ctx := context.Background()

	// load the audio file data
	if fileName == "" {
		fileName = "test.slin"
	}
	audioData, err = os.ReadFile(fileName)
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
		return fmt.Errorf("failed to bind listener to socket %s: %w", listenAddr, err)
	}

	go func() {
		<-ctx.Done()
		l.Close()
	}()

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
	ctx, cancel := context.WithTimeout(pCtx, maxCallDuration)

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

	go processDataFromAsterisk(ctx, c)

	log.Println("sending audio")
	if err = sendAudio(ctx, c, audioData); err != nil {
		log.Println("failed to send audio to Asterisk:", err)
	}
	log.Println("completed audio send")
}

func processDataFromAsterisk(ctx context.Context, in io.Reader) {
	var err error
	var m audiosocket.Message

	for ctx.Err() == nil {
		m, err = audiosocket.NextMessage(in)
		if errors.Is(err, io.EOF) {
			log.Println("audiosocket closed")
			return
		}
		switch m.Kind() {
		case audiosocket.KindHangup:
			log.Println("audiosocket received hangup command")
			return
		case audiosocket.KindError:
			log.Println("error from audiosocket")
		case audiosocket.KindDTMF:
			log.Println("received DTMF: ", string(m.Payload()))
		case audiosocket.KindSlin:
			if m.ContentLength() < 1 {
				log.Println("no audio data")
			}
			// m.Payload() contains the received audio bytes
		default:
		}
	}
}

func sendAudio(ctx context.Context, w io.Writer, data []byte) error {
	var i, chunks int

	t := time.NewTicker(20 * time.Millisecond)
	defer t.Stop()

	for range t.C {
		select {
		case <-ctx.Done():
			return ctx.Err()
		default:
		}

		if i >= len(data) {
			return nil
		}

		var chunkLen = slinChunkSize
		if i+slinChunkSize > len(data) {
			chunkLen = len(data) - i
		}
		if _, err := w.Write(audiosocket.SlinMessage(data[i : i+chunkLen])); err != nil {
			return fmt.Errorf("failed to write chunk to audiosocket: %w", err)
		}
		chunks++
		i += chunkLen
	}

	return errors.New("ticker unexpectedly stopped")
}
