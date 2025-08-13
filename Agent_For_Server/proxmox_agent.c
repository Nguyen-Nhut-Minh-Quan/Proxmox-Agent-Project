#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
//The name of the env for Tank Temperature reader is .proxmox_agent_env --> use nano .proxmox_agent_env
// #include <mongoc/mongoc.h> // REMOVED: No longer needed for direct MongoDB interaction
#include <regex.h>
#include <curl/curl.h> // Required for HTTP requests
#include <stdbool.h>
#define MAX_LINE_LENGTH 256
#define MAX_ITEMS 32 

// Modify the 4 variables below in the .env, plus the new FastAPI URL
char *tank_location = NULL; 
char *mongo_client_ip = NULL; // Can be REMOVED if no direct MongoDB operations at all
char *physical_server_id = NULL;
char *tank_id = NULL;
char *fastapi_base_url = NULL; // NEW: Base URL for your FastAPI server
char timestamp[64];
// gcc -g -o test test.c $(pkg-config --cflags --libs libmongoc-1.0) -lcurl

// --- Utility Functions ---

void get_iso_timestamp()
{
    time_t now = time(NULL);
    strftime(timestamp, 64, "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
}

// Structure to hold the parsed sensor data (used by parse_sensors_output)
typedef struct {
    char* adapter_name;
    char** socket_names;
    int num_sockets;
    char** core_names;
    int num_cores;
} SensorData;

void free_sensor_data(SensorData* data) {
    if (data == NULL) return;

    free(data->adapter_name);

    for (int i = 0; i < data->num_sockets; i++) {
        free(data->socket_names[i]);
    }
    free(data->socket_names);

    for (int i = 0; i < data->num_cores; i++) {
        free(data->core_names[i]);
    }
    free(data->core_names);

    free(data);
}

// Function to add a unique string to a dynamic array of strings
bool add_unique_string(char*** array_ptr, int* count_ptr, const char* new_str) {
    for (int i = 0; i < *count_ptr; i++) {
        if (strcmp((*array_ptr)[i], new_str) == 0) {
            return false;
        }
    }

    char** temp_array = realloc(*array_ptr, (*count_ptr + 1) * sizeof(char*));
    if (temp_array == NULL) {
        perror("Failed to reallocate array for strings");
        return false;
    }
    *array_ptr = temp_array;

    (*array_ptr)[*count_ptr] = strdup(new_str);
    if ((*array_ptr)[*count_ptr] == NULL) {
        perror("Failed to duplicate string");
        if (*count_ptr == 0) {
            free(*array_ptr);
            *array_ptr = NULL;
        } else {
            // Attempt to reallocate to original size if strdup failed
            realloc(*array_ptr, (*count_ptr) * sizeof(char*)); 
        }
        return false;
    }

    (*count_ptr)++;
    return true;
}

// Function to parse the sensors command output
SensorData* parse_sensors_output() {
    FILE *fp;
    char line[MAX_LINE_LENGTH];
    SensorData* data = (SensorData*)malloc(sizeof(SensorData));
    if (data == NULL) {
        perror("Failed to allocate SensorData");
        return NULL;
    }
    data->adapter_name = NULL;
    data->socket_names = NULL;
    data->num_sockets = 0;
    data->core_names = NULL;
    data->num_cores = 0;

    fp = popen("sensors", "r");
    if (fp == NULL) {
        perror("Failed to run command 'sensors'");
        free(data);
        return NULL;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        line[strcspn(line, "\n")] = 0;

        if (data->adapter_name == NULL && strstr(line, "Adapter:") == line) {
            char* adapter_start = strstr(line, ":");
            if (adapter_start) {
                adapter_start += 2;
                while (*adapter_start == ' ' || *adapter_start == '\t') {
                    adapter_start++;
                }
                data->adapter_name = strdup(adapter_start);
                if (data->adapter_name == NULL) {
                    perror("Failed to duplicate adapter name");
                    pclose(fp);
                    free_sensor_data(data);
                    return NULL;
                }
            }
        }

        if (strstr(line, "coretemp-isa-") == line) {
            char socket_name[MAX_LINE_LENGTH];
            if (sscanf(line, "%s", socket_name) == 1) {
                add_unique_string(&(data->socket_names), &(data->num_sockets), socket_name);
            }
        }

        if (strstr(line, "Core ") == line && strchr(line, ':') != NULL) {
            char core_part[MAX_LINE_LENGTH];
            char* colon_pos = strchr(line, ':');
            if (colon_pos) {
                size_t len = colon_pos - line;
                if (len < sizeof(core_part)) {
                    strncpy(core_part, line, len);
                    core_part[len] = '\0';
                    add_unique_string(&(data->core_names), &(data->num_cores), core_part);
                }
            }
        }
    }
    pclose(fp);
    return data;
}

// --- HTTP POST Helper Function ---

// Callback function for curl (does nothing here, but required)
size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    (void)contents; // Suppress unused parameter warning
    (void)userp;    // Suppress unused parameter warning
    return size * nmemb;
}

CURLcode post_json_to_api(const char *url, const char *json_payload) {
    CURL *curl;
    CURLcode res;
    struct curl_slist *headers = NULL;

    printf("[DEBUG] post_json_to_api received URL: %s\n", url);

    curl = curl_easy_init();
    if (curl) {
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(json_payload));

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            fprintf(stderr, "[ERROR] curl_easy_perform() failed for URL: %s\nReason: %s\n", url, curl_easy_strerror(res));
        } else {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            if (http_code < 200 || http_code >= 300) {
                fprintf(stderr, "[WARN] API call to %s returned HTTP %ld\nPayload: %s\n", url, http_code, json_payload);
            } else {
                printf("[DEBUG] API call to %s succeeded (HTTP %ld)\n", url, http_code);
            }
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    } else {
        fprintf(stderr, "[ERROR] Failed to initialize CURL.\n");
        res = CURLE_FAILED_INIT;
    }

    return res;
}

// --- Data Insertion Functions (Calling FastAPI APIs) ---

void insert_temperatures()
{
    printf("[DEBUG] Starting insert_temperatures\n");
    FILE *fp = popen("sensors", "r");
    if (!fp)
    {
        fprintf(stderr, "[ERROR] Failed to run sensors command\n");
        return;
    }

    char line[256];
    char current_chip[32] = {0};
    regex_t chip_regex, temp_regex;
    regmatch_t matches[3];

    const char *chip_pattern = "^(coretemp-isa-[0-9]+)";
    const char *temp_pattern = "Core ([0-9]+):[[:space:]]+\\+([0-9]+\\.[0-9])°C";

    if (regcomp(&chip_regex, chip_pattern, REG_EXTENDED) != 0 ||
        regcomp(&temp_regex, temp_pattern, REG_EXTENDED) != 0)
    {
        fprintf(stderr, "[ERROR] Failed to compile regex\n");
        pclose(fp);
        return;
    }

    // Buffer for the full JSON array
    char json_payload[4096];
    size_t offset = 0;

    offset += snprintf(json_payload + offset, sizeof(json_payload) - offset, "[");

    int first_entry = 1;

    while (fgets(line, sizeof(line), fp))
    {
        if (regexec(&chip_regex, line, 2, matches, 0) == 0)
        {
            int len = matches[1].rm_eo - matches[1].rm_so;
            strncpy(current_chip, line + matches[1].rm_so, len);
            current_chip[len] = '\0';
            continue;
        }

        if (regexec(&temp_regex, line, 3, matches, 0) == 0 && strlen(current_chip) > 0)
        {
            char core[8] = {0}, temp_str[8] = {0};
            int core_len = matches[1].rm_eo - matches[1].rm_so;
            int temp_len = matches[2].rm_eo - matches[2].rm_so;

            strncpy(core, line + matches[1].rm_so, core_len);
            core[core_len] = '\0';

            strncpy(temp_str, line + matches[2].rm_so, temp_len);
            temp_str[temp_len] = '\0';

            float temp = atof(temp_str);

            printf("[DEBUG] Parsed temperature: %s - Core %s => %.1f°C\n", current_chip, core, temp);

            if (!first_entry)
            {
                offset += snprintf(json_payload + offset, sizeof(json_payload) - offset, ",");
            }
            first_entry = 0;

            offset += snprintf(json_payload + offset, sizeof(json_payload) - offset,
                               "{\"TANK_LOCATION\":\"%s\","
                               "\"TANK_ID\":\"%s\","
                               "\"SERVER_ID\":\"%s\","
                               "\"core\":\"%s\","
                               "\"temperature_celsius\":%.1f,"
                               "\"adapter\":\"%s\","
                               "\"Timestamp\":\"%s\"}",
                               tank_location,
                               tank_id,
                               physical_server_id,
                               core,
                               temp,
                               current_chip,
                               timestamp);
        }
    }

    offset += snprintf(json_payload + offset, sizeof(json_payload) - offset, "]");

    // Send the array in one request
    char url_buffer[256];
    snprintf(url_buffer, sizeof(url_buffer), "%s/physical-server/temperature/", fastapi_base_url);
    printf("[DEBUG] Sending batched temperature JSON to %s\n", url_buffer);
    post_json_to_api(url_buffer, json_payload);

    regfree(&chip_regex);
    regfree(&temp_regex);
    pclose(fp);
    printf("[DEBUG] Completed insert_temperatures\n");
}


void insert_ram_usage_via_api() // Modified signature
{
    printf("[DEBUG] Starting insert_ram_usage\n");

    FILE *fp = popen("free -m | awk 'NR==2{printf \"%d %d %d\", $2,$3,$7}'", "r");
    if (!fp)
    {
        fprintf(stderr, "[ERROR] Failed to run free command\n");
        return;
    }

    int total = 0, used = 0, free_mem = 0;
    if (fscanf(fp, "%d %d %d", &total, &used, &free_mem) != 3)
    {
        fprintf(stderr, "[ERROR] Failed to parse free output\n");
        pclose(fp);
        return;
    }
    pclose(fp);
    
    char json_payload[512];
    int len = snprintf(json_payload, sizeof(json_payload),
                       "{\"TANK_LOCATION\": \"%s\", "
                       "\"TANK_ID\": \"%s\", "
                       "\"SERVER_ID\": \"%s\", "
                       "\"total_ram\": %d, "
                       "\"used_ram\": %d, "
                       "\"free_ram\": %d, "
                       "\"Timestamp\": \"%s\"}",
                       tank_location,
                       tank_id,
                       physical_server_id,
                       total,
                       used,
                       free_mem,
                       timestamp);

    if (len < 0 || len >= sizeof(json_payload)) {
        fprintf(stderr, "[ERROR] JSON payload for RAM too large or error occurred.\n");
        return;
    }

    char url_buffer[256];
    snprintf(url_buffer, sizeof(url_buffer), "%s/physical-server/ram-usage/", fastapi_base_url);

    printf("[DEBUG] Sending RAM usage JSON to %s\n", url_buffer);
    post_json_to_api(url_buffer, json_payload);
}

void insert_virtual_stats_via_api() // Modified signature
{
    FILE *fp = popen("./VirtualServerStat.sh", "r");
    if (!fp)
    {
        printf("Cannot read the command VirtualServerStat.sh\n");
        return;
    }
    char line[512];
    fgets(line, sizeof(line), fp); // Read header line
    while (fgets(line, sizeof(line), fp))
    {
        char Name[52];
        char Status[20];
        char CPU[50];
        char Ram[50];
        char Disk[50];
        
        int match = sscanf(line, "%51s %19s %49s %49s %49s", Name, Status, CPU, Ram, Disk);
        if (match != 5 )
        {
            printf("[WARN] Some Data point from VirtualServerStat.sh does not match format in line: %s", line);
            continue; // Skip this line and try next
        }
        
        float percent;
        int numCores;
        // Parse CPU string (e.g., "25.7%/4")
        if (sscanf(CPU, "%f/%d", &percent, &numCores) != 2) {
            printf("[WARN] Failed to parse CPU string '%s' for VM %s\n", CPU, Name);
            percent = 0.0;
            numCores = 0;
        }

        // --- CPU Virtual ---
        char cpu_json[512];
        int len_cpu = snprintf(cpu_json, sizeof(cpu_json),
                               "{\"TANK_LOCATION\": \"%s\", "
                               "\"TANK_NUM\": \"%s\", "
                               "\"SERVER_NUM\": \"%s\", "
                               "\"SERVER_VIRTUAL_NAME\": \"%s\", "
                               "\"CPU_USAGE\": \"%.1f%%\", " // Keep as string with %% if API expects it
                               "\"NUM_CORES\": %d, "
                               "\"Timestamp\": \"%s\"}",
                               tank_location,
                               tank_id, // TANK_NUM maps to tank_id
                               physical_server_id,
                               Name,
                               percent,
                               numCores,
                               timestamp);
        if (len_cpu < 0 || len_cpu >= sizeof(cpu_json)) { fprintf(stderr, "[ERROR] CPU virtual JSON too large.\n"); }
        else { 
            char url_buffer[256];
            snprintf(url_buffer, sizeof(url_buffer), "%s/virtual-server/cpu-usage/", fastapi_base_url);
            post_json_to_api(url_buffer, cpu_json); 
        }

        // --- RAM Virtual ---
        char ram_json[512];
        int len_ram = snprintf(ram_json, sizeof(ram_json),
                               "{\"TANK_LOCATION\": \"%s\", "
                               "\"TANK_NUM\": \"%s\", "
                               "\"SERVER_NUM\": \"%s\", "
                               "\"SERVER_VIRTUAL_NAME\": \"%s\", "
                               "\"Ram_Usage\": \"%s\", " // Ram_Usage is string in model
                               "\"Timestamp\": \"%s\"}",
                               tank_location,
                               tank_id,
                               physical_server_id,
                               Name,
                               Ram, // Use the raw Ram string from sscanf
                               timestamp);
        if (len_ram < 0 || len_ram >= sizeof(ram_json)) { fprintf(stderr, "[ERROR] RAM virtual JSON too large.\n"); }
        else { 
            char url_buffer[256];
            snprintf(url_buffer, sizeof(url_buffer), "%s/virtual-server/ram-usage/", fastapi_base_url);
            post_json_to_api(url_buffer, ram_json); 
        }

        // --- Disk Virtual ---
        char disk_json[512];
        int len_disk = snprintf(disk_json, sizeof(disk_json),
                                "{\"TANK_LOCATION\": \"%s\", "
                                "\"TANK_NUM\": \"%s\", "
                                "\"SERVER_NUM\": \"%s\", "
                                "\"SERVER_VIRTUAL_NAME\": \"%s\", "
                                "\"Disk_Usage\": \"%s\", " // Disk_Usage is string in model
                                "\"Timestamp\": \"%s\"}",
                                tank_location,
                                tank_id,
                                physical_server_id,
                                Name,
                                Disk, // Use the raw Disk string from sscanf
                                timestamp);
        if (len_disk < 0 || len_disk >= sizeof(disk_json)) { fprintf(stderr, "[ERROR] Disk virtual JSON too large.\n"); }
        else { 
            char url_buffer[256];
            snprintf(url_buffer, sizeof(url_buffer), "%s/virtual-server/disk-usage/", fastapi_base_url);
            post_json_to_api(url_buffer, disk_json); 
        }

        // --- Status Virtual (Check and Update/Insert) ---
        char status_json[512];
        int len_status = snprintf(status_json, sizeof(status_json),
                                  "{\"TANK_LOCATION\": \"%s\", "
                                  "\"TANK_NUM\": \"%s\", "
                                  "\"SERVER_NUM\": \"%s\", "
                                  "\"SERVER_VIRTUAL_NAME\": \"%s\", "
                                  "\"status\": \"%s\", "
                                  "\"Timestamp\": \"%s\"}",
                                  tank_location,
                                  tank_id,
                                  physical_server_id,
                                  Name,
                                  Status,
                                  timestamp);
        if (len_status < 0 || len_status >= sizeof(status_json)) { fprintf(stderr, "[ERROR] Status virtual JSON too large.\n"); }
        else { 
            char url_buffer[256];
            snprintf(url_buffer, sizeof(url_buffer), "%s/virtual-server/status/", fastapi_base_url);
            post_json_to_api(url_buffer, status_json); 
        }
    }
    pclose(fp);
}

void insert_disk_stats_via_api() // Modified signature
{
    printf("[DEBUG] Starting insert_disk_stats\n");

    FILE *fp = popen("pvesm status", "r");

    if (!fp)
    {
        fprintf(stderr, "[ERROR] Failed to run pvesm status command\n");
        return;
    }

    char line[512];
    int count = 0;
    fgets(line, sizeof(line), fp); // Read header line
    while (fgets(line, sizeof(line), fp))
    {
        printf("[DEBUG] pvesm status line: %s", line);

        char disk_name[64], type[32], status[32], size_str[32], used_str[32], avail_str[32], percent_used[32];
        int matched = sscanf(line, "%63s %31s %31s %31s %31s %31s %31s", disk_name, type, status, size_str, used_str, avail_str, percent_used);

        if (matched == 7)
        {
            if (strcmp(size_str, "N/A") == 0 || strcmp(used_str, "N/A") == 0 || strcmp(avail_str, "N/A") == 0 || strcmp(disk_name, "N/A") == 0 || strcmp(percent_used, "N/A") == 0)
            {
                printf("[INFO] Skipping %s due to unavailable data\n", disk_name);
                continue;
            }
            
            char json_payload[1024]; // Larger buffer for disk stats
            int len = snprintf(json_payload, sizeof(json_payload),
                               "{\"TANK_LOCATION\": \"%s\", "
                               "\"TANK_ID\": \"%s\", "
                               "\"SERVER_ID\": \"%s\", "
                               "\"disk_name\": \"%s\", "
                               "\"total_bytes\": \"%s\", "
                               "\"used_bytes\": \"%s\", "
                               "\"available_bytes\": \"%s\", "
                               "\"percent_used\": \"%s\", "
                               "\"Timestamp\": \"%s\"}",
                               tank_location,
                               tank_id,
                               physical_server_id,
                               disk_name,
                               size_str,
                               used_str,
                               avail_str,
                               percent_used,
                               timestamp);

            if (len < 0 || len >= sizeof(json_payload)) {
                fprintf(stderr, "[ERROR] JSON payload for disk stats too large or error occurred.\n");
            } else {
                char url_buffer[256];
                snprintf(url_buffer, sizeof(url_buffer), "%s/physical-server/disk-usage/", fastapi_base_url);
                post_json_to_api(url_buffer, json_payload);
                count++;
            }
        }
        else
        {
            printf("[WARN] Failed to parse pvesm status line: %s", line);
        }
    }

    if (count == 0)
    {
        printf("[WARN] No disk stats were inserted via API. Consider adjusting the filter or command output.\n");
    }

    pclose(fp);
    printf("[DEBUG] Completed insert_disk_stats\n");
}

void insert_cpu_usage_via_api() // Modified signature
{
    printf("[DEBUG] Starting insert_cpu_usage\n");

    // Step 1: Get number of cores
    FILE *core_fp = popen("nproc", "r");
    if (!core_fp)
    {
        fprintf(stderr, "[ERROR] Failed to get CPU core count\n");
        return;
    }
    int cores = 0;
    fscanf(core_fp, "%d", &cores);
    pclose(core_fp);
    if (cores <= 0)
    {
        fprintf(stderr, "[ERROR] Invalid number of CPU cores: %d\n", cores);
        return;
    }

    // Step 2: Run mpstat and calculate summed CPU usage
    FILE *fp = popen("mpstat -P ALL 1 1 | awk '/Average/ && $2 ~ /[0-9]+/ {sum += 100 - $NF} END {print sum}'", "r");
    if (!fp)
    {
        fprintf(stderr, "[ERROR] Failed to run mpstat\n");
        return;
    }

    float total_cpu_used = 0.0;
    fscanf(fp, "%f", &total_cpu_used);
    pclose(fp);

    float percent_used = total_cpu_used / (float)cores;
    printf("[DEBUG] CPU Usage = %.2f%% of %d cores\n", percent_used, cores);

    // Step 3: Get load average
    char loadavg[64] = {0};
    fp = popen("uptime | awk -F'load average: ' '{print $2}'", "r");
    if (!fp)
    {
        fprintf(stderr, "[ERROR] Failed to get load average\n");
        return;
    }
    fgets(loadavg, sizeof(loadavg), fp);
    loadavg[strcspn(loadavg, "\n")] = 0; // remove newline
    pclose(fp);

    printf("[DEBUG] Load average: %s\n", loadavg);

    // Step 4: Construct JSON and send to API
    char json_payload[512];
    int len = snprintf(json_payload, sizeof(json_payload),
                       "{\"TANK_LOCATION\": \"%s\", "
                       "\"TANK_ID\": \"%s\", "
                       "\"SERVER_ID\": \"%s\", "
                       "\"cpu_percent_used\": %.2f, "
                       "\"logical_cores\": %d, "
                       "\"load_avarage\": \"%s\", " // Note: model has load_avarage, not load_average
                       "\"Timestamp\": \"%s\"}",
                       tank_location,
                       tank_id,
                       physical_server_id,
                       percent_used,
                       cores,
                       loadavg,
                       timestamp);

    if (len < 0 || len >= sizeof(json_payload)) {
        fprintf(stderr, "[ERROR] JSON payload for CPU usage too large or error occurred.\n");
    } else {
        char url_buffer[256];
        snprintf(url_buffer, sizeof(url_buffer), "%s/physical-server/cpu-usage/", fastapi_base_url);
        post_json_to_api(url_buffer, json_payload);
    }
    printf("[DEBUG] Completed insert_cpu_usage\n");
}

// REVISED: This function now sends data to FastAPI for insertion, and FastAPI handles the check.
void insert_Common_info_via_api() {
    printf("[DEBUG] Starting insert_Common_info_via_api\n");

    char ipconf[64] = {0};
    SensorData* sensor_data = NULL;

    // Get Server IP
    FILE *fp_ipconf = popen("ip -4 addr show vmbr0 | grep -oP 'inet \\K[\\d.]+'", "r");
    if (fp_ipconf && fgets(ipconf, sizeof(ipconf), fp_ipconf)) {
        ipconf[strcspn(ipconf, "\n")] = 0;
        printf("[DEBUG] Server IP: %s\n", ipconf);
    } else {
        fprintf(stderr, "[ERROR] Failed to get server IP or command failed.\n");
        strncpy(ipconf, "N/A", sizeof(ipconf) - 1);
    }
    if (fp_ipconf) pclose(fp_ipconf);

    // Get Sensor Data
    sensor_data = parse_sensors_output();
    if (!sensor_data) {
        fprintf(stderr, "[ERROR] Failed to parse sensor data. Using defaults.\n");
    }

    // Construct socket_names array string
    char sockets_str[MAX_LINE_LENGTH * MAX_ITEMS + 50] = "[";
    size_t socket_offset = strlen(sockets_str);
    if (sensor_data) {
        for (int i = 0; i < sensor_data->num_sockets; i++) {
            socket_offset += snprintf(sockets_str + socket_offset, sizeof(sockets_str) - socket_offset,
                                      "\"%s\"%s", sensor_data->socket_names[i],
                                      (i < sensor_data->num_sockets - 1) ? ", " : "");
        }
    }
    snprintf(sockets_str + socket_offset, sizeof(sockets_str) - socket_offset, "]");

    // Construct core_names array string
    char cores_str[MAX_LINE_LENGTH * MAX_ITEMS + 50] = "[";
    size_t core_offset = strlen(cores_str);
    if (sensor_data) {
        for (int i = 0; i < sensor_data->num_cores; i++) {
            core_offset += snprintf(cores_str + core_offset, sizeof(cores_str) - core_offset,
                                    "\"%s\"%s", sensor_data->core_names[i],
                                    (i < sensor_data->num_cores - 1) ? ", " : "");
        }
    }
    snprintf(cores_str + core_offset, sizeof(cores_str) - core_offset, "]");

    const char *adapter_name_val = (sensor_data && sensor_data->adapter_name && sensor_data->adapter_name[0] != '\0')
                                    ? sensor_data->adapter_name : "N/A";

    // Build JSON payload
    char json_payload[4096];
    int len = snprintf(json_payload, sizeof(json_payload),
                       "{\"TANK_LOCATION\": \"%s\", "
                       "\"TANK_ID\": \"%s\", "
                       "\"SERVER_IP\": \"%s\", "
                       "\"SERVER_ID\": \"%s\", "
                       "\"socket_names\": %s, "
                       "\"core_names\": %s, "
                       "\"adapter_name\": \"%s\"}",
                       tank_location,
                       tank_id,
                       ipconf,
                       physical_server_id,
                       sockets_str,
                       cores_str,
                       adapter_name_val);

    if (len < 0 || len >= sizeof(json_payload)) {
        fprintf(stderr, "[ERROR] JSON payload for Common_info too large or error occurred.\n");
    } else {
        char url_buffer[100000];
        snprintf(url_buffer, sizeof(url_buffer), "%s/physical-server/general-info/", fastapi_base_url);

        printf("[DEBUG] Constructed URL: %s\n", url_buffer);
        printf("[DEBUG] Sending Common_info JSON to %s\n", url_buffer);
        fflush(stdout);

        post_json_to_api(url_buffer, json_payload);
    }

    if (sensor_data) free_sensor_data(sensor_data);
    printf("[DEBUG] Completed insert_Common_info_via_api\n");
}


void Setenv() {

    const char *env_server_id = getenv("SERVER_OBJECT_ID");
    if (env_server_id != NULL) {
        physical_server_id = strdup(env_server_id);
        if (physical_server_id == NULL) {
            perror("strdup failed for SERVER_OBJECT_ID");
            exit(EXIT_FAILURE);
        }
    } else {
        fprintf(stderr, "[ERROR] SERVER_OBJECT_ID environment variable not set. Exiting.\n");
        exit(EXIT_FAILURE);
    }

    const char *env_tank_id = getenv("TANK_ID");
    if (env_tank_id != NULL) {
        tank_id = strdup(env_tank_id);
        if (tank_id == NULL) {
            perror("strdup failed for TANK_ID");
            exit(EXIT_FAILURE);
        }
    } else {
        fprintf(stderr, "[ERROR] TANK_ID environment variable not set. Exiting.\n");
        exit(EXIT_FAILURE);
    }

    const char *env_tank_location = getenv("TANK_LOCATION");
    if (env_tank_location != NULL){ 
        tank_location = strdup(env_tank_location);
        if (tank_location ==NULL){
            perror("strdup failed for Tank_Location");
            exit(EXIT_FAILURE);
        }
    }else {
        fprintf(stderr, "[ERROR] TANK_LOCATION environment variable not set. Exiting.\n");
        exit(EXIT_FAILURE);
    }

    const char *env_fastapi_url = getenv("FASTAPI_BASE_URL");
    if (env_fastapi_url != NULL) {
        fastapi_base_url = strdup(env_fastapi_url);
        if (fastapi_base_url == NULL) {
            perror("strdup failed for FASTAPI_BASE_URL");
            exit(EXIT_FAILURE);
        }
    } else {
        fprintf(stderr, "[ERROR] FASTAPI_BASE_URL environment variable not set. Exiting.\n");
        exit(EXIT_FAILURE);
    }
 // Still prints if mongo_client_ip is kept for info
    printf("[DEBUG] SERVER_OBJECT_ID: %s\n", physical_server_id);
    printf("[DEBUG] TANK_ID: %s\n", tank_id);
    printf("[DEBUG] TANK_LOCATION: %s\n", tank_location);
    printf("[DEBUG] FASTAPI_BASE_URL: %s\n", fastapi_base_url);
}

int main()
{
    printf("C file has been updated three times\n");
    // Initialize libcurl global state
    get_iso_timestamp();
    curl_global_init(CURL_GLOBAL_DEFAULT);
    // mongoc_init(); // REMOVED: No longer needed as mongoc is not used directly

    Setenv(); // Load all environment variables
    if (!fastapi_base_url || strlen(fastapi_base_url) == 0) {
        fprintf(stderr, "[ERROR] FASTAPI_BASE_URL is empty or not set.\n");
    }
    printf("Start Inserting Data in MongoDB\n");
    // REPLACED: Call the new API function for general info
    insert_Common_info_via_api();

    // Call the new API-based functions
    insert_temperatures();
    insert_ram_usage_via_api();
    insert_disk_stats_via_api();
    insert_cpu_usage_via_api();
    insert_virtual_stats_via_api();

    // REMOVED: MongoDB cleanup
    /*
    if (general_info_coll) mongoc_collection_destroy(general_info_coll);
    if (client) mongoc_client_destroy(client);
    mongoc_cleanup(); // Clean up mongoc global state
    */

    // Free the strdup'd environment variables // Keep if mongo_client_ip is still part of Setenv and strdup'd
    free(physical_server_id);
    free(tank_id);
    free(tank_location);
    free(fastapi_base_url);

    // Clean up libcurl global state
    curl_global_cleanup();
    return EXIT_SUCCESS;
}
