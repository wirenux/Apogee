#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "../include/cJSON.h"

struct MemoryChunk {
    char *memory;
    size_t size;
};

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t real_size = size * nmemb;
    struct MemoryChunk *mem = (struct MemoryChunk *)userp;

    char *ptr = realloc(mem->memory, mem->size + real_size + 1);
    if (!ptr) {
        return 0; // Out of memory
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, real_size);
    mem->size += real_size;
    mem->memory[mem->size] = 0; // Null terminate string

    return real_size;
}

struct MemoryChunk fetch_url(const char *url) {
    CURL *curl_handle;
    CURLcode res;
    struct MemoryChunk chunk;

    chunk.memory = malloc(1);
    chunk.size = 0;

    curl_handle = curl_easy_init();
    if (curl_handle) {
        curl_easy_setopt(curl_handle, CURLOPT_URL, url);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");
        
        curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 15L);
        
        res = curl_easy_perform(curl_handle);

        if (res != CURLE_OK) {
            fprintf(stderr, "Fetch failed: %s\n", curl_easy_strerror(res));
            free(chunk.memory);
            chunk.memory = NULL;
            chunk.size = 0;
        }
        curl_easy_cleanup(curl_handle);
    }
    return chunk;
}

char *extract_image_url(const char *json_payload) {
    cJSON *root = cJSON_Parse(json_payload);
    if (root == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            fprintf(stderr, "JSON Parse Error before: %s\n", error_ptr);
        }
        return NULL;
    }

    cJSON *url_item = cJSON_GetObjectItemCaseSensitive(root, "hdurl");
    char *extracted_url = NULL;

    if (cJSON_IsString(url_item) && (url_item->valuestring != NULL)) {
        extracted_url = strdup(url_item->valuestring);
    } else {
        fprintf(stderr, "\e[1;31mError:\e[0m 'url' key not found or is not a string in JSON.\n");
    }

    cJSON_Delete(root);
    return extracted_url;
}

int main(void) {
    curl_global_init(CURL_GLOBAL_ALL);

    char *api_key = getenv("NASA_API_KEY");

    if (!api_key) {;
        fprintf(stderr, "\e[1;31mError:\e[0m NASA_API_KEY not set. Get one at : https://api.nasa.gov/ (it's free).\n");
        return 0;
    }

    char url_buffer[256];
    snprintf(url_buffer, sizeof(url_buffer), "https://api.nasa.gov/planetary/apod?api_key=%s", api_key);

    struct MemoryChunk json_response = fetch_url(url_buffer);
    if (json_response.memory == NULL) {
        curl_global_cleanup();
        return 1;
    }
    
    char *extracted_img_url = extract_image_url(json_response.memory);
    if (extracted_img_url == NULL) {
        free(json_response.memory);
        curl_global_cleanup();
        return 1;
    }

    printf("\e[1;32mSuccess:\e[0m Extracted Image URL: %s\n", extracted_img_url);
    printf("Downloading space image asset...\n");

    struct MemoryChunk img_response = fetch_url(extracted_img_url);
    if (img_response.memory == NULL) {
        fprintf(stderr, "\e[1;31mError:\e[0m Failed to download image asset from NASA.\n");
        free(extracted_img_url);
        free(json_response.memory);
        curl_global_cleanup();
        return 1;
    }

    printf("\e[1;32mSuccess:\e[0m Downloaded raw image buffer (%lu bytes).\n", (unsigned long)img_response.size);
    
    
    // Cleanup
    free(img_response.memory);
    free(extracted_img_url);
    free(json_response.memory);
    
    curl_global_cleanup();
    return 0;
}