#define _GNU_SOURCE
#include "hermit/observability.h"

#include "hermit/common/error.h"
#include "hermit/common/log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// Metrics Implementation
int hermit_metrics_init(struct hermit_metrics_collector *collector, const char *output_file)
{
    if (!collector || !output_file) {
        return -1;
    }
    
    memset(collector, 0, sizeof(*collector));
    strncpy(collector->output_file, output_file, sizeof(collector->output_file) - 1);
    collector->prometheus_enabled = true;
    collector->prometheus_port = 9090;
    
    hermit_log(HERMIT_LOG_INFO, "observability", "initialized metrics collector");
    return 0;
}

int hermit_metrics_increment_counter(struct hermit_metrics_collector *collector, const char *name, const char *labels, double value)
{
    if (!collector || !name || collector->metric_count >= HERMIT_MAX_METRICS) {
        return -1;
    }
    
    struct hermit_metric *metric = &collector->metrics[collector->metric_count];
    strncpy(metric->name, name, sizeof(metric->name) - 1);
    metric->type = HERMIT_METRIC_COUNTER;
    metric->value = value;
    metric->timestamp = time(NULL);
    
    if (labels) {
        strncpy(metric->labels, labels, sizeof(metric->labels) - 1);
    }
    
    collector->metric_count++;
    return 0;
}

int hermit_metrics_set_gauge(struct hermit_metrics_collector *collector, const char *name, const char *labels, double value)
{
    if (!collector || !name || collector->metric_count >= HERMIT_MAX_METRICS) {
        return -1;
    }
    
    struct hermit_metric *metric = &collector->metrics[collector->metric_count];
    strncpy(metric->name, name, sizeof(metric->name) - 1);
    metric->type = HERMIT_METRIC_GAUGE;
    metric->value = value;
    metric->timestamp = time(NULL);
    
    if (labels) {
        strncpy(metric->labels, labels, sizeof(metric->labels) - 1);
    }
    
    collector->metric_count++;
    return 0;
}

int hermit_metrics_record_histogram(struct hermit_metrics_collector *collector, const char *name, const char *labels, double value)
{
    if (!collector || !name || collector->metric_count >= HERMIT_MAX_METRICS) {
        return -1;
    }
    
    struct hermit_metric *metric = &collector->metrics[collector->metric_count];
    strncpy(metric->name, name, sizeof(metric->name) - 1);
    metric->type = HERMIT_METRIC_HISTOGRAM;
    metric->value = value;
    metric->timestamp = time(NULL);
    
    if (labels) {
        strncpy(metric->labels, labels, sizeof(metric->labels) - 1);
    }
    
    collector->metric_count++;
    return 0;
}

int hermit_metrics_export_prometheus(struct hermit_metrics_collector *collector, const char *output_path)
{
    FILE *fp;
    
    if (!collector || !output_path) {
        return -1;
    }
    
    fp = fopen(output_path, "w");
    if (!fp) {
        hermit_log(HERMIT_LOG_ERROR, "observability", "cannot create metrics file: %s", strerror(errno));
        return -1;
    }
    
    fprintf(fp, "# Hermit Metrics Export\n");
    fprintf(fp, "# Generated at %ld\n", time(NULL));
    
    for (int i = 0; i < collector->metric_count; i++) {
        struct hermit_metric *metric = &collector->metrics[i];
        
        fprintf(fp, "# HELP hermit_%s %s\n", metric->name, metric->name);
        fprintf(fp, "# TYPE hermit_%s %s\n", metric->name, 
                metric->type == HERMIT_METRIC_COUNTER ? "counter" :
                metric->type == HERMIT_METRIC_GAUGE ? "gauge" :
                metric->type == HERMIT_METRIC_HISTOGRAM ? "histogram" : "timer");
        
        if (strlen(metric->labels) > 0) {
            fprintf(fp, "hermit_%s{%s} %.2f %ld\n", metric->name, metric->labels, metric->value, metric->timestamp);
        } else {
            fprintf(fp, "hermit_%s %.2f %ld\n", metric->name, metric->value, metric->timestamp);
        }
    }
    
    fclose(fp);
    hermit_log(HERMIT_LOG_INFO, "observability", "exported %d metrics to %s", collector->metric_count, output_path);
    return 0;
}

// Events Implementation
int hermit_events_init(struct hermit_event_streamer *streamer, const char *output_file)
{
    if (!streamer || !output_file) {
        return -1;
    }
    
    memset(streamer, 0, sizeof(*streamer));
    strncpy(streamer->output_file, output_file, sizeof(streamer->output_file) - 1);
    streamer->json_enabled = true;
    streamer->syslog_enabled = true;
    
    hermit_log(HERMIT_LOG_INFO, "observability", "initialized event streamer");
    return 0;
}

int hermit_events_emit(struct hermit_event_streamer *streamer, enum hermit_event_type type, const char *source, const char *message, const char *details)
{
    if (!streamer || streamer->event_count >= HERMIT_MAX_EVENTS) {
        return -1;
    }
    
    struct hermit_event *event = &streamer->events[streamer->event_count];
    event->type = type;
    event->timestamp = time(NULL);
    
    if (source) {
        strncpy(event->source, source, sizeof(event->source) - 1);
    }
    
    if (message) {
        strncpy(event->message, message, sizeof(event->message) - 1);
    }
    
    if (details) {
        strncpy(event->details, details, sizeof(event->details) - 1);
    }
    
    streamer->event_count++;
    return 0;
}

int hermit_events_emit_container_event(struct hermit_event_streamer *streamer, enum hermit_event_type type, const char *container_id, const char *image_name, const char *message)
{
    if (!streamer || streamer->event_count >= HERMIT_MAX_EVENTS) {
        return -1;
    }
    
    struct hermit_event *event = &streamer->events[streamer->event_count];
    event->type = type;
    event->timestamp = time(NULL);
    
    if (container_id) {
        strncpy(event->container_id, container_id, sizeof(event->container_id) - 1);
    }
    
    if (image_name) {
        strncpy(event->image_name, image_name, sizeof(event->image_name) - 1);
    }
    
    if (message) {
        strncpy(event->message, message, sizeof(event->message) - 1);
    }
    
    streamer->event_count++;
    return 0;
}

int hermit_events_export_json(struct hermit_event_streamer *streamer, const char *output_path)
{
    FILE *fp;
    
    if (!streamer || !output_path) {
        return -1;
    }
    
    fp = fopen(output_path, "w");
    if (!fp) {
        hermit_log(HERMIT_LOG_ERROR, "observability", "cannot create events file: %s", strerror(errno));
        return -1;
    }
    
    fprintf(fp, "{\"events\":[\n");
    
    for (int i = 0; i < streamer->event_count; i++) {
        struct hermit_event *event = &streamer->events[i];
        
        fprintf(fp, "  {\n");
        fprintf(fp, "    \"type\": %d,\n", event->type);
        fprintf(fp, "    \"timestamp\": %ld,\n", event->timestamp);
        fprintf(fp, "    \"source\": \"%s\",\n", event->source);
        fprintf(fp, "    \"message\": \"%s\",\n", event->message);
        fprintf(fp, "    \"details\": \"%s\",\n", event->details);
        fprintf(fp, "    \"container_id\": \"%s\",\n", event->container_id);
        fprintf(fp, "    \"image_name\": \"%s\"\n", event->image_name);
        fprintf(fp, "  }%s\n", i < streamer->event_count - 1 ? "," : "");
    }
    
    fprintf(fp, "]}\n");
    fclose(fp);
    
    hermit_log(HERMIT_LOG_INFO, "observability", "exported %d events to %s", streamer->event_count, output_path);
    return 0;
}

// Log Aggregation Implementation
int hermit_logs_init(struct hermit_log_aggregator *aggregator, const char *log_dir)
{
    if (!aggregator || !log_dir) {
        return -1;
    }
    
    memset(aggregator, 0, sizeof(*aggregator));
    strncpy(aggregator->log_dir, log_dir, sizeof(aggregator->log_dir) - 1);
    aggregator->structured_enabled = true;
    aggregator->rotation_size_mb = 100;
    aggregator->retention_days = 30;
    
    // Create log directory
    if (mkdir(log_dir, 0755) != 0 && errno != EEXIST) {
        hermit_log(HERMIT_LOG_ERROR, "observability", "cannot create log directory: %s", strerror(errno));
        return -1;
    }
    
    hermit_log(HERMIT_LOG_INFO, "observability", "initialized log aggregator");
    return 0;
}

int hermit_logs_add_entry(struct hermit_log_aggregator *aggregator, const char *level, const char *component, const char *message, const char *container_id, const char *service_name)
{
    if (!aggregator || !level || !component || !message || aggregator->entry_count >= HERMIT_MAX_EVENTS) {
        return -1;
    }
    
    struct hermit_log_entry *entry = &aggregator->entries[aggregator->entry_count];
    
    // Format timestamp
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(entry->timestamp, sizeof(entry->timestamp), "%Y-%m-%dT%H:%M:%SZ", tm_info);
    
    strncpy(entry->level, level, sizeof(entry->level) - 1);
    strncpy(entry->component, component, sizeof(entry->component) - 1);
    strncpy(entry->message, message, sizeof(entry->message) - 1);
    
    if (container_id) {
        strncpy(entry->container_id, container_id, sizeof(entry->container_id) - 1);
    }
    
    if (service_name) {
        strncpy(entry->service_name, service_name, sizeof(entry->service_name) - 1);
    }
    
    aggregator->entry_count++;
    return 0;
}

int hermit_logs_export_structured(struct hermit_log_aggregator *aggregator, const char *output_path)
{
    FILE *fp;
    
    if (!aggregator || !output_path) {
        return -1;
    }
    
    fp = fopen(output_path, "w");
    if (!fp) {
        hermit_log(HERMIT_LOG_ERROR, "observability", "cannot create log file: %s", strerror(errno));
        return -1;
    }
    
    fprintf(fp, "{\"log_entries\":[\n");
    
    for (int i = 0; i < aggregator->entry_count; i++) {
        struct hermit_log_entry *entry = &aggregator->entries[i];
        
        fprintf(fp, "  {\n");
        fprintf(fp, "    \"timestamp\": \"%s\",\n", entry->timestamp);
        fprintf(fp, "    \"level\": \"%s\",\n", entry->level);
        fprintf(fp, "    \"component\": \"%s\",\n", entry->component);
        fprintf(fp, "    \"message\": \"%s\",\n", entry->message);
        fprintf(fp, "    \"container_id\": \"%s\",\n", entry->container_id);
        fprintf(fp, "    \"service_name\": \"%s\"\n", entry->service_name);
        fprintf(fp, "  }%s\n", i < aggregator->entry_count - 1 ? "," : "");
    }
    
    fprintf(fp, "]}\n");
    fclose(fp);
    
    hermit_log(HERMIT_LOG_INFO, "observability", "exported %d log entries to %s", aggregator->entry_count, output_path);
    return 0;
}

int hermit_metrics_cleanup(struct hermit_metrics_collector *collector)
{
    if (!collector) return 0;
    
    memset(collector, 0, sizeof(*collector));
    return 0;
}

int hermit_events_cleanup(struct hermit_event_streamer *streamer)
{
    if (!streamer) return 0;
    
    memset(streamer, 0, sizeof(*streamer));
    return 0;
}

int hermit_logs_cleanup(struct hermit_log_aggregator *aggregator)
{
    if (!aggregator) return 0;
    
    memset(aggregator, 0, sizeof(*aggregator));
    return 0;
}
