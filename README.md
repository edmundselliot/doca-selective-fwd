# DOCA sample app
Sample application showing a simple forwarding application, which allows users to explicitly allow/deny flows.

![selective-fwd-diagram](./doc/selective_fwd_diagram.png)

Traffic can take one of three paths:
1. RED: Path for a packet without an offloaded flow, which is then denied by the PMD.
2. ORANGE: Path for a packet without an offloaded flow, which is then allowed by the PMD. The next packet for this flow will take the GREEN path.
3. GREEN: Path for a packet with an offloaded flow, entirely in hardware.

## Prerequisites
* DOCA: `2.7.0085`
* DPDK: `22.11.2404.0.11`

Make sure they are in pkg-config path `pkg-config --modversion libdpdk doca`.

## Building
```
meson build
ninja -C build
```

## Sample init
```
./build/doca-sample -m0xf -a26:00.3,dv_flow_en=2,dv_xmeta_en=4 -a26:00.5,dv_flow_en=2,dv_xmeta_en=4
```

## Running
Users can selectively offload hairpin flows for traffic which is received.
```
[06:38:23:967506][398293][DOCA][INF][pipes.c:291][run_app] Setup done. Listening for packets
[P0 Q2] Offload 60.0.0.1:1238 -> 60.0.0.3:80? (y/n): y
[06:38:56:092519][398293][DOCA][INF][pipes.c:327][run_app] Flow offloaded
[P0 Q44] Offload 60.0.0.1:4000 -> 60.0.0.2:4001? (y/n): n
[06:39:21:403840][398293][DOCA][INF][pipes.c:331][run_app] Flow will not be offloaded
[P0 Q44] Offload 60.0.0.1:4000 -> 60.0.0.2:4001? (y/n): n
[06:39:28:982206][398293][DOCA][INF][pipes.c:331][run_app] Flow will not be offloaded
```
If user selects:
* y - packet is re-injected, and a flow which hairpins future packets for that 5-tuple is added
* n - packet is dropped
