/*
 * @Author: Lzww0608
 * @Date: 2025-01-08 21:30:00
 * @Description: ConcordKV metrics mock implementation for testing
 */

#include "../kv_engine_metrics.h"

// Mock implementations to avoid linking errors during testing

int kv_engine_metrics_register_engine(kv_engine_metrics_manager_t *manager, 
                                     kv_engine_type_t type, 
                                     const char *name) {
    (void)manager; (void)type; (void)name;
    return 0;
}

int kv_engine_metrics_record_read(kv_engine_metrics_manager_t *manager, 
                                 kv_engine_type_t type, 
                                 double latency_ms) {
    (void)manager; (void)type; (void)latency_ms;
    return 0;
}

int kv_engine_metrics_record_write(kv_engine_metrics_manager_t *manager, 
                                  kv_engine_type_t type, 
                                  double latency_ms) {
    (void)manager; (void)type; (void)latency_ms;
    return 0;
}

int kv_engine_metrics_record_delete(kv_engine_metrics_manager_t *manager, 
                                   kv_engine_type_t type, 
                                   double latency_ms) {
    (void)manager; (void)type; (void)latency_ms;
    return 0;
}

int kv_engine_metrics_record_error(kv_engine_metrics_manager_t *manager, 
                                  kv_engine_type_t type, 
                                  const char *error_type) {
    (void)manager; (void)type; (void)error_type;
    return 0;
}

int kv_engine_metrics_update_memory_usage(kv_engine_metrics_manager_t *manager, 
                                         kv_engine_type_t type, 
                                         size_t bytes) {
    (void)manager; (void)type; (void)bytes;
    return 0;
} 