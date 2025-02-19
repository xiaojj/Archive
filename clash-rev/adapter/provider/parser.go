package provider

import (
	"errors"
	"fmt"
	"time"

	"github.com/MerlinKodo/clash-rev/common/structure"
	"github.com/MerlinKodo/clash-rev/common/utils"
	"github.com/MerlinKodo/clash-rev/component/resource"
	C "github.com/MerlinKodo/clash-rev/constant"
	types "github.com/MerlinKodo/clash-rev/constant/provider"
)

var (
	errVehicleType = errors.New("unsupport vehicle type")
	errSubPath     = errors.New("path is not subpath of home directory")
)

type healthCheckSchema struct {
	Enable         bool   `provider:"enable"`
	URL            string `provider:"url"`
	Interval       int    `provider:"interval"`
	Lazy           bool   `provider:"lazy,omitempty"`
	ExpectedStatus string `provider:"expected-status,omitempty"`
}

type proxyProviderSchema struct {
	Type          string            `provider:"type"`
	Path          string            `provider:"path,omitempty"`
	URL           string            `provider:"url,omitempty"`
	Interval      int               `provider:"interval,omitempty"`
	Filter        string            `provider:"filter,omitempty"`
	ExcludeFilter string            `provider:"exclude-filter,omitempty"`
	ExcludeType   string            `provider:"exclude-type,omitempty"`
	DialerProxy   string            `provider:"dialer-proxy,omitempty"`
	HealthCheck   healthCheckSchema `provider:"health-check,omitempty"`
}

func ParseProxyProvider(name string, mapping map[string]any) (types.ProxyProvider, error) {
	decoder := structure.NewDecoder(structure.Option{TagName: "provider", WeaklyTypedInput: true})

	schema := &proxyProviderSchema{
		HealthCheck: healthCheckSchema{
			Lazy: true,
		},
	}
	if err := decoder.Decode(mapping, schema); err != nil {
		return nil, err
	}

	expectedStatus, err := utils.NewIntRanges[uint16](schema.HealthCheck.ExpectedStatus)
	if err != nil {
		return nil, err
	}

	var hcInterval uint
	if schema.HealthCheck.Enable {
		hcInterval = uint(schema.HealthCheck.Interval)
	}
	hc := NewHealthCheck([]C.Proxy{}, schema.HealthCheck.URL, hcInterval, schema.HealthCheck.Lazy, expectedStatus)

	var vehicle types.Vehicle
	switch schema.Type {
	case "file":
		path := C.Path.Resolve(schema.Path)
		vehicle = resource.NewFileVehicle(path)
	case "http":
		if schema.Path != "" {
			path := C.Path.Resolve(schema.Path)
			if !C.Path.IsSafePath(path) {
				return nil, fmt.Errorf("%w: %s", errSubPath, path)
			}
			vehicle = resource.NewHTTPVehicle(schema.URL, path)
		} else {
			path := C.Path.GetPathByHash("proxies", schema.URL)
			vehicle = resource.NewHTTPVehicle(schema.URL, path)
		}
	default:
		return nil, fmt.Errorf("%w: %s", errVehicleType, schema.Type)
	}

	interval := time.Duration(uint(schema.Interval)) * time.Second
	filter := schema.Filter
	excludeFilter := schema.ExcludeFilter
	excludeType := schema.ExcludeType
	dialerProxy := schema.DialerProxy

	return NewProxySetProvider(name, interval, filter, excludeFilter, excludeType, dialerProxy, vehicle, hc)
}
