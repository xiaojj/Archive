package anytls

import (
	"context"
	"crypto/sha256"
	"encoding/binary"
	"errors"
	"net"
	"strings"

	"github.com/metacubex/mihomo/adapter/inbound"
	"github.com/metacubex/mihomo/common/atomic"
	"github.com/metacubex/mihomo/common/buf"
	"github.com/metacubex/mihomo/component/ca"
	"github.com/metacubex/mihomo/component/ech"
	tlsC "github.com/metacubex/mihomo/component/tls"
	C "github.com/metacubex/mihomo/constant"
	LC "github.com/metacubex/mihomo/listener/config"
	"github.com/metacubex/mihomo/listener/sing"
	"github.com/metacubex/mihomo/transport/anytls/padding"
	"github.com/metacubex/mihomo/transport/anytls/session"

	"github.com/metacubex/sing/common/auth"
	"github.com/metacubex/sing/common/bufio"
	M "github.com/metacubex/sing/common/metadata"
)

type Listener struct {
	closed    bool
	config    LC.AnyTLSServer
	listeners []net.Listener
	tlsConfig *tlsC.Config
	userMap   map[[32]byte]string
	padding   atomic.TypedValue[*padding.PaddingFactory]
}

func New(config LC.AnyTLSServer, tunnel C.Tunnel, additions ...inbound.Addition) (sl *Listener, err error) {
	if len(additions) == 0 {
		additions = []inbound.Addition{
			inbound.WithInName("DEFAULT-ANYTLS"),
			inbound.WithSpecialRules(""),
		}
	}

	tlsConfig := &tlsC.Config{}
	if config.Certificate != "" && config.PrivateKey != "" {
		cert, err := ca.LoadTLSKeyPair(config.Certificate, config.PrivateKey, C.Path)
		if err != nil {
			return nil, err
		}
		tlsConfig.Certificates = []tlsC.Certificate{tlsC.UCertificate(cert)}

		if config.EchKey != "" {
			err = ech.LoadECHKey(config.EchKey, tlsConfig, C.Path)
			if err != nil {
				return nil, err
			}
		}
	}

	sl = &Listener{
		config:    config,
		tlsConfig: tlsConfig,
		userMap:   make(map[[32]byte]string),
	}

	for user, password := range config.Users {
		sl.userMap[sha256.Sum256([]byte(password))] = user
	}

	if len(config.PaddingScheme) > 0 {
		if !padding.UpdatePaddingScheme([]byte(config.PaddingScheme), &sl.padding) {
			return nil, errors.New("incorrect padding scheme format")
		}
	} else {
		padding.UpdatePaddingScheme(padding.DefaultPaddingScheme, &sl.padding)
	}

	// Using sing handler can automatically handle UoT
	h, err := sing.NewListenerHandler(sing.ListenerConfig{
		Tunnel:    tunnel,
		Type:      C.ANYTLS,
		Additions: additions,
	})
	if err != nil {
		return nil, err
	}

	for _, addr := range strings.Split(config.Listen, ",") {
		addr := addr

		//TCP
		l, err := inbound.Listen("tcp", addr)
		if err != nil {
			return nil, err
		}
		if len(tlsConfig.Certificates) > 0 {
			l = tlsC.NewListener(l, tlsConfig)
		} else {
			return nil, errors.New("disallow using AnyTLS without certificates config")
		}
		sl.listeners = append(sl.listeners, l)

		go func() {
			for {
				c, err := l.Accept()
				if err != nil {
					if sl.closed {
						break
					}
					continue
				}
				go sl.HandleConn(c, h)
			}
		}()
	}

	return sl, nil
}

func (l *Listener) Close() error {
	l.closed = true
	var retErr error
	for _, lis := range l.listeners {
		err := lis.Close()
		if err != nil {
			retErr = err
		}
	}
	return retErr
}

func (l *Listener) Config() string {
	return l.config.String()
}

func (l *Listener) AddrList() (addrList []net.Addr) {
	for _, lis := range l.listeners {
		addrList = append(addrList, lis.Addr())
	}
	return
}

func (l *Listener) HandleConn(conn net.Conn, h *sing.ListenerHandler) {
	ctx := context.TODO()
	defer conn.Close()

	b := buf.NewPacket()
	defer b.Release()

	_, err := b.ReadOnceFrom(conn)
	if err != nil {
		return
	}
	conn = bufio.NewCachedConn(conn, b)

	by, err := b.ReadBytes(32)
	if err != nil {
		return
	}
	var passwordSha256 [32]byte
	copy(passwordSha256[:], by)
	if user, ok := l.userMap[passwordSha256]; ok {
		ctx = auth.ContextWithUser(ctx, user)
	} else {
		return
	}
	by, err = b.ReadBytes(2)
	if err != nil {
		return
	}
	paddingLen := binary.BigEndian.Uint16(by)
	if paddingLen > 0 {
		_, err = b.ReadBytes(int(paddingLen))
		if err != nil {
			return
		}
	}

	session := session.NewServerSession(conn, func(stream *session.Stream) {
		defer stream.Close()

		destination, err := M.SocksaddrSerializer.ReadAddrPort(stream)
		if err != nil {
			return
		}

		// It seems that mihomo does not implement a connection error reporting mechanism, so we report success directly.
		err = stream.HandshakeSuccess()
		if err != nil {
			return
		}

		h.NewConnection(ctx, stream, M.Metadata{
			Source:      M.SocksaddrFromNet(conn.RemoteAddr()),
			Destination: destination,
		})
	}, &l.padding)
	session.Run()
	session.Close()
}
