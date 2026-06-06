#define STB_IMAGE_IMPLEMENTATION
#include "../include/stb_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <sys/ioctl.h>
#include <unistd.h>
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

void get_terminal_size(int *width, int *height) {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        *width = w.ws_col;
        *height = w.ws_row;
    } else {
        // Fallback
        *width = 80;
        *height = 24;
    }
}

void render_image_to_terminal(const unsigned char *encoded_bytes, size_t size) {
    int orig_w, orig_h, channels;

    unsigned char *pixels = stbi_load_from_memory(encoded_bytes, (int)size, &orig_w, &orig_h, &channels, 3);
    if (!pixels) {
        fprintf(stderr, "\e[1;31mError:\e[0m Failed to decode image: %s\n", stbi_failure_reason());
        return;
    }

    int term_w, term_h;
    get_terminal_size(&term_w, &term_h);

    int target_w = term_w - 2;
    int target_h = (orig_h * target_w) / orig_w;

    float x_ratio = (float)orig_w / target_w;
    float y_ratio = (float)orig_h / target_h;

    for (int screen_y = 0; screen_y < target_h - 1; screen_y += 2) {
        for (int screen_x = 0; screen_x < target_w; screen_x++) {
            int orig_x = (int)(screen_x * x_ratio);
            int orig_y_top = (int)(screen_y * y_ratio);
            int orig_y_bot = (int)((screen_y + 1)* y_ratio);

            int top_idx = (orig_y_top * orig_w + orig_x) * 3;
            int bot_idx = (orig_y_bot * orig_w + orig_x) * 3;

            // set foreground (top) and background (bottom) color then print the half-block
            printf("\033[38;2;%d;%d;%dm\033[48;2;%d;%d;%dm▀", 
                   pixels[top_idx], pixels[top_idx+1], pixels[top_idx+2],
                   pixels[bot_idx], pixels[bot_idx+1], pixels[bot_idx+2]);
        }
        // reset text color
        printf("\033[0m\n");
    }
    stbi_image_free(pixels);
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

    if (img_response.memory != NULL) {
        printf("\e[1;32mSuccess:\e[0m Downloaded raw image buffer (%lu bytes).\n", (unsigned long)img_response.size);
        printf("Decoding and rendering space artifact...\n\n");
        render_image_to_terminal((unsigned char*)img_response.memory, img_response.size);
        
        free(img_response.memory);
    }
    
    // Cleanup
    free(extracted_img_url);
    free(json_response.memory);

    curl_global_cleanup();
    return 0;
}