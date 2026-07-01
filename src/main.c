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

struct ApodMetadata {
    char *url;
    char *title;
    char *date;
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

struct ApodMetadata extract_metadata(const char *json_payload) {
    struct ApodMetadata meta = {NULL, NULL, NULL};

    cJSON *root = cJSON_Parse(json_payload);
    if (root == NULL) {
        return meta;
    }

    cJSON *url_item = cJSON_GetObjectItemCaseSensitive(root, "hdurl");
    if (!url_item || !url_item->valuestring) {
        url_item = cJSON_GetObjectItemCaseSensitive(root, "url");
    }
    cJSON *title_item = cJSON_GetObjectItemCaseSensitive(root, "title");
    cJSON *date_item = cJSON_GetObjectItemCaseSensitive(root, "date");

    if (cJSON_IsString(url_item) && url_item->valuestring) {
        meta.url = strdup(url_item->valuestring);
    }
    if (cJSON_IsString(title_item) && title_item->valuestring) {
        meta.title = strdup(title_item->valuestring);
    }
    if (cJSON_IsString(date_item) && date_item->valuestring) {
        meta.date = strdup(date_item->valuestring);
    }

    cJSON_Delete(root);
    return meta;
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

    // Reserve 5 lines + 1 line for the next shell prompt
    int metadata_rows = 5;
    int max_text_rows = term_h - metadata_rows - 1; 
    if (max_text_rows < 2) {
        max_text_rows = 2;
    }

    int max_pixel_h = max_text_rows * 2;
    int max_pixel_w = term_w - 2;

    int target_w = term_w - 2;
    int target_h = (orig_h * target_w) / orig_w;

    if (target_h > max_pixel_h) {
        target_h = max_pixel_h;
        target_w = (orig_w * target_h) / orig_h;
    }

    target_h = (target_h / 2) * 2;

    float x_ratio = (float)orig_w / target_w;
    float y_ratio = (float)orig_h / target_h;

    for (int screen_y = 0; screen_y < target_h - 1; screen_y += 2) {
        for (int screen_x = 0; screen_x < target_w; screen_x++) {
            int orig_x = (int)(screen_x * x_ratio);
            int orig_y_top = (int)(screen_y * y_ratio);
            int orig_y_bot = (int)((screen_y + 1)* y_ratio);

            if (orig_x >= orig_w) {
                orig_x = orig_w - 1;
            }
            if (orig_y_top >= orig_h) {
                orig_y_top = orig_h -1;
            }
            if (orig_y_bot >= orig_h) {
                orig_y_bot = orig_h -1;
            }

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

int main(int agrc, char *argv[]) {
    curl_global_init(CURL_GLOBAL_ALL);

    char *api_key = NULL;

    if (agrc > 1 && strcmp(argv[1], "--demo") == 0) {
        api_key = "DEMO_KEY";
    } else {
        api_key = getenv("NASA_API_KEY");
    }

    if (!api_key) {;
        fprintf(stderr, "\e[1;31mError:\e[0m NASA_API_KEY not set. Get one at : https://api.nasa.gov/ (it's free).\n");
        fprintf(stderr, "Or bypass authentication by running: \e[1;36mmake run ARGS=\"--demo\"\e[0m\n");
        return 1;
    }

    char url_buffer[256];
    snprintf(url_buffer, sizeof(url_buffer), "https://api.nasa.gov/planetary/apod?api_key=%s", api_key);

    struct MemoryChunk json_response = fetch_url(url_buffer);
    if (json_response.memory == NULL) {
        curl_global_cleanup();
        return 1;
    }

    struct ApodMetadata meta = extract_metadata(json_response.memory);
    if (meta.url == NULL) {
        cJSON *err_root = cJSON_Parse(json_response.memory);
        int is_rate_limit = 0;
        char *err_msg = NULL;

        if (err_root) {
            cJSON *error_obj = cJSON_GetObjectItemCaseSensitive(err_root, "error");
            if (error_obj) {
                cJSON *code_item = cJSON_GetObjectItemCaseSensitive(error_obj, "code");
                cJSON *msg_item = cJSON_GetObjectItemCaseSensitive(error_obj, "message");
                
                if (cJSON_IsString(code_item) && strcmp(code_item->valuestring, "OVER_RATE_LIMIT") == 0) {
                    is_rate_limit = 1;
                    if (cJSON_IsString(msg_item) && msg_item->valuestring) {
                        err_msg = strdup(msg_item->valuestring);
                    }
                }
            }
            cJSON_Delete(err_root);
        }

        if (is_rate_limit) {
            fprintf(stderr, "\e[1;31mError:\e[0m NASA API Rate Limit Exceeded (\e[1;33mOVER_RATE_LIMIT\e[0m)\n");
            if (err_msg) {
                fprintf(stderr, "Message: %s\n", err_msg);
                free(err_msg);
            }
            fprintf(stderr, "\e[1;36mTip:\e[0m The shared 'DEMO_KEY' is heavily throttled globally. \n");
            fprintf(stderr, "     Get your own free API key at https://api.nasa.gov/ and export it to your environment:\n");
            fprintf(stderr, "     export NASA_API_KEY=\"your_key_here\"\n");
        } else {
            // Fallback for other unexpected JSON payloads
            fprintf(stderr, "\e[1;31mError:\e[0m Failed to extract payload asset target URL.\n");
            fprintf(stderr, "Server response block:\n%s\n", json_response.memory);
        }

        free(json_response.memory);
        curl_global_cleanup();
        return 1;
    }

    struct MemoryChunk img_response = fetch_url(meta.url);
    if (img_response.memory == NULL) {
        fprintf(stderr, "\e[1;31mError:\e[0m Failed to download image asset from NASA.\n");
        free(meta.url);
        free(meta.title);
        free(meta.date);
        free(json_response.memory);
        curl_global_cleanup();
        return 1;
    }

    printf("\033[2J\033[H");
    render_image_to_terminal((unsigned char*)img_response.memory, img_response.size);

    printf("\n\e[1;35m┌─── NASA ASTRONOMY PICTURE OF THE DAY ───────────────────────────────────┐\e[0m\n");
    printf("\e[1;35m│\e[0m \e[1;33mTitle:\e[0m %-65s\e[1;35m│\e[0m\n", meta.title ? meta.title : "N/A"); 
    printf("\e[1;35m│\e[0m \e[1;33mDate :\e[0m %-65s\e[1;35m│\e[0m\n", meta.date ? meta.date : "N/A"); 
    printf("\e[1;35m└─────────────────────────────────────────────────────────────────────────┘\e[0m\n\n");
    
    // Cleanup
    free(img_response.memory);
    free(meta.url);
    free(meta.title);
    free(meta.date);
    free(json_response.memory);

    curl_global_cleanup();
    return 0;
}