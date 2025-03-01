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
	var chunks int

	if chunkSize < 1 {
		chunkSize = DefaultSlinChunkSize
	}

	t := time.NewTicker(20 * time.Millisecond)
	defer t.Stop()

	for i := 0; i < len(input); {
		<-t.C
		chunkLen := chunkSize
		if i+chunkSize > len(input) {
			chunkLen = len(input) - i
		}
		if _, err := w.Write(SlinMessage(input[i : i+chunkLen])); err != nil {
			return fmt.Errorf("failted to write chunk to AudioSocket: %w", err)
		}
		chunks++
		i += chunkLen
	}

	return nil
}
