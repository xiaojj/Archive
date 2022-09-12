module github.com/v2rayA/v2rayA

go 1.16

require (
	github.com/adrg/xdg v0.4.0
	github.com/beevik/ntp v0.3.0
	github.com/devfeel/mapper v0.7.5
	github.com/dgrijalva/jwt-go/v4 v4.0.0-preview1
	github.com/gin-contrib/cors v1.3.1
	github.com/gin-gonic/gin v1.7.1
	github.com/golang/protobuf v1.5.2
	github.com/google/gopacket v1.1.19
	github.com/gorilla/websocket v1.5.0
	github.com/json-iterator/go v1.1.12
	github.com/matoous/go-nanoid v1.5.0
	github.com/mohae/deepcopy v0.0.0-20170929034955-c48cc78d4826
	github.com/muhammadmuzzammil1998/jsonc v0.0.0-20201229145248-615b0916ca38
	github.com/pkg/errors v0.9.1
	github.com/shadowsocks/go-shadowsocks2 v0.1.5
	github.com/shirou/gopsutil/v3 v3.21.11
	github.com/stevenroose/gonfig v0.1.5
	github.com/tidwall/gjson v1.10.2
	github.com/tidwall/sjson v1.2.3
	github.com/v2fly/v2ray-core/v5 v5.1.0
	github.com/v2rayA/RoutingA v1.0.2
	github.com/v2rayA/beego/v2 v2.0.7
	github.com/v2rayA/shadowsocksR v1.0.4
	github.com/v2rayA/v2ray-lib v0.0.0-20211227083129-d4f59fbf62b8
	github.com/v2rayA/v2rayA-lib4 v0.0.0-20220912152138-f38eb344419a
	github.com/vearutop/statigz v1.1.7
	go.etcd.io/bbolt v1.3.6
	golang.org/x/net v0.0.0-20220624214902-1bab6f366d9e
	golang.org/x/sys v0.0.0-20220520151302-bc2c85ada10a
	google.golang.org/grpc v1.48.0
)

// Replace dependency modules with local developing copy
// use `go list -m all` to confirm the final module used
//replace github.com/v2rayA/shadowsocksR => ../../shadowsocksR
//replace github.com/mzz2017/go-engine => ../../go-engine
//replace github.com/v2rayA/beego/v2 => D:\beego

replace go4.org/unsafe/assume-no-moving-gc => go4.org/unsafe/assume-no-moving-gc v0.0.0-20220617031537-928513b29760
