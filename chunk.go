package audiosocket

import (
	"fmt"
	"io"
	"time"
)

// DefaultSlinChunkSize is the number of bytes which should be sent per slin
// AudioSocket message.  Larger data will be chunked into this size for
// transmission of the AudioSocket.
const DefaultSlinChunkSize = 320 // 8000Hz * 20ms * 2 bytes

// SendSlinChunks takes signed linear data and sends it over an AudioSocket connection in chunks of the given size.
func SendSlinChunks(w io.Writer, chunkSize int, input []byte) error {
	return SendAudioChunks(
		w, AudioFormat{
			Kind:      KindSlin,
			ChunkSize: chunkSize,
		}, input)
}

// SendAudioChunks takes audio data and sends it over an AudioSocket connection using the specified format
func SendAudioChunks(w io.Writer, format AudioFormat, input []byte) error {
	var chunks int

	t := time.NewTicker(20 * time.Millisecond)
	defer t.Stop()

	for i := 0; i < len(input); {
		<-t.C
		chunkLen := format.ChunkSize
		if i+format.ChunkSize > len(input) {
			chunkLen = len(input) - i
		}
		if _, err := w.Write(AudioMessage(input[i:i+chunkLen], format.Kind)); err != nil {
			return fmt.Errorf("failed to write chunk to AudioSocket: %w", err)
		}
		chunks++
		i += chunkLen
	}

	return nil
}
