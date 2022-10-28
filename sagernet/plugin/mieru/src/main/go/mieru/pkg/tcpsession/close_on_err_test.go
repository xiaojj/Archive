// Copyright (C) 2022  mieru authors
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

package tcpsession

import (
	"context"
	mrand "math/rand"
	"net"
	"testing"
	"time"

	"github.com/enfein/mieru/pkg/appctl/appctlpb"
	"github.com/enfein/mieru/pkg/metrics"
	"github.com/enfein/mieru/pkg/rng"
	"github.com/enfein/mieru/pkg/testtool"
	"google.golang.org/protobuf/proto"
)

func TestCloseOnErr(t *testing.T) {
	serverAddr := "127.0.0.1:12359"
	clientAddr := "127.0.0.1:12360"
	clientTCPAddr, _ := net.ResolveTCPAddr("tcp", clientAddr)
	users := map[string]*appctlpb.User{
		"erbaijin": {
			Name:     proto.String("erbaijin"),
			Password: proto.String("buhuanjian"),
		},
	}

	server, err := ListenWithOptions(serverAddr, users)
	if err != nil {
		t.Fatalf("ListenWithOptions() failed: %v", err)
	}
	go func() {
		for {
			s, err := server.Accept()
			if err != nil {
				return
			} else {
				t.Logf("[%s] accepting new connection from %v", time.Now().Format(testtool.TimeLayout), s.RemoteAddr())
				go func() {
					if err = testtool.TestHelperServeConn(s); err != nil {
						return
					}
				}()
			}
		}
	}()
	time.Sleep(1 * time.Second)

	// Establish a TCP connection to server.
	dialer := net.Dialer{
		LocalAddr: clientTCPAddr,
	}
	conn, err := dialer.DialContext(context.TODO(), "tcp", serverAddr)
	if err != nil {
		t.Fatalf("DialContext() failed: %v", err)
	}
	defer conn.Close()

	// Get the current TCP error counter.
	errCnt := metrics.TCPReceiveErrors
	t.Logf("metrics.TCPReceiveErrors value before client write: %d", metrics.TCPReceiveErrors)

	// Send a very small message. This shouldn't trigger TCP read error in server.
	data := testtool.TestHelperGenRot13Input(12)
	if _, err := conn.Write(data); err != nil {
		t.Fatalf("Write() failed: %v", err)
	}
	time.Sleep(1 * time.Second)
	t.Logf("metrics.TCPReceiveErrors value after 1 client write: %d", metrics.TCPReceiveErrors)
	if metrics.TCPReceiveErrors > errCnt {
		t.Errorf("metrics.TCPReceiveErrors value unexpectly increased")
	}

	// Send a second small message. Combined with the first message it is larger than decryption unit.
	// This should trigger TCP read error in server.
	data = testtool.TestHelperGenRot13Input(48)
	if _, err := conn.Write(data); err != nil {
		t.Fatalf("Write() failed: %v", err)
	}
	time.Sleep(1 * time.Second)
	t.Logf("metrics.TCPReceiveErrors value after 2 client writes: %d", metrics.TCPReceiveErrors)
	if metrics.TCPReceiveErrors <= errCnt {
		t.Errorf("metrics.TCPReceiveErrors value is not increased")
	}

	// Send a larger message for server to drain after error.
	data = testtool.TestHelperGenRot13Input(65536)
	if _, err := conn.Write(data); err != nil {
		t.Fatalf("Write() failed: %v", err)
	}
	time.Sleep(1 * time.Second)
	errCnt = metrics.TCPReceiveErrors

	// Verify the connection has been closed by server.
	data = testtool.TestHelperGenRot13Input(256)
	_, err = conn.Write(data)
	if err == nil {
		t.Fatalf("unexpected successful Write()")
	}
	time.Sleep(1 * time.Second)
	t.Logf("metrics.TCPReceiveErrors value after 4 client writes: %d", metrics.TCPReceiveErrors)
	if metrics.TCPReceiveErrors > errCnt {
		// The number of error should not increase because the conn is already closed by server.
		t.Errorf("metrics.TCPReceiveErrors value is increased unexpectly")
	}

	server.Close()
	time.Sleep(1 * time.Second) // Wait for resources to be released.
}

func TestErrorMetrics(t *testing.T) {
	serverAddr := "127.0.0.1:12361"
	clientAddr := "127.0.0.1:12362"
	clientTCPAddr, _ := net.ResolveTCPAddr("tcp", clientAddr)
	users := map[string]*appctlpb.User{
		"erbaijin": {
			Name:     proto.String("erbaijin"),
			Password: proto.String("buhuanjian"),
		},
	}

	server, err := ListenWithOptions(serverAddr, users)
	if err != nil {
		t.Fatalf("ListenWithOptions() failed: %v", err)
	}
	go func() {
		for {
			s, err := server.Accept()
			if err != nil {
				return
			} else {
				t.Logf("[%s] accepting new connection from %v", time.Now().Format(testtool.TimeLayout), s.RemoteAddr())
				go func() {
					if err = testtool.TestHelperServeConn(s); err != nil {
						return
					}
				}()
			}
		}
	}()
	time.Sleep(1 * time.Second)

	// Establish a TCP connection to server.
	dialer := net.Dialer{
		LocalAddr: clientTCPAddr,
	}
	conn, err := dialer.DialContext(context.TODO(), "tcp", serverAddr)
	if err != nil {
		t.Fatalf("DialContext() failed: %v", err)
	}
	defer conn.Close()

	// Get the current TCP error counter.
	errCnt := metrics.TCPReceiveErrors
	t.Logf("metrics.TCPReceiveErrors value before client write: %d", metrics.TCPReceiveErrors)

	for i := 0; i < 50; i++ {
		sleepMillis := 50 + mrand.Intn(50)
		time.Sleep(time.Duration(sleepMillis) * time.Millisecond)
		data := testtool.TestHelperGenRot13Input(rng.IntRange(256, 65536))

		// Send data to server. The connection should be closed by the server after reading some data.
		if _, err = conn.Write(data); err != nil {
			t.Logf("Write() failed after %d attempts", i+1)
			break
		}
	}
	time.Sleep(1 * time.Second)

	// The TCP error counter should increase.
	t.Logf("metrics.TCPReceiveErrors value after client writes: %d", metrics.TCPReceiveErrors)
	if metrics.TCPReceiveErrors < errCnt+1 {
		t.Errorf("got %d TCPReceiveErrors, want at least %d", metrics.TCPReceiveErrors, errCnt+1)
	}

	server.Close()
	time.Sleep(1 * time.Second) // Wait for resources to be released.
}
