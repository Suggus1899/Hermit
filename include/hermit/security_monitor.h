#ifndef HERMIT_SECURITY_MONITOR_H
#define HERMIT_SECURITY_MONITOR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define HERMIT_MAX_ALERTS 1024
#define HERMIT_MAX_RULES 256
#define HERMIT_MAX_PROCESSES 4096

// Security alert types
enum hermit_alert_type {
    HERMIT_ALERT_SYSCALL_VIOLATION = 1,
    HERMIT_ALERT_NETWORK_ANOMALY,
    HERMIT_ALERT_FILE_ACCESS_VIOLATION,
    HERMIT_ALERT_PRIVILEGE_ESCALATION,
    HERMIT_ALERT_PROCESS_ANOMALY,
    HERMIT_ALERT_MEMORY_ANOMALY,
    HERMIT_ALERT_CONTAINER_ESCAPE_ATTEMPT,
    HERMIT_ALERT_SUSPICIOUS_EXECUTION,
    HERMIT_ALERT_UNAUTHORIZED_MOUNT,
    HERMIT_ALERT_SECURITY_POLICY_VIOLATION
};

// Security alert severity
enum hermit_alert_severity {
    HERMIT_SEVERITY_INFO = 1,
    HERMIT_SEVERITY_WARNING,
    HERMIT_SEVERITY_ERROR,
    HERMIT_SEVERITY_CRITICAL
};

// Security alert structure
struct hermit_security_alert {
    uint64_t alert_id;
    enum hermit_alert_type type;
    enum hermit_alert_severity severity;
    time_t timestamp;
    pid_t container_pid;
    pid_t process_pid;
    char container_id[64];
    char image_name[256];
    char command[512];
    char details[1024];
    char source_ip[64];
    char target_path[PATH_MAX];
    uint32_t syscall_number;
    uint64_t process_start_time;
    bool resolved;
};

// Security rule structure
struct hermit_security_rule {
    uint32_t rule_id;
    char name[128];
    enum hermit_alert_type alert_type;
    enum hermit_alert_severity severity;
    bool enabled;
    char pattern[512];
    char action[64]; // "alert", "block", "terminate"
    uint32_t timeout_seconds;
    uint64_t max_violations_per_hour;
    uint64_t current_violations;
    time_t last_violation;
};

// Process monitoring
struct hermit_process_info {
    pid_t pid;
    pid_t ppid;
    uid_t uid;
    gid_t gid;
    char comm[256];
    char cmdline[HERMIT_MAX_CMDLINE];
    char cwd[PATH_MAX];
    char exe[PATH_MAX];
    time_t start_time;
    uint64_t memory_usage;
    double cpu_usage;
    int open_files;
    int network_connections;
    bool is_container_process;
    char container_id[64];
};

// Network monitoring
struct hermit_network_connection {
    pid_t pid;
    char local_ip[64];
    uint16_t local_port;
    char remote_ip[64];
    uint16_t remote_port;
    char protocol[16];
    time_t established_time;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    bool is_suspicious;
};

// Security monitor configuration
struct hermit_security_monitor_config {
    bool enable_syscall_monitoring;
    bool enable_network_monitoring;
    bool enable_file_monitoring;
    bool enable_process_monitoring;
    bool enable_memory_monitoring;
    bool enable_real_time_alerts;
    bool enable_learning_mode;
    char alert_log_path[PATH_MAX];
    char rule_config_path[PATH_MAX];
    int max_alerts_per_hour;
    int alert_retention_days;
    bool enable_ml_detection;
    char ml_model_path[PATH_MAX];
};

// Security monitor state
struct hermit_security_monitor {
    struct hermit_security_monitor_config config;
    struct hermit_security_alert alerts[HERMIT_MAX_ALERTS];
    int alert_count;
    struct hermit_security_rule rules[HERMIT_MAX_RULES];
    int rule_count;
    struct hermit_process_info processes[HERMIT_MAX_PROCESSES];
    int process_count;
    struct hermit_network_connection connections[HERMIT_MAX_PROCESSES];
    int connection_count;
    uint64_t next_alert_id;
    uint32_t next_rule_id;
    bool running;
    time_t start_time;
    int monitor_thread;
    char state_file[PATH_MAX];
};

// Security monitor lifecycle
int hermit_security_monitor_init(struct hermit_security_monitor *monitor, const struct hermit_security_monitor_config *config);
int hermit_security_monitor_start(struct hermit_security_monitor *monitor);
int hermit_security_monitor_stop(struct hermit_security_monitor *monitor);
int hermit_security_monitor_cleanup(struct hermit_security_monitor *monitor);

// Alert management
int hermit_security_monitor_emit_alert(struct hermit_security_monitor *monitor, enum hermit_alert_type type, enum hermit_alert_severity severity, const char *container_id, const char *details);
int hermit_security_monitor_get_alerts(struct hermit_security_monitor *monitor, struct hermit_security_alert *alerts, int max_alerts, int *actual_count);
int hermit_security_monitor_resolve_alert(struct hermit_security_monitor *monitor, uint64_t alert_id);
int hermit_security_monitor_clear_alerts(struct hermit_security_monitor *monitor, const char *container_id);

// Rule management
int hermit_security_monitor_add_rule(struct hermit_security_monitor *monitor, const struct hermit_security_rule *rule);
int hermit_security_monitor_remove_rule(struct hermit_security_monitor *monitor, uint32_t rule_id);
int hermit_security_monitor_enable_rule(struct hermit_security_monitor *monitor, uint32_t rule_id, bool enabled);
int hermit_security_monitor_load_rules(struct hermit_security_monitor *monitor, const char *rules_file);
int hermit_security_monitor_save_rules(struct hermit_security_monitor *monitor, const char *rules_file);

// Monitoring functions
int hermit_security_monitor_container_start(struct hermit_security_monitor *monitor, const char *container_id, pid_t container_pid, const char *image_name);
int hermit_security_monitor_container_stop(struct hermit_security_monitor *monitor, const char *container_id);
int hermit_security_monitor_process_spawn(struct hermit_security_monitor *monitor, pid_t parent_pid, pid_t child_pid);
int hermit_security_monitor_syscall_event(struct hermit_security_monitor *monitor, pid_t pid, uint32_t syscall_number, const char *args);
int hermit_security_monitor_network_event(struct hermit_security_monitor *monitor, pid_t pid, const char *local_ip, uint16_t local_port, const char *remote_ip, uint16_t remote_port, const char *protocol);
int hermit_security_monitor_file_event(struct hermit_security_monitor *monitor, pid_t pid, const char *path, const char *operation, int flags);

// Anomaly detection
int hermit_security_monitor_detect_anomaly(struct hermit_security_monitor *monitor, pid_t pid);
int hermit_security_monitor_check_container_escape(struct hermit_security_monitor *monitor, pid_t pid);
int hermit_security_monitor_check_privilege_escalation(struct hermit_security_monitor *monitor, pid_t pid);
int hermit_security_monitor_check_suspicious_execution(struct hermit_security_monitor *monitor, pid_t pid, const char *exe_path);

// Machine learning integration
int hermit_security_monitor_train_model(struct hermit_security_monitor *monitor);
int hermit_security_monitor_predict_anomaly(struct hermit_security_monitor *monitor, pid_t pid, float *anomaly_score);
int hermit_security_monitor_update_model(struct hermit_security_monitor *monitor, const struct hermit_security_alert *alert);

// Reporting and analytics
int hermit_security_monitor_generate_report(struct hermit_security_monitor *monitor, const char *output_path);
int hermit_security_monitor_get_statistics(struct hermit_security_monitor *monitor, int *total_alerts, int *critical_alerts, int *resolved_alerts);
int hermit_security_monitor_export_alerts(struct hermit_security_monitor *monitor, const char *output_path, const char *format); // "json", "csv", "xml"

// Integration with external systems
int hermit_security_monitor_send_webhook(struct hermit_security_monitor *monitor, const char *webhook_url, const struct hermit_security_alert *alert);
int hermit_security_monitor_send_syslog(struct hermit_security_monitor *monitor, const struct hermit_security_alert *alert);
int hermit_security_monitor_send_email(struct hermit_security_monitor *monitor, const struct hermit_security_alert *alert, const char *email_address);

// Utility functions
const char* hermit_alert_type_to_string(enum hermit_alert_type type);
const char* hermit_alert_severity_to_string(enum hermit_alert_severity severity);
const char* hermit_security_monitor_status_to_string(bool running);

#endif
