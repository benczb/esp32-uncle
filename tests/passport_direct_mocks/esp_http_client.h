#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 0x101
struct mock_http;typedef struct mock_http *esp_http_client_handle_t;
typedef struct {const char *url,*user_agent;int timeout_ms,buffer_size,buffer_size_tx;void (*crt_bundle_attach)(void);bool disable_auto_redirect;} esp_http_client_config_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_open(esp_http_client_handle_t,int);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
int esp_http_client_read(esp_http_client_handle_t,char *,int);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t);
void esp_http_client_close(esp_http_client_handle_t);
void esp_http_client_cleanup(esp_http_client_handle_t);

esp_err_t esp_http_client_set_header(esp_http_client_handle_t,const char *,const char *);
