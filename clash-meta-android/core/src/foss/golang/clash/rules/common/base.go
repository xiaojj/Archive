package common

import (
	"errors"

	C "github.com/metacubex/mihomo/constant"

	"golang.org/x/exp/slices"
)

var (
	errPayload = errors.New("payloadRule error")
)

// params
var (
	NoResolve = "no-resolve"
	Src       = "src"
)

type Base struct {
}

func (b *Base) ProviderNames() []string { return nil }

func ParseParams(params []string) (isSrc bool, noResolve bool) {
	isSrc = slices.Contains(params, Src)
	if isSrc {
		noResolve = true
	} else {
		noResolve = slices.Contains(params, NoResolve)
	}
	return
}

type ParseRuleFunc func(tp, payload, target string, params []string, subRules map[string][]C.Rule) (C.Rule, error)
