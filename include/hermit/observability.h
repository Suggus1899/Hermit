#ifndef HERMIT_OBSERVABILITY_H
#define HERMIT_OBSERVABILITY_H

#include <stddef.h>
#include <stdbool.h>

#define HERMIT_MAX_METRICS 1024
#define HERMIT_MAX_EVENTS 4096
#define HERMIT_MAX_LOG_ENTRY 4096

enum hermit_metric_type {
    HERMIT_METRIC_COUNTER = 1,
    HERMIT_METRIC_GAUGE,
    HERMIT_METRIC_HISTOGRAM,
    HERMIT_METRIC_TIMER
};

enum hermit_event_type {
    HERMIT_EVENT_CONTAINER_START = 1,
    HERMIT_EVENT_CONTAINER_STOP,
    HERMIT_EVENT_CONTAINER_ERROR,
    HERMIT_EVENT_IMAGE_BUILD,
    HERMIT_EVENT_IMAGE_PULL,
    HERMIT_EVENT_IMAGE_PUSH,
    HERMIT_EVENT_VOLUME_CREATE,
    HERMIT_EVENT_VOLUME_DELETE,
    HERMIT_EVENT_SYSTEM_ERROR,
    HERMIT_EVENT_SECURITY_VIOLATION
};

struct hermit_metric {
    char name[128];
    enum hermit_metric_type type;
    double value;
    char labels[256];
    time_t timestamp;
};

struct hermit_event {
    enum hermit_event_type type;
    char source[64];
    char message[512];
    char details[1024];
    time_t timestamp;
    char container_id[64];
    char image_name[256];
};

struct hermit_log_entry {
    char timestamp[32];
    char level[16];
    char component[64];
    char message[HERMIT_MAX_LOG_ENTRY];
    char container_id[64];
    char service_name[64];
};

struct hermit_metrics_collector {
    struct hermit_metric metrics[HERMIT_MAX_METRICS];
    int metric_count;
    char output_file[PATH_MAX];
    bool prometheus_enabled;
    int prometheus_port;
};

struct hermit_event_streamer {
    struct hermit_event events[HERMIT_MAX_EVENTS];
    int event_count;
    char output_file[PATH_MAX];
    bool json_enabled;
    bool syslog_enabled;
};

struct hermit_log_aggregator {
    struct hermit_log_entry entries[HERMIT_MAX_EVENTS];
    int entry_count;
    char log_dir[PATH_MAX];
    bool structured_enabled;
    int rotation_size_mb;
    int retention_days;
};

// Metrics functions
int hermit_metrics_init(struct hermit_metrics_collector *collector, const char *output_file);
int hermit_metrics_increment_counter(struct hermit_metrics_collector *collector, const char *name, const char *labels, double value);
int hermit_metrics_set_gauge(struct hermit_metrics_collector *collector, const char *name, const char *labels, double value);
int hermit_metrics_record_histogram(struct hermit_metrics_collector *collector, const char *name, const char *labels, double value);
int hermit_metrics_start_timer(struct hermit_metrics_collector *collector, const char *name, const char *labels);
int hermit_metrics_stop_timer(struct hermit_metrics_collector *collector, const char *name, const char *labels);
int hermit_metrics_export_prometheus(struct hermit_metrics_collector *collector, const char *output_path);
int hermit_metrics_cleanup(struct hermit_metrics_collector *collector);

// Event streaming functions
int hermit_events_init(struct hermit_event_streamer *streamer, const char *output_file);
int hermit_events_emit(struct hermit_event_streamer *streamer, enum hermit_event_type type, const char *source, const char *message, const char *details);
int hermit_events_emit_container_event(struct hermit_event_streamer *streamer, enum hermit_event_type type, const char *container_id, const char *image_name, const char *message);
int hermit_events_export_json(struct hermit_event_streamer *streamer, const char *output_path);
int hermit_events_stream_to_syslog(struct hermit_event_streamer *streamer);
int hermit_events_cleanup(struct hermit_event_streamer *streamer);

// Log aggregation functions
int hermit_logs_init(struct hermit_log_aggregator *aggregator, const char *log_dir);
int hermit_logs_add_entry(struct hermit_log_aggregator *aggregator, const char *level, const char *component, const char *message, const char *container_id, const char *service_name);
int hermit_logs_rotate(struct hermit_log_aggregator *aggregator);
int hermit_logs_export_structured(struct hermit_log_aggregator *aggregator, const char *output_path);
int hermit_logs_cleanup(struct hermit_log_aggregator *aggregator);

#endif
