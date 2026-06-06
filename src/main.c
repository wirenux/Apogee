#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

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

int main(void) {
    char *api_key = getenv("NASA_API_KEY");

    if (!api_key) {
        api_key = "DEMO_KEY";
        fprintf(stderr, "Warning: NASA_API_KEY not set. Using DEMO_KEY (rate limited).\n");
    }

    CURL *curl_handle;
    CURLcode res;

    struct MemoryChunk chunk;
    chunk.memory = malloc(1);
    chunk.size = 0;

    curl_global_init(CURL_GLOBAL_ALL);
    curl_handle = curl_easy_init();

    char url_buffer[256];

    snprintf(url_buffer, sizeof(url_buffer), "https://api.nasa.gov/planetary/apod?api_key=%s", api_key);

    if (curl_handle) {
        curl_easy_setopt(curl_handle, CURLOPT_URL, url_buffer);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");
        
        res = curl_easy_perform(curl_handle);

        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        } else {
            printf("Fetched %lu byte. \n", (unsigned long)chunk.size);
            printf("Paylod:\n%s\n", chunk.memory);
        }

        curl_easy_cleanup(curl_handle);
    }
    
    // Cleanup
    free(chunk.memory);
    curl_global_cleanup();
    return 0;
}