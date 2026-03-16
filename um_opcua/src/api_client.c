#include "api_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>
#include <cjson/cJSON.h>

static size_t
write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    HttpBuffer *mem = (HttpBuffer *)userp;

    char *ptr = (char *)realloc(mem->data, mem->size + realsize + 1);
    if(!ptr)
        return 0;

    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = '\0';
    return realsize;
}

static UA_Boolean
response_looks_like_auth_error(int http_code, const char *response) {
    if(http_code == 401 || http_code == 403)
        return true;

    if(!response)
        return false;

    if(strstr(response, "unauthorized")) return true;
    if(strstr(response, "Unauthorized")) return true;
    if(strstr(response, "forbidden")) return true;
    if(strstr(response, "Forbidden")) return true;

    if(strstr(response, "\"error\"")) {
        if(strstr(response, "auth")) return true;
        if(strstr(response, "login")) return true;
        if(strstr(response, "session")) return true;
        if(strstr(response, "token")) return true;
        if(strstr(response, "cookie")) return true;
    }

    return false;
}

static int
http_post_json_once(const char *url, const char *json, int with_protocol_header, char **response_out) {
    CURL *curl = curl_easy_init();
    if(!curl)
        return -1;

    HttpBuffer chunk;
    chunk.data = (char *)malloc(1);
    chunk.size = 0;

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");

    if(with_protocol_header) {
        char protocol_header[256];
        snprintf(protocol_header, sizeof(protocol_header), "X-Protocol-USPD: %s", PROTOCOL);
        headers = curl_slist_append(headers, protocol_header);
        headers = curl_slist_append(headers, "X-Requested-With: XMLHttpRequest");
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "cookies.txt");
    curl_easy_setopt(curl, CURLOPT_COOKIEJAR, "cookies.txt");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if(res != CURLE_OK) {
        free(chunk.data);
        return -1;
    }

    *response_out = chunk.data;
    return (int)http_code;
}

static int
http_get_json_once(const char *url, const char *protocol, char **response_out) {
    CURL *curl = curl_easy_init();
    if(!curl)
        return -1;

    HttpBuffer chunk;
    chunk.data = (char *)malloc(1);
    chunk.size = 0;

    struct curl_slist *headers = NULL;
    char protocol_header[256];
    snprintf(protocol_header, sizeof(protocol_header), "X-Protocol-USPD: %s", protocol);
    headers = curl_slist_append(headers, protocol_header);
    headers = curl_slist_append(headers, "Accept: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "cookies.txt");
    curl_easy_setopt(curl, CURLOPT_COOKIEJAR, "cookies.txt");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if(res != CURLE_OK) {
        free(chunk.data);
        return -1;
    }

    *response_out = chunk.data;
    return (int)http_code;
}

static int
http_post_json(const char *url, const char *json, int with_protocol_header, char **response_out) {
    int code = http_post_json_once(url, json, with_protocol_header, response_out);

    if(code < 0)
        return code;

    if(response_looks_like_auth_error(code, *response_out)) {
        free(*response_out);
        *response_out = NULL;

        printf("HTTP POST auth expired, re-login...\n");

        if(api_login() != 0)
            return -1;

        code = http_post_json_once(url, json, with_protocol_header, response_out);
    }

    return code;
}

static int
http_get_json(const char *url, const char *protocol, char **response_out) {
    int code = http_get_json_once(url, protocol, response_out);

    if(code < 0)
        return code;

    if(response_looks_like_auth_error(code, *response_out)) {
        free(*response_out);
        *response_out = NULL;

        printf("HTTP GET auth expired, re-login...\n");

        if(api_login() != 0)
            return -1;

        code = http_get_json_once(url, protocol, response_out);
    }

    return code;
}

UA_Boolean
parse_iso_datetime_to_ua(const char *iso, UA_DateTime *out_dt) {
    if(!iso || !out_dt)
        return false;

    int Y = 0, M = 0, D = 0, h = 0, m = 0, s = 0;
    int tz_h = 0, tz_m = 0;
    char sign = '+';

    int n = sscanf(iso, "%d-%d-%dT%d:%d:%d%c%d:%d",
                   &Y, &M, &D, &h, &m, &s, &sign, &tz_h, &tz_m);

    if(n < 6)
        return false;

    UA_DateTimeStruct dts;
    memset(&dts, 0, sizeof(dts));
    dts.year = (UA_UInt16)Y;
    dts.month = (UA_Byte)M;
    dts.day = (UA_Byte)D;
    dts.hour = (UA_Byte)h;
    dts.min = (UA_Byte)m;
    dts.sec = (UA_Byte)s;

    UA_DateTime dt = UA_DateTime_fromStruct(dts);

    if(n >= 9) {
        UA_Int64 offsetMin = (UA_Int64)tz_h * 60 + tz_m;
        UA_Int64 offset100ns = offsetMin * 60LL * UA_DATETIME_SEC;
        if(sign == '+')
            dt -= offset100ns;
        else if(sign == '-')
            dt += offset100ns;
    }

    *out_dt = dt;
    return true;
}

int
api_login(void) {
    remove("cookies.txt");

    char url[512];
    snprintf(url, sizeof(url), "%s/auth", API_URL);

    char payload[512];
    snprintf(payload, sizeof(payload),
             "{\"login\":\"%s\",\"password\":\"%s\"}",
             LOGIN, PASSWORD);

    char *response = NULL;
    int code = http_post_json_once(url, payload, 0, &response);

    printf("Login status: %d\n", code);
    if(response) {
        printf("Login response: %s\n", response);
        free(response);
    }

    return code == 200 ? 0 : -1;
}

int
api_get_meters(MeterInfo *meters, size_t max_meters, size_t *out_count) {
    if(!meters || !out_count)
        return -1;

    char url[512];
    snprintf(url, sizeof(url), "%s/settings/meter/table", API_URL);

    char *response = NULL;
    int code = http_get_json(url, PROTOCOL, &response);
    printf("Meters status: %d\n", code);

    if(code != 200 || !response) {
        if(response)
            free(response);
        return -1;
    }

    cJSON *root = cJSON_Parse(response);
    if(!root) {
        free(response);
        return -1;
    }

    cJSON *meters_json = cJSON_GetObjectItem(root, "Meters");
    if(!cJSON_IsArray(meters_json)) {
        cJSON_Delete(root);
        free(response);
        return -1;
    }

    size_t count = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, meters_json) {
        if(count >= max_meters)
            break;

        cJSON *id = cJSON_GetObjectItem(item, "id");
        cJSON *type = cJSON_GetObjectItem(item, "type");
        cJSON *typeName = cJSON_GetObjectItem(item, "typeName");
        cJSON *addr = cJSON_GetObjectItem(item, "addr");
        cJSON *serial = cJSON_GetObjectItem(item, "serial");

        if(!cJSON_IsNumber(id) || !cJSON_IsNumber(type))
            continue;

        MeterInfo *m = &meters[count];
        memset(m, 0, sizeof(*m));
        m->id = id->valueint;
        m->type = type->valueint;

        if(cJSON_IsString(typeName))
            strncpy(m->typeName, typeName->valuestring, sizeof(m->typeName) - 1);
        if(cJSON_IsString(addr))
            strncpy(m->addr, addr->valuestring, sizeof(m->addr) - 1);
        if(cJSON_IsString(serial))
            strncpy(m->serial, serial->valuestring, sizeof(m->serial) - 1);

        count++;
    }

    cJSON_Delete(root);
    free(response);

    *out_count = count;

    printf("Meters count: %zu\n", count);
    for(size_t i = 0; i < count; i++) {
        printf("METER[%zu]: id=%d type=%d typeName=%s addr=%s serial=%s\n",
               i, meters[i].id, meters[i].type, meters[i].typeName,
               meters[i].addr, meters[i].serial);
    }

    return 0;
}

int
api_read_current_double(int meter_id,
                        const char *measure,
                        const char *tag,
                        double *out_value) {
    if(!measure || !tag || !out_value)
        return -1;

    char url[512];
    snprintf(url, sizeof(url), "%s/meter/data/moment", API_URL);

    char payload[1024];
    snprintf(payload, sizeof(payload),
             "{\"ids\":[%d],\"measures\":[\"%s\"],\"tags\":[\"%s\"]}",
             meter_id, measure, tag);

    char *response = NULL;
    int code = http_post_json(url, payload, 1, &response);

    if(code != 200 || !response) {
        if(response)
            free(response);
        return -1;
    }

    int rc = -1;
    cJSON *root = cJSON_Parse(response);
    if(root) {
        cJSON *measures = cJSON_GetObjectItem(root, "measures");
        cJSON *measureObj = cJSON_IsArray(measures) ? cJSON_GetArrayItem(measures, 0) : NULL;
        cJSON *devices = measureObj ? cJSON_GetObjectItem(measureObj, "devices") : NULL;
        cJSON *device = cJSON_IsArray(devices) ? cJSON_GetArrayItem(devices, 0) : NULL;
        cJSON *vals = device ? cJSON_GetObjectItem(device, "vals") : NULL;
        cJSON *valItem = cJSON_IsArray(vals) ? cJSON_GetArrayItem(vals, 0) : NULL;
        cJSON *tags = valItem ? cJSON_GetObjectItem(valItem, "tags") : NULL;

        if(cJSON_IsArray(tags)) {
            cJSON *tagItem = NULL;
            cJSON_ArrayForEach(tagItem, tags) {
                cJSON *tagName = cJSON_GetObjectItem(tagItem, "tag");
                cJSON *tagVal = cJSON_GetObjectItem(tagItem, "val");

                if(cJSON_IsString(tagName) &&
                   strcmp(tagName->valuestring, tag) == 0 &&
                   cJSON_IsNumber(tagVal)) {
                    *out_value = tagVal->valuedouble;
                    rc = 0;
                    break;
                }
            }
        }

        cJSON_Delete(root);
    }

    free(response);
    return rc;
}

int
api_read_current_datetime(int meter_id, UA_DateTime *out_dt) {
    if(!out_dt)
        return -1;

    char url[512];
    snprintf(url, sizeof(url), "%s/meter/data/moment", API_URL);

    char payload[512];
    snprintf(payload, sizeof(payload),
             "{\"ids\":[%d],\"measures\":[\"GetTime\"],\"tags\":[]}",
             meter_id);

    char *response = NULL;
    int code = http_post_json(url, payload, 1, &response);

    if(code != 200 || !response) {
        if(response)
            free(response);
        return -1;
    }

    int rc = -1;
    cJSON *root = cJSON_Parse(response);
    if(root) {
        cJSON *measures = cJSON_GetObjectItem(root, "measures");
        cJSON *measureObj = cJSON_IsArray(measures) ? cJSON_GetArrayItem(measures, 0) : NULL;
        cJSON *devices = measureObj ? cJSON_GetObjectItem(measureObj, "devices") : NULL;
        cJSON *device = cJSON_IsArray(devices) ? cJSON_GetArrayItem(devices, 0) : NULL;
        cJSON *vals = device ? cJSON_GetObjectItem(device, "vals") : NULL;
        cJSON *valItem = cJSON_IsArray(vals) ? cJSON_GetArrayItem(vals, 0) : NULL;
        cJSON *ts = valItem ? cJSON_GetObjectItem(valItem, "ts") : NULL;

        if(cJSON_IsString(ts) && parse_iso_datetime_to_ua(ts->valuestring, out_dt))
            rc = 0;

        cJSON_Delete(root);
    }

    free(response);
    return rc;
}

int
api_read_archive(int meter_id,
                 const char *measure,
                 const char *tag,
                 long start_ts,
                 long end_ts,
                 ArchiveResult *out) {
    if(!measure || !tag || !out)
        return -1;

    char url[512];
    snprintf(url, sizeof(url), "%s/meter/data/arch", API_URL);

    char payload[1024];
    snprintf(payload, sizeof(payload),
             "{\"ids\":[%d],\"measures\":[\"%s\"],\"tags\":[\"%s\"],\"time\":{\"start\":%ld,\"end\":%ld}}",
             meter_id, measure, tag, start_ts, end_ts);

    char *response = NULL;
    int code = http_post_json(url, payload, 1, &response);

    if(code != 200 || !response) {
        if(response)
            free(response);
        return -1;
    }

    out->count = 0;

    cJSON *root = cJSON_Parse(response);
    if(!root) {
        free(response);
        return -1;
    }

    cJSON *measures = cJSON_GetObjectItem(root, "measures");
    if(!cJSON_IsArray(measures)) {
        cJSON_Delete(root);
        free(response);
        return -1;
    }

    int rc = -1;

    cJSON *measureItem = NULL;
    cJSON_ArrayForEach(measureItem, measures) {
        cJSON *measureName = cJSON_GetObjectItem(measureItem, "measure");
        if(!cJSON_IsString(measureName))
            continue;
        if(strcmp(measureName->valuestring, measure) != 0)
            continue;

        cJSON *devices = cJSON_GetObjectItem(measureItem, "devices");
        if(!cJSON_IsArray(devices))
            continue;

        cJSON *deviceItem = NULL;
        cJSON_ArrayForEach(deviceItem, devices) {
            cJSON *id = cJSON_GetObjectItem(deviceItem, "id");
            if(!cJSON_IsNumber(id) || id->valueint != meter_id)
                continue;

            cJSON *vals = cJSON_GetObjectItem(deviceItem, "vals");
            if(!cJSON_IsArray(vals))
                continue;

            cJSON *valItem = NULL;
            cJSON_ArrayForEach(valItem, vals) {
                if(out->count >= MAX_ARCH_POINTS)
                    break;

                cJSON *ts = cJSON_GetObjectItem(valItem, "ts");
                if(!cJSON_IsString(ts))
                    continue;

                ArchivePoint *pt = &out->points[out->count];
                memset(pt, 0, sizeof(*pt));

                if(!parse_iso_datetime_to_ua(ts->valuestring, &pt->ts))
                    continue;

                cJSON *tags = cJSON_GetObjectItem(valItem, "tags");
                if(cJSON_IsArray(tags)) {
                    cJSON *tagItem = NULL;
                    cJSON_ArrayForEach(tagItem, tags) {
                        cJSON *tagName = cJSON_GetObjectItem(tagItem, "tag");
                        cJSON *tagVal = cJSON_GetObjectItem(tagItem, "val");

                        if(cJSON_IsString(tagName) &&
                           strcmp(tagName->valuestring, tag) == 0 &&
                           cJSON_IsNumber(tagVal)) {
                            pt->value = tagVal->valuedouble;
                            pt->has_value = true;
                            break;
                        }
                    }
                }

                out->count++;
            }

            rc = 0;
            break;
        }

        if(rc == 0)
            break;
    }

    cJSON_Delete(root);
    free(response);
    return rc;
}
