# DOCA selective forwarding sample
Sample application showing a simple forwarding application, which allows users to explicitly allow/deny flows.

##  Topology
```
 ┌─────────────────────────────────────────────────────────────────────┐
 │x86 Host                                                             │
 │      ┌──────────────────────────────────────────────────────────────┤
 │      │Virtual machine                                               │
 │      │                                                              │
 │      │    ┌─────────────────────────────────────────────────────────┤
 │      │    │DPDK PMD                                                 │
 │      │    │                    ┌---allow/deny◄---┐                  │
 │      │    │                    ¦                 ¦                  │
 │      │    ├────────────────────¦───────┬─────────¦──────────────────┤
 │      │    │VF 1                ¦       │VF 2     ¦                  │
 │      │    │                    ¦       │         ¦                  │
 │      │    │                    ¦       │         ¦                  │
 │      │    │                    ¦       │         ¦                  │
 ├──────┴────┼──────────────┬─────¦───────┼─────────¦────┬─────────────┤
 │NVIDIA NIC │ VF 1 VNF rx  │ VF 1¦VNF tx │ VF 2 VNF¦rx  │ VF 2 VNF tx │
 │           │ ┌─────┐      │     ¦-------┼--┐   ┌──┴──┐ │             │
 │           │ │ rss │      │     ¦       │  ¦   │ rss │ │             │
 │           │ └─────┘      │     ¦       │  ¦   └──▲──┘ │             │
 │           │              │     ¦       │  ¦      ¦    │             │
 │           │ ┌──────────┐ │     ¦       │ ┌┴──────┴──┐ │             │
 │           │ │ hairpin  │ │     ¦       │ │ hairpin  │ │             │
 │           └─┴──────────┴─┴─────▼───────┴─┴──────────┴─┴─────────────┤
 │                                                                     │
 └─────────────────────────────────────────────────────────────────────┘
```

### Slow path
Any packets without offloaded flows will get put in the VF's rx queues and rx_burst'ed by the PMD. The PMD will then make a decision on whether to allow the traffic or not.

* If the PMD decides to allow the flow, the packet will be tx_bursted to the opposite VF's TX queues and put on the wire.
* If the PMD decides to deny the flow, the packet will be dropped.

### Fast path
Any packets with offloaded flows will be directly hairpinned to the opposite VF's TX queues and will be put on the wire, without incurring any CPU overhead.

## Prerequisites
* DOCA: `2.7.0085`
* DPDK: `22.11.2404.0.11`

Make sure they are in pkg-config path: `pkg-config --modversion libdpdk doca`.

## Building
```
meson build
ninja -C build
```

## Sample init
With VF PCIs `26:00.3` and `26.00.5`:
```
./build/doca-selective-fwd -a26:00.3,dv_flow_en=2,dv_xmeta_en=4 -a26:00.5,dv_flow_en=2,dv_xmeta_en=4
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
