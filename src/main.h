#include <iostream>
#include <list>
#include <chrono>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <array>
#include <regex>
#include <thread>

#include <signal.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <arpa/inet.h>

#include <doca_argp.h>
#include <doca_flow.h>
#include <doca_log.h>
#include <doca_dev.h>
#include <doca_dpdk.h>

#include <rte_ethdev.h>
#include <rte_malloc.h>
#include <dpdk_utils.h>


typedef struct app_ctx app_ctx;

extern app_ctx router_cfg;

DOCA_LOG_REGISTER("switch_demo");

#define PORT_ID_ANY UINT16_MAX
/* Timeout for processing pipe entries */
#define DEFAULT_TIMEOUT_US 10000
/* Maximum number of ECMP sets supported */
#define MAX_ECMP_SETS 10
/* Maximum VRF domains supported */
#define MAX_VRF_DOMAINS 2
/* Number of ports supported by this application */
#define MAX_NUM_PORTS 2

/* user context struct that will be used in entries process callback */
struct entries_status {
	bool failure;	      /* will be set to true if some entry status will not be success */
	int nb_processed;     /* number of entries that was already processed */
	int entries_in_queue; /* number of entries in queue that is waiting to process */
};

struct vrf_domain_ctx {
    // All fwd entries which point to this VRF domain
    std::vector<doca_flow_pipe_entry*> vrf_fwd_entries;

    // Single classifier pipe, entry point
    // Each classifier pipe will have two entries that forward v4/v6 packets
    struct doca_flow_pipe *classifier_pipe;
    struct doca_flow_pipe_entry *classifier_v4_fwd;
    struct doca_flow_pipe_entry *classifier_v6_fwd;

    // Direct match pipes will have entries == number of full IPs
    struct doca_flow_pipe *exact_match_pipe_v4;
    struct doca_flow_pipe *exact_match_pipe_v6;

    // LPM pipes will have entries == number of prefixes
    struct doca_flow_pipe *lpm_pipe_v4;
    struct doca_flow_pipe *lpm_pipe_v6;
};

struct port_ctx {
    uint16_t port_id;
    struct doca_flow_port *port;
    rte_ether_addr port_mac;

    // If non-NULL, it's an opened device
    struct doca_dev *dev;

    // Single ingress root pipe, entry point
    struct doca_flow_pipe *root_pipe;

    // Used to select the VRF domain based on VLAN
    struct doca_flow_pipe *vlan_selector_pipe;

    // VRF domain
    // Each VRF domain contains IP classifiers & LPMs
    struct vrf_domain_ctx vrf_domain_list[MAX_VRF_DOMAINS];

    // We will have number of ECMP pipes == number of ECMP sets
    // Each ECMP pipe will have entries == number of next hops in that set
    struct doca_flow_pipe *ecmp_pipe_v4[MAX_ECMP_SETS];
    std::vector<doca_flow_pipe_entry*> ecmp_pipe_v4_entries[MAX_ECMP_SETS];
    struct doca_flow_pipe *ecmp_pipe_v6[MAX_ECMP_SETS];
    std::vector<doca_flow_pipe_entry*> ecmp_pipe_v6_entries[MAX_ECMP_SETS];

    // Single egress root pipe, exit point
    struct doca_flow_pipe *egress_root_pipe;
};

struct app_ctx {
    struct application_dpdk_config dpdk_config;
    struct port_ctx p0_ctx;
    // struct port_ctx p1_ctx;
};

void
monitoring_loop();

doca_error_t
register_argp(struct app_ctx *config);

doca_error_t
open_doca_device_with_pci(const char *pci_addr, struct doca_dev **retval);

int
flow_init(struct app_ctx *config);

#define SET_IP6_ADDR(addr, a, b, c, d) \
	do { \
		addr[0] = a; \
		addr[1] = b; \
		addr[2] = c; \
		addr[3] = d; \
	} while (0)

#define SET_MAC_ADDR(addr, a, b, c, d, e, f)\
do {\
	addr[0] = a & 0xff;\
	addr[1] = b & 0xff;\
	addr[2] = c & 0xff;\
	addr[3] = d & 0xff;\
	addr[4] = e & 0xff;\
	addr[5] = f & 0xff;\
} while (0)

