#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <mongoc/mongoc.h>
#include <regex.h>
#define MAX_LINE_LENGTH 256
#define MAX_ITEMS 32 
#define TEMP_ADAPTER_NAME "Coretemp Adapter"
char *mongo_client_ip = NULL;
char *physical_server_id = NULL;
char *tank_id = NULL;
//gcc -g -o test test.c $(pkg-config --cflags --libs libmongoc-1.0)
//path to service or timer : /etc/systemd/system/...
//chmod +x VirtualServerStat.sh
void get_iso_timestamp(char *buffer, size_t size)
{
    time_t now = time(NULL);
    strftime(buffer, size, "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
}
// Structure to hold the parsed sensor data
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
// Returns true if added, false if already exists or allocation failed
bool add_unique_string(char*** array_ptr, int* count_ptr, const char* new_str) {
    // Check if string already exists
    for (int i = 0; i < *count_ptr; i++) {
        if (strcmp((*array_ptr)[i], new_str) == 0) {
            return false; // Already exists
        }
    }

    // Allocate memory for the new string pointer in the array
    char** temp_array = realloc(*array_ptr, (*count_ptr + 1) * sizeof(char*));
    if (temp_array == NULL) {
        perror("Failed to reallocate array for strings");
        return false;
    }
    *array_ptr = temp_array;

    // Allocate memory for the string itself and copy it
    (*array_ptr)[*count_ptr] = strdup(new_str);
    if ((*array_ptr)[*count_ptr] == NULL) {
        perror("Failed to duplicate string");
        // Revert realloc if strdup fails
        if (*count_ptr == 0) {
            free(*array_ptr);
            *array_ptr = NULL;
        } else {
            realloc(*array_ptr, (*count_ptr) * sizeof(char*)); // Try to shrink back
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
    // Initialize members
    data->adapter_name = NULL;
    data->socket_names = NULL;
    data->num_sockets = 0;
    data->core_names = NULL;
    data->num_cores = 0;

    // Open the sensors command for reading
    fp = popen("sensors", "r");
    if (fp == NULL) {
        perror("Failed to run command 'sensors'");
        free(data);
        return NULL;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        // Remove trailing newline character
        line[strcspn(line, "\n")] = 0;

        // --- Parse Adapter Name ---
        if (data->adapter_name == NULL && strstr(line, "Adapter:") == line) {
            char* adapter_start = strstr(line, ":");
            if (adapter_start) {
                // Skip ": " and any leading whitespace
                adapter_start += 2;
                while (*adapter_start == ' ' || *adapter_start == '\t') {
                    adapter_start++;
                }
                data->adapter_name = strdup(adapter_start);
                if (data->adapter_name == NULL) {
                    perror("Failed to duplicate adapter name");
                    pclose(fp);
                    // Free partially allocated data before returning
                    // (detailed cleanup handled by free_sensor_data)
                    free_sensor_data(data);
                    return NULL;
                }
            }
        }

        // --- Parse Socket Names ---
        // Socket names are lines that start with "coretemp-isa-"
        if (strstr(line, "coretemp-isa-") == line) {
            char socket_name[MAX_LINE_LENGTH];
            if (sscanf(line, "%s", socket_name) == 1) {
                add_unique_string(&(data->socket_names), &(data->num_sockets), socket_name);
            }
        }

        // --- Parse Core Names ---
        // Core names are lines that start with "Core " followed by a number and a colon
        if (strstr(line, "Core ") == line && strchr(line, ':') != NULL) {
            char core_part[MAX_LINE_LENGTH];
            // Find the position of the colon
            char* colon_pos = strchr(line, ':');
            if (colon_pos) {
                // Copy characters from the start of the line up to the colon
                size_t len = colon_pos - line;
                if (len < sizeof(core_part)) {
                    strncpy(core_part, line, len);
                    core_part[len] = '\0'; // Null-terminate the string
                    add_unique_string(&(data->core_names), &(data->num_cores), core_part);
                }
            }
        }
    }
    pclose(fp);
    return data;
}

// Function to free the memory allocated for SensorDat
// Insert temperature per core
void insert_temperature(mongoc_collection_t *collection, const char *core, float temp, const char *chip_name)
{
    char timestamp[64];
    get_iso_timestamp(timestamp, sizeof(timestamp));

    bson_t *doc = bson_new();
    BSON_APPEND_UTF8(doc, "SERVER_ID", physical_server_id);
    BSON_APPEND_UTF8(doc, "core", core);
    BSON_APPEND_DOUBLE(doc, "temperature_celsius", temp);
    BSON_APPEND_UTF8(doc, "adapter", chip_name); // Now using coretemp-isa-xxxx
    BSON_APPEND_UTF8(doc, "timestamp", timestamp);
    BSON_APPEND_UTF8(doc, "tank_id", tank_id);
    bson_error_t error;
    if (!mongoc_collection_insert_one(collection, doc, NULL, NULL, &error))
    {
        fprintf(stderr, "[ERROR] Temperature insert failed: %s\n", error.message);
    }
    bson_destroy(doc);
}

void insert_temperatures(mongoc_collection_t *collection)
{
    printf("[DEBUG] Starting insert_temperatures\n");
    FILE *fp = popen("sensors", "r");
    if (!fp)
    {
        fprintf(stderr, "[ERROR] Failed to run sensors command\n");
        return;
    }

    char line[256];
    char current_chip[32] = {0}; // To store e.g. coretemp-isa-0000
    regex_t chip_regex, temp_regex;
    regmatch_t matches[3];

    // Match chip line: e.g. coretemp-isa-0000
    const char *chip_pattern = "^(coretemp-isa-[0-9]+)";
    // Match temperature line: e.g. Core 0: +35.0°C
    const char *temp_pattern = "Core ([0-9]+):[[:space:]]+\\+([0-9]+\\.[0-9])°C";

    if (regcomp(&chip_regex, chip_pattern, REG_EXTENDED) != 0 ||
        regcomp(&temp_regex, temp_pattern, REG_EXTENDED) != 0)
    {
        fprintf(stderr, "[ERROR] Failed to compile regex\n");
        pclose(fp);
        return;
    }

    while (fgets(line, sizeof(line), fp))
    {
        // Check if the line contains a new chip ID
        if (regexec(&chip_regex, line, 2, matches, 0) == 0)
        {
            int len = matches[1].rm_eo - matches[1].rm_so;
            strncpy(current_chip, line + matches[1].rm_so, len);
            current_chip[len] = '\0';
            continue;
        }

        // Check if line contains temperature info
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

            // Set TEMP_ADAPTER_NAME to current_chip (or pass it as parameter)
            insert_temperature(collection, core, temp, current_chip);
        }
    }
    regfree(&chip_regex);
    regfree(&temp_regex);
    pclose(fp);
    printf("[DEBUG] Completed insert_temperatures\n");
}

void insert_ram_usage(mongoc_collection_t *collection)
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

    char timestamp[64];
    get_iso_timestamp(timestamp, sizeof(timestamp));
    bson_t *doc = bson_new();
    BSON_APPEND_UTF8(doc, "SERVER_ID", physical_server_id);
    BSON_APPEND_INT32(doc, "total_ram_mb", total);
    BSON_APPEND_INT32(doc, "used_ram_mb", used);
    BSON_APPEND_INT32(doc, "free_ram_mb", free_mem);
    BSON_APPEND_UTF8(doc, "timestamp", timestamp);
    BSON_APPEND_UTF8(doc, "tank_id", tank_id);
    
    bson_error_t error;
    if (!mongoc_collection_insert_one(collection, doc, NULL, NULL, &error))
    {
        fprintf(stderr, "[ERROR] RAM insert failed: %s\n", error.message);
    }

    bson_destroy(doc);
    printf("[DEBUG] Completed insert_ram_usage\n");
}
void insert_virtual_stats(mongoc_collection_t *disk_collection, mongoc_collection_t *ram_collection, mongoc_collection_t *status_collection, mongoc_collection_t *cpu_collection)
{
    FILE *fp = popen("./VirtualServerStat.sh", "r");
    if (!fp)
    {
        printf("Cannot read the command \n");
        return;
    }
    char line[512];
    fgets(line, sizeof(line), fp);
    while (fgets(line, sizeof(line), fp))
    {
        char VMID[20];
        char Name[52];
        char Status[20];
        char CPU[50];
        char Ram[50];
        char Disk[50];
        bson_error_t error;
        int match = sscanf(line, "%19s %51s %19s %49s %49s %49s", VMID, Name, Status, CPU, Ram, Disk);
        if (match != 6)
        {
            printf("Some Data point does not match\n");
            return;
        }
        float percent;
        int numCores; // remove newline or carriage return if present
        int check = sscanf(CPU, "%f/%d", &percent, &numCores);
        char timestamp[64];
        get_iso_timestamp(timestamp, sizeof(timestamp));
        bson_t *cpu_doc = bson_new();
        BSON_APPEND_UTF8(cpu_doc, "SERVER_NUM", physical_server_id);
        BSON_APPEND_UTF8(cpu_doc, "SERVER_VIRTUAL_NAME", Name);
        BSON_APPEND_DOUBLE(cpu_doc, "CPU_USAGE(%)", percent);
        BSON_APPEND_UTF8(cpu_doc,"TANK_NUM", tank_id);
        BSON_APPEND_INT32(cpu_doc, "NUM_CORES", numCores);
        BSON_APPEND_UTF8(cpu_doc, "Timestamp", timestamp);
        if (!mongoc_collection_insert_one(cpu_collection, cpu_doc, NULL, NULL, &error))
        {
            fprintf(stderr, "[ERROR] CPU VIRTUAL insert failed: %s\n", error.message);
        }
        bson_t *ram_doc = bson_new();
        // printf("The Line for Ram is %s",Ram);
        BSON_APPEND_UTF8(ram_doc, "SERVER_NUM", physical_server_id);
        BSON_APPEND_UTF8(ram_doc, "SERVER_VIRTUAL_NAME", Name);
        BSON_APPEND_UTF8(ram_doc, "Ram_Usage(MB)", Ram);
        BSON_APPEND_UTF8(ram_doc, "Timestamp", timestamp);
        BSON_APPEND_UTF8(ram_doc,"TANK_NUM", tank_id);
        if (!mongoc_collection_insert_one(ram_collection, ram_doc, NULL, NULL, &error))
        {
            fprintf(stderr, "[ERROR] RAM VIRTUAL insert failed: %s\n", error.message);
        }
        bson_t *disk_doc = bson_new();
        BSON_APPEND_UTF8(disk_doc, "SERVER_NUM", physical_server_id);
        BSON_APPEND_UTF8(disk_doc, "SERVER_VIRTUAL_NAME", Name);
        BSON_APPEND_UTF8(disk_doc, "Disk_Usage(GB)", Disk);
        BSON_APPEND_UTF8(disk_doc, "Timestamp", timestamp);
        BSON_APPEND_UTF8(disk_doc,"TANK_NUM", tank_id);
        if (!mongoc_collection_insert_one(disk_collection, disk_doc, NULL, NULL, &error))
        {
            fprintf(stderr, "[ERROR] DISK VIRTUAL insert failed: %s\n", error.message);
        }
        bson_t *filter = BCON_NEW("SERVER_VIRTUAL_NAME", BCON_UTF8(Name));
        mongoc_cursor_t *cursor = mongoc_collection_find_with_opts(status_collection, filter, NULL, NULL);
        const bson_t *status_doc;
        bson_iter_t iter;
        char status_old[20] = "";
        bool status_changed = true;

        if (mongoc_cursor_next(cursor, &status_doc))
        {
            if (bson_iter_init_find(&iter, status_doc, "status") && BSON_ITER_HOLDS_UTF8(&iter))
            {
                strncpy(status_old, bson_iter_utf8(&iter, NULL), sizeof(status_old));
                if (strcmp(status_old, Status) == 0)
                {
                    status_changed = false; // No change
                }
            }
        }

        mongoc_cursor_destroy(cursor);
        bson_destroy(filter);

        // Only update if status has changed
        if (status_changed)
        {
            bson_t *filter2 = BCON_NEW("SERVER_VIRTUAL_NAME", BCON_UTF8(Name));
            bson_t *update = BCON_NEW(
                "$set", "{",
                "SERVER_VIRTUAL_NAME", BCON_UTF8(Name),
                "SERVER_NUM", BCON_UTF8(physical_server_id),
                "status", BCON_UTF8(Status),
                "timestamp", BCON_UTF8(timestamp),
                "TANK_NUM", BCON_UTF8(tank_id),
                "}");
            bson_t *opts = BCON_NEW("upsert", BCON_BOOL(true));

            if (!mongoc_collection_update_one(status_collection, filter2, update, opts, NULL, &error))
            {
                fprintf(stderr, "[ERROR] Status update failed: %s\n", error.message);
            }

            bson_destroy(filter2);
            bson_destroy(update);
            bson_destroy(opts);
        }
    }
}
void insert_disk_stats(mongoc_collection_t *collection)
{
    printf("[DEBUG] Starting insert_disk_stats\n");

    // Updated command to capture all /dev/ devices
    FILE *fp = popen("pvesm status", "r");

    if (!fp)
    {
        fprintf(stderr, "[ERROR] Failed to run df\n");
        return;
    }

    char line[512];
    char timestamp[64];
    get_iso_timestamp(timestamp, sizeof(timestamp));
    int count = 0;
    fgets(line, sizeof(line), fp);
    while (fgets(line, sizeof(line), fp))
    {
        printf("[DEBUG] df line: %s", line);

        char disk_name[64], type[32], status[32], size_str[32], used_str[32], avail_str[32], percent_used[32];
        int matched = sscanf(line, "%63s %31s %31s %31s %31s %31s %31s", disk_name, type, status, size_str, used_str, avail_str, percent_used);

        if (matched == 7)
        {
            if (strcmp(size_str, "N/A") == 0 || strcmp(used_str, "N/A") == 0 || strcmp(avail_str, "N/A") == 0 || strcmp(disk_name, "N/A") == 0 || strcmp(percent_used, "N/A") == 0)
            {
                printf("[INFO] Skipping %s due to unavailable data\n", disk_name);
                continue;
            }
            bson_t *doc = bson_new();
            BSON_APPEND_UTF8(doc, "SERVER_ID", physical_server_id);
            BSON_APPEND_UTF8(doc, "disk_name", disk_name);
            BSON_APPEND_UTF8(doc, "total_bytes", size_str);
            BSON_APPEND_UTF8(doc, "used_bytes", used_str);
            BSON_APPEND_UTF8(doc, "available_bytes", avail_str);
            BSON_APPEND_UTF8(doc, "percent_used", percent_used);
            BSON_APPEND_UTF8(doc, "timestamp", timestamp);
            BSON_APPEND_UTF8(doc, "tank_id",tank_id);
            bson_error_t error;
            if (!mongoc_collection_insert_one(collection, doc, NULL, NULL, &error))
            {
                fprintf(stderr, "[ERROR] Disk stats insert failed: %s\n", error.message);
            }

            bson_destroy(doc);
            count++;
        }
        else
        {
            printf("[WARN] Failed to parse df line: %s", line);
        }
    }

    if (count == 0)
    {
        printf("[WARN] No disk stats were inserted. Consider adjusting the filter.\n");
    }

    pclose(fp);
    printf("[DEBUG] Completed insert_disk_stats\n");
}
void insert_cpu_usage(mongoc_collection_t *collection)
{
    printf("[DEBUG] Starting insert_cpu_usage\n");

    char timestamp[64];
    get_iso_timestamp(timestamp, sizeof(timestamp));

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

    // Step 4: Insert into MongoDB
    bson_t *doc = bson_new();
    BSON_APPEND_UTF8(doc, "SERVER_ID", physical_server_id);
    BSON_APPEND_DOUBLE(doc, "cpu_percent_used", percent_used);
    BSON_APPEND_INT32(doc, "logical_cores", cores);
    BSON_APPEND_UTF8(doc, "load_average", loadavg);
    BSON_APPEND_UTF8(doc, "timestamp", timestamp);
    BSON_APPEND_UTF8(doc, "tank_id", tank_id);

    bson_error_t error;
    if (!mongoc_collection_insert_one(collection, doc, NULL, NULL, &error))
    {
        fprintf(stderr, "[ERROR] CPU usage insert failed: %s\n", error.message);
    }

    bson_destroy(doc);
    printf("[DEBUG] Completed insert_cpu_usage\n");
}
void insert_Common_info(mongoc_collection_t *col){
    // --- Phase 1: Gather All Data ---

    char ipconf[64];
    SensorData* sensor_data = NULL;
    
    // Get Server IP
    FILE *fp_ipconf = popen("ip -4 addr show vmbr0 | grep -oP 'inet \\K[\\d.]+'","r");
    fscanf(fp_ipconf, "%s", &ipconf);
    if (!ipconf) {
        fprintf(stderr, "Failed to get server IP or command failed. Aborting.\n");
        return;
    }

    // Get Sensor Data (Sockets, Cores, Adapter)
    sensor_data = parse_sensors_output();
    if (sensor_data == NULL) {
        fprintf(stderr, "Failed to parse sensor data. Aborting.\n");
        return;
    }

    // --- Placeholder values for other metrics (replace with actual data collection) ---
    // You would integrate functions here to get these values (e.g., from /proc/stat, /proc/loadavg)


    // --- Phase 2: Build BSON Document (all appends at once) ---
    bson_t *doc = bson_new();
    if (doc == NULL) {
        fprintf(stderr, "Failed to create new BSON document. Aborting.\n");
        return;
    }

    // Append core common info
    BSON_APPEND_UTF8(doc, "SERVER_ID", physical_server_id);
    BSON_APPEND_UTF8(doc, "SERVER_IP", ipconf);
    BSON_APPEND_UTF8(doc, "tank_id", tank_id);

    // Append Socket Names as a BSON array
    bson_t sockets_array;
    BSON_APPEND_ARRAY_BEGIN(doc, "socket_names", &sockets_array);
    for (int i = 0; i < sensor_data->num_sockets; i++) {
        char index_str[16];
        bson_snprintf(index_str, sizeof(index_str), "%d", i);
        BSON_APPEND_UTF8(&sockets_array, index_str, sensor_data->socket_names[i]);
    }
    bson_append_array_end(doc, &sockets_array);

    // Append Core Names as a BSON array
    bson_t cores_array;
    BSON_APPEND_ARRAY_BEGIN(doc, "core_names", &cores_array);
    for (int i = 0; i < sensor_data->num_cores; i++) {
        char index_str[16];
        bson_snprintf(index_str, sizeof(index_str), "%d", i);
        BSON_APPEND_UTF8(&cores_array, index_str, sensor_data->core_names[i]);
    }
    bson_append_array_end(doc, &cores_array);

    // Append Adapter Name
    if (sensor_data->adapter_name[0] != '\0') { // Check if populated
        BSON_APPEND_UTF8(doc, "adapter_name", sensor_data->adapter_name);
    } else {
        BSON_APPEND_UTF8(doc, "adapter_name", "N/A"); // Default if not found
    }


    // --- Phase 3: Insert into MongoDB ---
    bson_error_t error;
    if (!mongoc_collection_insert_one(col, doc, NULL, NULL, &error)) {
        fprintf(stderr, "Failed to insert document: %s\n", error.message);
    } else {
        printf("Common_info document inserted successfully.\n");
    }

    // --- Phase 4: Clean up allocated memory ---
    bson_destroy(doc);
}
int has_doc(mongoc_collection_t *collection) {
    int result = 0;
    bson_t query;
    const bson_t *doc;

    bson_init(&query);

    // Correct type for the cursor
    mongoc_cursor_t *cursor = mongoc_collection_find_with_opts(collection, &query, NULL, NULL);

    if (mongoc_cursor_next(cursor, &doc)) {
        result = 1;
    }

    mongoc_cursor_destroy(cursor);
    bson_destroy(&query);
    return result;
}
void Setenv() {
    const char *env_mongo_ip = getenv("MONGO_CLIENT_IP");
    if (env_mongo_ip != NULL) {
        mongo_client_ip = strdup(env_mongo_ip);
        if (mongo_client_ip == NULL) {
            perror("strdup failed for MONGO_CLIENT_IP");
            // Consider exiting or setting a default if this is critical
            exit(EXIT_FAILURE); // Example: Crash if critical variable can't be set
        }
    } else {
        fprintf(stderr, "[ERROR] MONGO_CLIENT_IP environment variable not set. Exiting.\n");
        exit(EXIT_FAILURE); // Critical variable missing
    }

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

    printf("[DEBUG] MONGO_CLIENT_IP: %s\n", mongo_client_ip);
    printf("[DEBUG] SERVER_OBJECT_ID: %s\n", physical_server_id);
    printf("[DEBUG] TANK_ID: %s\n", tank_id);
}
int main()
{
    printf("[DEBUG] Initializing MongoDB client\n");
    mongoc_init();
    Setenv();
    mongoc_client_t *client = mongoc_client_new(mongo_client_ip);
    if (!client)
    {
        fprintf(stderr, "[ERROR] Failed to connect to MongoDB\n");
        return EXIT_FAILURE;
    }
    printf("[DEBUG] Connected to MongoDB\n");

    mongoc_collection_t *temp_coll = mongoc_client_get_collection(client, "performance_db", "temperature");
    mongoc_collection_t *ram_coll = mongoc_client_get_collection(client, "performance_db", "ram_usage");
    mongoc_collection_t *disk_coll = mongoc_client_get_collection(client, "performance_db", "disk_stats");
    mongoc_collection_t *cpu_coll = mongoc_client_get_collection(client, "performance_db", "storage_usage");
    mongoc_collection_t *cpu_virtual = mongoc_client_get_collection(client, "Virtual_Stats", "virtual_cpu_usage");
    mongoc_collection_t *ram_virtual = mongoc_client_get_collection(client, "Virtual_Stats", "virtual_ram_usage");
    mongoc_collection_t *disk_virtual = mongoc_client_get_collection(client, "Virtual_Stats", "virtual_disk_usage");
    mongoc_collection_t *status_virtual = mongoc_client_get_collection(client, "Virtual_Stats", "virtual_status");
    mongoc_collection_t *general_info = mongoc_client_get_collection(client,"performance_db","general_info");
    if (!has_doc(general_info)){
        printf("INSERTING GENERAL INFO\n");
        insert_Common_info(general_info);
    }
    insert_temperatures(temp_coll);
    insert_ram_usage(ram_coll);
    insert_disk_stats(disk_coll);
    insert_cpu_usage(cpu_coll);
    insert_virtual_stats(disk_virtual, ram_virtual, status_virtual, cpu_virtual); // disk,ram ,status,cpu
    mongoc_collection_destroy(temp_coll);
    mongoc_collection_destroy(ram_coll);
    mongoc_collection_destroy(disk_coll);
    mongoc_collection_destroy(cpu_coll);
    mongoc_client_destroy(client);
    mongoc_cleanup();

    printf("[DEBUG] Program completed\n");
    return EXIT_SUCCESS;
}