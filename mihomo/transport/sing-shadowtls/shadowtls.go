package sing_shadowtls

import (
	"context"
	"crypto/tls"
	"net"

	"github.com/metacubex/mihomo/component/ca"
	tlsC "github.com/metacubex/mihomo/component/tls"
	"github.com/metacubex/mihomo/log"

	"github.com/metacubex/sing-shadowtls"
	utls "github.com/metacubex/utls"
	sing_common "github.com/sagernet/sing/common"
)

const (
	Mode string = "shadow-tls"
)

var (
	DefaultALPN = []string{"h2", "http/1.1"}
)

type ShadowTLSOption struct {
	Password          string
	Host              string
	Fingerprint       string
	ClientFingerprint string
	SkipCertVerify    bool
	Version           int
}

func NewShadowTLS(ctx context.Context, conn net.Conn, option *ShadowTLSOption) (net.Conn, error) {
	tlsConfig := &tls.Config{
		NextProtos:         DefaultALPN,
		MinVersion:         tls.VersionTLS12,
		InsecureSkipVerify: option.SkipCertVerify,
		ServerName:         option.Host,
	}

	var err error
	tlsConfig, err = ca.GetSpecifiedFingerprintTLSConfig(tlsConfig, option.Fingerprint)
	if err != nil {
		return nil, err
	}

	tlsHandshake := uTLSHandshakeFunc(tlsConfig, option.ClientFingerprint)
	client, err := shadowtls.NewClient(shadowtls.ClientConfig{
		Version:      option.Version,
		Password:     option.Password,
		TLSHandshake: tlsHandshake,
		Logger:       log.SingLogger,
	})
	if err != nil {
		return nil, err
	}
	return client.DialContextConn(ctx, conn)
}

func uTLSHandshakeFunc(config *tls.Config, clientFingerprint string) shadowtls.TLSHandshakeFunc {
	return func(ctx context.Context, conn net.Conn, sessionIDGenerator shadowtls.TLSSessionIDGeneratorFunc) error {
		tlsConfig := &utls.Config{
			Rand:                  config.Rand,
			Time:                  config.Time,
			VerifyPeerCertificate: config.VerifyPeerCertificate,
			RootCAs:               config.RootCAs,
			NextProtos:            config.NextProtos,
			ServerName:            config.ServerName,
			InsecureSkipVerify:    config.InsecureSkipVerify,
			CipherSuites:          config.CipherSuites,
			MinVersion:            config.MinVersion,
			MaxVersion:            config.MaxVersion,
			CurvePreferences: sing_common.Map(config.CurvePreferences, func(it tls.CurveID) utls.CurveID {
				return utls.CurveID(it)
			}),
			SessionTicketsDisabled: config.SessionTicketsDisabled,
			Renegotiation:          utls.RenegotiationSupport(config.Renegotiation),
			SessionIDGenerator:     sessionIDGenerator,
		}
		clientFingerprint := clientFingerprint
		if tlsC.HaveGlobalFingerprint() && len(clientFingerprint) == 0 {
			clientFingerprint = tlsC.GetGlobalFingerprint()
		}
		if len(clientFingerprint) != 0 {
			if fingerprint, exists := tlsC.GetFingerprint(clientFingerprint); exists {
				clientHelloID := *fingerprint.ClientHelloID
				tlsConn := utls.UClient(conn, tlsConfig, clientHelloID)
				return tlsConn.HandshakeContext(ctx)
			}
		}
		tlsConn := utls.Client(conn, tlsConfig)
		return tlsConn.HandshakeContext(ctx)
	}
}
