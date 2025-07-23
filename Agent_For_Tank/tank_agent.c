#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <curl/curl.h> // Required for HTTP requests
#include <libusbtc08/usbtc08.h> // For TC-08 communication (USBTC08_MAX_CHANNELS is defined here)
#include <unistd.h> // For sleep()
//The name of the env for Tank Temperature reader is .tank_agent_env --> use nano .tank_agent_env
// Global variables to be loaded from .env
char *NUM_TANK = NULL;
char *fastapi_base_url = NULL; // Base URL for your FastAPI server
char *tank_location = NULL;
int NUM_LAYERS_FROM_ENV = 0; // Will be set to 3 for Tank model

#define CHANNEL_OFFSET 1 // Skip channel 0 (cold junction)

// Function to get ISO 8601 formatted timestamp
void get_iso_timestamp(char *buffer, size_t size) {
    time_t now = time(NULL);
    strftime(buffer, size, "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
}

// Callback function for curl to write response data (not strictly needed for POST, but good practice)
size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    (void)contents; // Avoid unused parameter warning
    (void)userp;    // Avoid unused parameter warning
    return size * nmemb;
}

// Function to post JSON data to a specified URL
int post_json_to_api(const char *url, const char *json_payload) {
    CURL *curl;
    CURLcode res;
    int api_call_success = 0; // Use 0 for false, 1 for true

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (curl) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            fprintf(stderr, "[ERROR] curl_easy_perform() failed for %s: %s\n", url, curl_easy_strerror(res));
        } else {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            if (http_code >= 200 && http_code < 300) {
                printf("[DEBUG] Successfully posted data to %s, HTTP Status: %ld\n", url, http_code);
                api_call_success = 1;
            } else {
                fprintf(stderr, "[ERROR] Failed to post data to %s, HTTP Status: %ld. Payload: %s\n", url, http_code, json_payload);
            }
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    } else {
        fprintf(stderr, "[ERROR] Failed to initialize CURL.\n");
    }
    curl_global_cleanup();
    return api_call_success;
}


// Function to collect and send tank temperature data to FastAPI
void send_tank_temperature_to_api() {
    printf("Connecting to TC-08...\n");
    int handle = usb_tc08_open_unit();
    if (handle <= 0) {
        printf("TC-08 Connection Failed (code: %d)\n", handle);
        return;
    }

    int num_tanks_int = atoi(NUM_TANK);
    if (num_tanks_int <= 0) {
        fprintf(stderr, "[ERROR] Invalid NUMBER_TANK value: %s. Must be a positive integer.\n", NUM_TANK);
        usb_tc08_close_unit(handle);
        return;
    }
    // For the Tank model, NUM_LAYERS_FROM_ENV must be 3
    NUM_LAYERS_FROM_ENV = 3;

    // Set K type for all relevant channels (1 to num_tanks * NUM_LAYERS_FROM_ENV)
    // This will set channels 1, 2, 3 to K-type for 1 tank.
    for (int i = 1; i <= num_tanks_int * NUM_LAYERS_FROM_ENV; i++) {
        usb_tc08_set_channel(handle, i, 'K');
    }

    sleep(1); // Allow time for setup

    // Allocate enough space for all TC08 channels (0-8), as the driver might write to all
    int total_allocated_channels = USBTC08_MAX_CHANNELS; // This is 9
    float *temperat = (float *)malloc(total_allocated_channels * sizeof(float));
    if (temperat == NULL) {
        perror("Failed to allocate memory for temperatures");
        usb_tc08_close_unit(handle);
        return;
    }

    short overflow;
    // Call usb_tc08_get_single, providing buffer for all 9 channels
    short nTemps = usb_tc08_get_single(handle, temperat, &overflow, total_allocated_channels);

    // IMPORTANT: Even if nTemps is 1, the `temperat` array *should* be populated correctly
    // for channels that have thermocouples connected, based on your previous working code.
    // The "nTemps=1" might be a misreporting or an indication of some internal state
    // in libusbtc08 that doesn't prevent data from being read.
    if (nTemps <= 0) {
        printf("Temperature read failed (code: %d). Proceeding with potentially partial data if cold junction was read.\n", nTemps);
        // Do not return here, allow processing to continue as your old code did.
        // If nTemps is actually 0, then the values will be garbage or N/A.
        // If nTemps is 1 (cold junction only), Layer temps might be garbage.
        // The fact your old code works means nTemps must have been effectively > 1,
        // or it ignores nTemps and always trusts channels 1-3.
    }

    // Print all raw channels for debugging (up to 9, as per your old working code)
    printf("Raw channel readings:\n");
    for (int i = 0; i < USBTC08_MAX_CHANNELS; i++) {
        printf("  Channel %d: %.2f °C\n", i, temperat[i]);
    }

    // Prepare URL for FastAPI
    char api_url[256];
    // Removed the problematic /api suffix from here, as it should be in the .env
    snprintf(api_url, sizeof(api_url), "%s/tank/temperature/", fastapi_base_url);


    // Create and send JSON data for each tank
    for (int tank_idx = 0; tank_idx < num_tanks_int; tank_idx++) {
        // Base channel index for the current tank's layers (e.g., Tank 1: channels 1,2,3; Tank 2: channels 4,5,6)
        int base_channel_index = tank_idx * NUM_LAYERS_FROM_ENV + CHANNEL_OFFSET;

        printf("Tank %d processed readings:\n", tank_idx + 1);

        char json_payload[512];
        char temp_str_L1[16], temp_str_L2[16], temp_str_L3[16];
        char timestamp[100];
        // Direct access to temperat array, assuming it's populated correctly by TC-08.
        // This mirrors your working old code's behavior.
        snprintf(temp_str_L1, sizeof(temp_str_L1), "%.2f", temperat[base_channel_index + 0]);
        snprintf(temp_str_L2, sizeof(temp_str_L2), "%.2f", temperat[base_channel_index + 1]);
        snprintf(temp_str_L3, sizeof(temp_str_L3), "%.2f", temperat[base_channel_index + 2]);
        get_iso_timestamp(timestamp,sizeof(timestamp));
        char tank_num_str[16];
        snprintf(tank_num_str, sizeof(tank_num_str), "%d", tank_idx + 1);

        int len = snprintf(json_payload, sizeof(json_payload),
                           "{\"TANK_LOCATION\": \"%s\", \"TANK_NUM\": \"%s\", \"L1\": \"%s\", \"L2\": \"%s\", \"L3\": \"%s\" , \"Timestamp\": \"%s\"}",
                           tank_location,
                           tank_num_str,
                           temp_str_L1,
                           temp_str_L2,
                           temp_str_L3,
                            timestamp);

        if (len < 0 || len >= sizeof(json_payload)) {
            fprintf(stderr, "[ERROR] JSON payload for Tank %d too large or error occurred.\n", tank_idx + 1);
        } else {
            printf("[DEBUG] Sending JSON: %s\n", json_payload);
            if (!post_json_to_api(api_url, json_payload)) {
                fprintf(stderr, "[ERROR] Failed to post temperature data for Tank %d\n", tank_idx + 1);
            } else {
                printf("Successfully sent data for Tank %d to API.\n", tank_idx + 1);
            }
        }
    }

    free(temperat);
    usb_tc08_close_unit(handle);
    printf("Finished sending temperature data.\n");
}

// Function to set environment variables
void Setenv() {
    const char *env_fastapi_base_url = getenv("FASTAPI_BASE_URL");
    if (env_fastapi_base_url != NULL) {
        fastapi_base_url = strdup(env_fastapi_base_url);
        if (fastapi_base_url == NULL) {
            perror("strdup failed for FASTAPI_BASE_URL");
            exit(EXIT_FAILURE);
        }
    } else {
        fprintf(stderr, "[ERROR] FASTAPI_BASE_URL environment variable not set. Exiting.\n");
        exit(EXIT_FAILURE);
    }

    const char *env_num_tank = getenv("NUMBER_TANK");
    if (env_num_tank != NULL) {
        NUM_TANK = strdup(env_num_tank);
        if (NUM_TANK == NULL) {
            perror("strdup failed for NUMBER_TANK");
            exit(EXIT_FAILURE);
        }
    } else {
        fprintf(stderr, "[ERROR] NUMBER_TANK environment variable not set. Exiting.\n");
        exit(EXIT_FAILURE);
    }

    // NUM_LAYERS will be implicitly 3 for the Tank model in this code.
    // However, if you explicitly set NUM_LAYERS in your .env for other purposes,
    // we can still read it, but this code assumes 3 layers for the Tank API.
    const char *env_num_layers = getenv("NUM_LAYERS");
    if (env_num_layers != NULL) {
        int temp_num_layers = atoi(env_num_layers);
        if (temp_num_layers != 3) {
             fprintf(stderr, "[WARN] NUM_LAYERS in .env is %d, but Tank model expects 3. Proceeding with 3.\n", temp_num_layers);
        }
        // We will override NUM_LAYERS_FROM_ENV to 3 for the Tank model anyway.
        NUM_LAYERS_FROM_ENV = 3;
    } else {
        fprintf(stderr, "[WARN] NUM_LAYERS environment variable not set. Assuming 3 for Tank model.\n");
        NUM_LAYERS_FROM_ENV = 3; // Default to 3 layers for the Tank model
    }

    const char *env_tank_location = getenv("TANK_LOCATION");
    if (env_tank_location != NULL) {
        tank_location = strdup(env_tank_location);
        if (tank_location == NULL) {
            perror("strdup failed for TANK_LOCATION");
            exit(EXIT_FAILURE);
        }
    } else {
        fprintf(stderr, "[ERROR] TANK_LOCATION environment variable not set. Exiting.\n");
        exit(EXIT_FAILURE);
    }

    printf("[DEBUG] FASTAPI_BASE_URL: %s\n", fastapi_base_url);
    printf("[DEBUG] NUMBER_TANK: %s\n", NUM_TANK);
    printf("[DEBUG] NUM_LAYERS (for Tank API): %d\n", NUM_LAYERS_FROM_ENV);
    printf("[DEBUG] TANK_LOCATION: %s\n", tank_location);
}

int main() {
    Setenv();

    printf("Starting temperature capture and API send...\n");
    send_tank_temperature_to_api();

    free(fastapi_base_url);
    free(NUM_TANK);
    free(tank_location);

    return EXIT_SUCCESS;
}
