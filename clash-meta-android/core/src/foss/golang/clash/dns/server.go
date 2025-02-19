package dns

import (
	stdContext "context"
	"errors"
	"net"

	"github.com/metacubex/mihomo/adapter/inbound"
	"github.com/metacubex/mihomo/common/sockopt"
	"github.com/metacubex/mihomo/context"
	"github.com/metacubex/mihomo/log"

	D "github.com/miekg/dns"
)

var (
	address string
	server  = &Server{}

	dnsDefaultTTL uint32 = 600
)

type Server struct {
	handler   handler
	tcpServer *D.Server
	udpServer *D.Server
}

// ServeDNS implement D.Handler ServeDNS
func (s *Server) ServeDNS(w D.ResponseWriter, r *D.Msg) {
	msg, err := handlerWithContext(stdContext.Background(), s.handler, r)
	if err != nil {
		D.HandleFailed(w, r)
		return
	}
	msg.Compress = true
	w.WriteMsg(msg)
}

func handlerWithContext(stdCtx stdContext.Context, handler handler, msg *D.Msg) (*D.Msg, error) {
	if len(msg.Question) == 0 {
		return nil, errors.New("at least one question is required")
	}

	ctx := context.NewDNSContext(stdCtx, msg)
	return handler(ctx, msg)
}

func (s *Server) SetHandler(handler handler) {
	s.handler = handler
}

func ReCreateServer(addr string, resolver *Resolver, mapper *ResolverEnhancer) {
	if addr == address && resolver != nil {
		handler := NewHandler(resolver, mapper)
		server.SetHandler(handler)
		return
	}

	if server.tcpServer != nil {
		_ = server.tcpServer.Shutdown()
		server.tcpServer = nil
	}

	if server.udpServer != nil {
		_ = server.udpServer.Shutdown()
		server.udpServer = nil
	}

	server.handler = nil
	address = ""

	if addr == "" {
		return
	}

	var err error
	defer func() {
		if err != nil {
			log.Errorln("Start DNS server error: %s", err.Error())
		}
	}()

	_, port, err := net.SplitHostPort(addr)
	if port == "0" || port == "" || err != nil {
		return
	}

	address = addr
	handler := NewHandler(resolver, mapper)
	server = &Server{handler: handler}

	go func() {
		p, err := inbound.ListenPacket("udp", addr)
		if err != nil {
			log.Errorln("Start DNS server(UDP) error: %s", err.Error())
			return
		}

		if err := sockopt.UDPReuseaddr(p); err != nil {
			log.Warnln("Failed to Reuse UDP Address: %s", err)
		}

		log.Infoln("DNS server(UDP) listening at: %s", p.LocalAddr().String())
		server.udpServer = &D.Server{Addr: addr, PacketConn: p, Handler: server}
		_ = server.udpServer.ActivateAndServe()
	}()

	go func() {
		l, err := inbound.Listen("tcp", addr)
		if err != nil {
			log.Errorln("Start DNS server(TCP) error: %s", err.Error())
			return
		}

		log.Infoln("DNS server(TCP) listening at: %s", l.Addr().String())
		server.tcpServer = &D.Server{Addr: addr, Listener: l, Handler: server}
		_ = server.tcpServer.ActivateAndServe()
	}()

}
