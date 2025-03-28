package sing_shadowsocks

import (
	"context"
	"fmt"
	"net"
	"strings"

	"github.com/MerlinKodo/clash-rev/adapter/inbound"
	N "github.com/MerlinKodo/clash-rev/common/net"
	"github.com/MerlinKodo/clash-rev/common/sockopt"
	C "github.com/MerlinKodo/clash-rev/constant"
	LC "github.com/MerlinKodo/clash-rev/listener/config"
	embedSS "github.com/MerlinKodo/clash-rev/listener/shadowsocks"
	"github.com/MerlinKodo/clash-rev/listener/sing"
	"github.com/MerlinKodo/clash-rev/log"
	"github.com/MerlinKodo/clash-rev/ntp"

	shadowsocks "github.com/MerlinKodo/sing-shadowsocks"
	"github.com/MerlinKodo/sing-shadowsocks/shadowaead"
	"github.com/MerlinKodo/sing-shadowsocks/shadowaead_2022"
	"github.com/sagernet/sing/common"
	"github.com/sagernet/sing/common/buf"
	"github.com/sagernet/sing/common/bufio"
	M "github.com/sagernet/sing/common/metadata"
)

type Listener struct {
	closed       bool
	config       LC.ShadowsocksServer
	listeners    []net.Listener
	udpListeners []net.PacketConn
	service      shadowsocks.Service
}

var _listener *Listener

func New(config LC.ShadowsocksServer, tunnel C.Tunnel, additions ...inbound.Addition) (C.MultiAddrListener, error) {
	var sl *Listener
	var err error
	if len(additions) == 0 {
		additions = []inbound.Addition{
			inbound.WithInName("DEFAULT-SHADOWSOCKS"),
			inbound.WithSpecialRules(""),
		}
		defer func() {
			_listener = sl
		}()
	}

	udpTimeout := int64(sing.UDPTimeout.Seconds())

	h := &sing.ListenerHandler{
		Tunnel:    tunnel,
		Type:      C.SHADOWSOCKS,
		Additions: additions,
	}

	sl = &Listener{false, config, nil, nil, nil}

	switch {
	case config.Cipher == shadowsocks.MethodNone:
		sl.service = shadowsocks.NewNoneService(udpTimeout, h)
	case common.Contains(shadowaead.List, config.Cipher):
		sl.service, err = shadowaead.NewService(config.Cipher, nil, config.Password, udpTimeout, h)
	case common.Contains(shadowaead_2022.List, config.Cipher):
		sl.service, err = shadowaead_2022.NewServiceWithPassword(config.Cipher, config.Password, udpTimeout, h, ntp.Now)
	default:
		err = fmt.Errorf("shadowsocks: unsupported method: %s", config.Cipher)
		return embedSS.New(config, tunnel)
	}
	if err != nil {
		return nil, err
	}

	for _, addr := range strings.Split(config.Listen, ",") {
		addr := addr

		if config.Udp {
			//UDP
			ul, err := net.ListenPacket("udp", addr)
			if err != nil {
				return nil, err
			}

			err = sockopt.UDPReuseaddr(ul.(*net.UDPConn))
			if err != nil {
				log.Warnln("Failed to Reuse UDP Address: %s", err)
			}

			sl.udpListeners = append(sl.udpListeners, ul)

			go func() {
				conn := bufio.NewPacketConn(ul)
				var buff *buf.Buffer
				newBuffer := func() *buf.Buffer {
					buff = buf.NewPacket() // do not use stack buffer
					return buff
				}
				readWaiter, isReadWaiter := bufio.CreatePacketReadWaiter(conn)
				if isReadWaiter {
					readWaiter.InitializeReadWaiter(newBuffer)
				}
				for {
					var (
						dest M.Socksaddr
						err  error
					)
					buff = nil // clear last loop status, avoid repeat release
					if isReadWaiter {
						dest, err = readWaiter.WaitReadPacket()
					} else {
						dest, err = conn.ReadPacket(newBuffer())
					}
					if err != nil {
						if buff != nil {
							buff.Release()
						}
						if sl.closed {
							break
						}
						continue
					}
					_ = sl.service.NewPacket(context.TODO(), conn, buff, M.Metadata{
						Protocol: "shadowsocks",
						Source:   dest,
					})
				}
			}()
		}

		//TCP
		l, err := inbound.Listen("tcp", addr)
		if err != nil {
			return nil, err
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
				N.TCPKeepAlive(c)

				go sl.HandleConn(c, tunnel)
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
	for _, lis := range l.udpListeners {
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
	for _, lis := range l.udpListeners {
		addrList = append(addrList, lis.LocalAddr())
	}
	return
}

func (l *Listener) HandleConn(conn net.Conn, tunnel C.Tunnel, additions ...inbound.Addition) {
	ctx := sing.WithAdditions(context.TODO(), additions...)
	err := l.service.NewConnection(ctx, conn, M.Metadata{
		Protocol: "shadowsocks",
		Source:   M.ParseSocksaddr(conn.RemoteAddr().String()),
	})
	if err != nil {
		_ = conn.Close()
		return
	}
}

func HandleShadowSocks(conn net.Conn, tunnel C.Tunnel, additions ...inbound.Addition) bool {
	if _listener != nil && _listener.service != nil {
		go _listener.HandleConn(conn, tunnel, additions...)
		return true
	}
	return embedSS.HandleShadowSocks(conn, tunnel, additions...)
}
