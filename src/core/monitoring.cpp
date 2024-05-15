#include "main.h"

struct port_stats {
    doca_flow_query  egress_root;
};

struct app_stats {
    struct port_stats p0;
    struct port_stats p1;
};

void
get_app_stats(struct app_stats *stats)
{
    doca_flow_query_pipe_miss(app_cfg.p0_ctx.egress_root_pipe, &stats->p0.egress_root);
    doca_flow_query_pipe_miss(app_cfg.p1_ctx.egress_root_pipe, &stats->p1.egress_root);
}

void
print_app_stats(struct app_stats *old_stats, struct app_stats *new_stats)
{
    DOCA_LOG_INFO("P0 EGRESS ROOT MISSES: %lu", new_stats->p0.egress_root.total_pkts - old_stats->p0.egress_root.total_pkts);
    DOCA_LOG_INFO("P1 EGRESS ROOT MISSES: %lu", new_stats->p1.egress_root.total_pkts - old_stats->p1.egress_root.total_pkts);
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