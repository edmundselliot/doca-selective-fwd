#include "selective_fwd.h"

DOCA_LOG_REGISTER(SELECTIVE_FWD_PIPE_MGR);

PipeMgr::PipeMgr() {}

PipeMgr::~PipeMgr() {}

doca_error_t PipeMgr::add_entry(std::string name, struct doca_flow_pipe_entry* entry) {
    entries.push_back(std::make_pair(name, entry));
    return DOCA_SUCCESS;
}

doca_error_t PipeMgr::remove_entry(struct doca_flow_pipe_entry* entry) {
    for (auto it = entries.begin(); it != entries.end(); it++) {
        if (it->second == entry) {
            entries.erase(it);
            return DOCA_SUCCESS;
        }
    }
    return DOCA_ERROR_NOT_FOUND;
}

void PipeMgr::print_stats() {
    DOCA_LOG_INFO("=================================");
    for (auto entry : entries) {
        struct doca_flow_resource_query stats;
        doca_error_t result = doca_flow_resource_query_entry(entry.second, &stats);
        if (result == DOCA_SUCCESS)
            DOCA_LOG_INFO("%s hit: %lu packets, %lu bytes", entry.first.c_str(), stats.counter.total_pkts, stats.counter.total_bytes);
        else
            DOCA_LOG_ERR("Failed to query entry %s: %s", entry.first.c_str(), doca_error_get_descr(result));
    }
}
