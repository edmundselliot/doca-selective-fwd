#include "main.h"

struct port_stats {
    doca_flow_query  ingress_root;
    doca_flow_query  ingress_root_entry;
    doca_flow_query  egress_root;
    doca_flow_query  egress_root_entry;
};

struct app_stats {
    struct port_stats p0;
    struct port_stats p1;
};

void
get_app_stats(struct app_stats *stats)
{
    for (int i = 0; i < 2; i ++) {
        struct port_ctx *port_ctx = i == 0 ? &app_cfg.p0_ctx : &app_cfg.p1_ctx;
        struct port_stats *port_stats = i == 0 ? &stats->p0 : &stats->p1;

        doca_flow_query_pipe_miss(port_ctx->ingress_root_pipe, &port_stats->ingress_root);
        doca_flow_query_pipe_miss(port_ctx->egress_root_pipe, &port_stats->egress_root);

        doca_flow_query_entry(port_ctx->ingress_root_pipe_entry, &port_stats->ingress_root_entry);
        doca_flow_query_entry(port_ctx->egress_root_pipe_entry, &port_stats->egress_root_entry);
    }
}

void
print_app_stats(struct app_stats *old_stats, struct app_stats *new_stats)
{
    DOCA_LOG_INFO("---------------------------------");
    for (int i = 0; i < 2; i ++) {
        struct port_stats *old_port_stats = i == 0 ? &old_stats->p0 : &old_stats->p1;
        struct port_stats *new_port_stats = i == 0 ? &new_stats->p0 : &new_stats->p1;

        DOCA_LOG_INFO("\tP%d INGRESS ROOT MISSES: %lu", i, new_port_stats->ingress_root.total_pkts - old_port_stats->ingress_root.total_pkts);
        DOCA_LOG_INFO("\tP%d EGRESS  ROOT MISSES: %lu", i, new_port_stats->egress_root.total_pkts - old_port_stats->egress_root.total_pkts);

        DOCA_LOG_INFO("\tP%d INGRESS ROOT HITS: %lu", i, new_port_stats->ingress_root_entry.total_pkts - old_port_stats->ingress_root_entry.total_pkts);
        DOCA_LOG_INFO("\tP%d EGRESS  ROOT HITS: %lu", i, new_port_stats->egress_root_entry.total_pkts - old_port_stats->egress_root_entry.total_pkts);
        DOCA_LOG_INFO(" ");
    }
}

void
monitoring_loop()
{
    struct app_stats stats1;
    struct app_stats stats2;

    while (1) {
        get_app_stats(&stats1);
        print_app_stats(&stats2, &stats1);
        sleep(5);
        get_app_stats(&stats2);
        print_app_stats(&stats1, &stats2);
        sleep(5);
    }
}