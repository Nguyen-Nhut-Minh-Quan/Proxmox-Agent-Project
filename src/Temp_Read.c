#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <mongoc/mongoc.h>
#include <libusbtc08/usbtc08.h>

char *NUM_TANK = NULL;
char *mongo_client_ip =NULL;
#define NUM_LAYERS 3
#define CHANNEL_OFFSET 1  // Skip channel 0 (cold junction)
//gcc Temp_Read.c -o main     -I/opt/picoscope/include     -L/opt/picoscope/lib -lusbtc08     $(pkg-config --cflags --libs libmongoc-1.0)
void get_iso_timestamp(char *buffer, size_t size) {
    time_t now = time(NULL);
    strftime(buffer, size, "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
}

void insert_tank_temperature(mongoc_collection_t *col) {
    printf("Connecting to TC-08...\n");
    int handle = usb_tc08_open_unit();
    if (handle <= 0) {
        printf("TC-08 Connection Failed (code: %d)\n", handle);
        return;
    }
    int num = atoi(NUM_TANK);
    // Set K type for channels 1-3
    for (int i = 1; i <= num * NUM_LAYERS; i++) {
        usb_tc08_set_channel(handle, i, 'K');
    }

    sleep(1); // Allow time for setup

    float temperat[9];  // Read all 9 channels (0 = cold junction)
    short overflow;
    short nTemps = usb_tc08_get_single(handle, temperat, &overflow, 9);

    if (nTemps <= 0) {
        printf("Temperature read failed (code: %d)\n", nTemps);
        usb_tc08_close_unit(handle);
        return;
    }

    // Print all raw channels for debugging
    printf("Raw channel readings:\n");
    for (int i = 0; i < 9; i++) {
        printf("  Channel %d: %.2f °C\n", i, temperat[i]);
    }

    char timestamp[64];
    get_iso_timestamp(timestamp, sizeof(timestamp));
    // Insert for each tank
    for (int tank = 0; tank < num; tank++) {
        int base = tank * NUM_LAYERS + CHANNEL_OFFSET;  // Start from channel 1

        printf("Tank %d processed readings:\n", tank + 1);
        printf("  Layer 1: %.2f °C\n", temperat[base + 0]);
        printf("  Layer 2: %.2f °C\n", temperat[base + 1]);
        printf("  Layer 3: %.2f °C\n", temperat[base + 2]);

        bson_t *doc = bson_new();
        BSON_APPEND_INT32(doc, "TANK_NUM", tank + 1);
        BSON_APPEND_UTF8(doc, "Timestamp", timestamp);
        BSON_APPEND_DOUBLE(doc, "Layer-1-temp", temperat[base + 0]);
        BSON_APPEND_DOUBLE(doc, "Layer-2-temp", temperat[base + 1]);
        BSON_APPEND_DOUBLE(doc, "Layer-3-temp", temperat[base + 2]);

        bson_error_t error;
        if (!mongoc_collection_insert_one(col, doc, NULL, NULL, &error)) {
            fprintf(stderr, "MongoDB Insert Error: %s\n", error.message);
        } else {
            printf("Inserted data for tank %d successfully.\n", tank + 1);
        }

        bson_destroy(doc);
    }

    usb_tc08_close_unit(handle);
    printf("Finished logging.\n");
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
    const char *env_tank_id = getenv("TANK_ID");
    if (env_tank_id != NULL) {
        NUM_TANK = strdup(env_tank_id);
        if (NUM_TANK == NULL) {
            perror("strdup failed for NUM_TANK");
            exit(EXIT_FAILURE);
        }
    } else {
        fprintf(stderr, "[ERROR] TANK_ID environment variable not set. Exiting.\n");
        exit(EXIT_FAILURE);
    }

    printf("[DEBUG] MONGO_CLIENT_IP: %s\n", mongo_client_ip);
    printf("[DEBUG] TANK_ID: %s\n", NUM_TANK);
}
int main() {
    Setenv();   
    mongoc_init();
    mongoc_client_t *client = mongoc_client_new(mongo_client_ip);

    if (!client) {
        fprintf(stderr, "Failed to connect to MongoDB at %s\n", mongo_client_ip);
        return EXIT_FAILURE;
    }

    mongoc_collection_t *temperature =
        mongoc_client_get_collection(client, "Tank_stats", "temperature");

    printf("Starting temperature capture and insert...\n");
    insert_tank_temperature(temperature);

    mongoc_collection_destroy(temperature);
    mongoc_client_destroy(client);
    mongoc_cleanup();

    return EXIT_SUCCESS;
}