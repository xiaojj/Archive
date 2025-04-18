package fakeip

import (
	"errors"
	"net/netip"
	"strings"
	"sync"

	"github.com/MerlinKodo/clash-rev/common/nnip"
	"github.com/MerlinKodo/clash-rev/component/profile/cachefile"
	"github.com/MerlinKodo/clash-rev/component/trie"
)

const (
	offsetKey = "key-offset-fake-ip"
	cycleKey  = "key-cycle-fake-ip"
)

type store interface {
	GetByHost(host string) (netip.Addr, bool)
	PutByHost(host string, ip netip.Addr)
	GetByIP(ip netip.Addr) (string, bool)
	PutByIP(ip netip.Addr, host string)
	DelByIP(ip netip.Addr)
	Exist(ip netip.Addr) bool
	CloneTo(store)
	FlushFakeIP() error
}

// Pool is an implementation about fake ip generator without storage
type Pool struct {
	gateway netip.Addr
	first   netip.Addr
	last    netip.Addr
	offset  netip.Addr
	cycle   bool
	mux     sync.Mutex
	host    *trie.DomainTrie[struct{}]
	ipnet   netip.Prefix
	store   store
}

// Lookup return a fake ip with host
func (p *Pool) Lookup(host string) netip.Addr {
	p.mux.Lock()
	defer p.mux.Unlock()

	// RFC4343: DNS Case Insensitive, we SHOULD return result with all cases.
	host = strings.ToLower(host)
	if ip, exist := p.store.GetByHost(host); exist {
		return ip
	}

	ip := p.get(host)
	p.store.PutByHost(host, ip)
	return ip
}

// LookBack return host with the fake ip
func (p *Pool) LookBack(ip netip.Addr) (string, bool) {
	p.mux.Lock()
	defer p.mux.Unlock()

	return p.store.GetByIP(ip)
}

// ShouldSkipped return if domain should be skipped
func (p *Pool) ShouldSkipped(domain string) bool {
	if p.host == nil {
		return false
	}
	return p.host.Search(domain) != nil
}

// Exist returns if given ip exists in fake-ip pool
func (p *Pool) Exist(ip netip.Addr) bool {
	p.mux.Lock()
	defer p.mux.Unlock()

	return p.store.Exist(ip)
}

// Gateway return gateway ip
func (p *Pool) Gateway() netip.Addr {
	return p.gateway
}

// Broadcast return the last ip
func (p *Pool) Broadcast() netip.Addr {
	return p.last
}

// IPNet return raw ipnet
func (p *Pool) IPNet() netip.Prefix {
	return p.ipnet
}

// CloneFrom clone cache from old pool
func (p *Pool) CloneFrom(o *Pool) {
	o.store.CloneTo(p.store)
}

func (p *Pool) get(host string) netip.Addr {
	p.offset = p.offset.Next()

	if !p.offset.Less(p.last) {
		p.cycle = true
		p.offset = p.first
	}

	if p.cycle || p.store.Exist(p.offset) {
		p.store.DelByIP(p.offset)
	}

	p.store.PutByIP(p.offset, host)
	return p.offset
}

func (p *Pool) FlushFakeIP() error {
	err := p.store.FlushFakeIP()
	if err == nil {
		p.cycle = false
		p.offset = p.first.Prev()
	}
	return err
}

func (p *Pool) StoreState() {
	if s, ok := p.store.(*cachefileStore); ok {
		s.PutByHost(offsetKey, p.offset)
		if p.cycle {
			s.PutByHost(cycleKey, p.offset)
		}
	}
}

func (p *Pool) restoreState() {
	if s, ok := p.store.(*cachefileStore); ok {
		if _, exist := s.GetByHost(cycleKey); exist {
			p.cycle = true
		}

		if offset, exist := s.GetByHost(offsetKey); exist {
			if p.ipnet.Contains(offset) {
				p.offset = offset
			} else {
				_ = p.FlushFakeIP()
			}
		} else if s.Exist(p.first) {
			_ = p.FlushFakeIP()
		}
	}
}

type Options struct {
	IPNet netip.Prefix
	Host  *trie.DomainTrie[struct{}]

	// Size sets the maximum number of entries in memory
	// and does not work if Persistence is true
	Size int

	// Persistence will save the data to disk.
	// Size will not work and record will be fully stored.
	Persistence bool
}

// New return Pool instance
func New(options Options) (*Pool, error) {
	var (
		hostAddr = options.IPNet.Masked().Addr()
		gateway  = hostAddr.Next()
		first    = gateway.Next().Next().Next() // default start with 198.18.0.4
		last     = nnip.UnMasked(options.IPNet)
	)

	if !options.IPNet.IsValid() || !first.IsValid() || !first.Less(last) {
		return nil, errors.New("ipnet don't have valid ip")
	}

	pool := &Pool{
		gateway: gateway,
		first:   first,
		last:    last,
		offset:  first.Prev(),
		cycle:   false,
		host:    options.Host,
		ipnet:   options.IPNet,
	}
	if options.Persistence {
		pool.store = &cachefileStore{
			cache: cachefile.Cache(),
		}
	} else {
		pool.store = newMemoryStore(options.Size)
	}

	pool.restoreState()

	return pool, nil
}
