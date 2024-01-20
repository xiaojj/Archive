# Build ARM Cortex-A9

```
export EXTRA_FLAGS='target_cpu="arm" target_os="openwrt" arm_version=0 arm_cpu="cortex-a9" arm_float_abi="soft" arm_use_neon=false build_static=true no_madvise_syscall=true'
export OPENWRT_FLAGS='arch=arm_cortex-a9-static release=23.05.0 gcc_ver=12.3.0 target=bcm53xx subtarget=generic'
./get-clang.sh
./build.sh
```

# BUILD MIPSEL

```
export EXTRA_FLAGS='target_cpu="mipsel" target_os="openwrt" mips_arch_variant="r2" mips_float_abi="soft"'
export OPENWRT_FLAGS='arch=mipsel_24kc release=23.05.0 gcc_ver=12.3.0 target=ramips subtarget=rt305x'
./get-clang.sh
./build.sh
```

# BUILD MIPSEL static

```
export EXTRA_FLAGS='target_cpu="mipsel" target_os="openwrt" mips_arch_variant="r2" mips_float_abi="soft" build_static=true no_madvise_syscall=true'
export OPENWRT_FLAGS='arch=mipsel_24kc-static release=23.05.0 gcc_ver=12.3.0 target=ramips subtarget=rt305x'
./get-clang.sh
./build.sh
```

# QEMU ARM

See https://wiki.qemu.org/Documentation/Networking for example.

```
$ wget https://downloads.openwrt.org/releases/23.05.2/targets/armsr/armv7/openwrt-23.05.2-armsr-armv7-generic-initramfs-kernel.bin

$ qemu-system-arm -nographic -M virt -m 64 -kernel openwrt-23.05.2-armsr-armv7-generic-initramfs-kernel.bin -device virtio-net,netdev=net0 -netdev user,id=net0,hostfwd=tcp::5555-:1080
...

root@OpenWrt:/# ip link del br-lan
root@OpenWrt:/# ip addr add 10.0.2.15/24 dev eth0
root@OpenWrt:/# ip route add default via 10.0.2.2
root@OpenWrt:/# nft flush ruleset
root@OpenWrt:/# scp user@10.0.2.2:/tmp/naive .
root@OpenWrt:/# ./naive --listen=socks://0.0.0.0:1080 --proxy=https://user:pass@example.com --log
$ curl -v --proxy socks5h://127.0.0.1:5555 example.com
```


## To exit QEMU in -nographic:

Press Ctrl-A
Press X
