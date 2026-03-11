#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <curl/curl.h>

#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <open62541/plugin/log_stdout.h>

#define API_URL   "http://localhost"
#define LOGIN     "admin"
#define PASSWORD  "admin"
#define PROTOCOL  "40"
#define CSV_FILE  "../config/tags.csv"

#define MAX_METERS   256
#define MAX_MAPPINGS 4096
#define STRSZ        128

static volatile UA_Boolean running = true;

/* -------------------------------------------------- */
/* SIGNALS                                            */
/* -------------------------------------------------- */

static void
stopHandler(int sig) {
    (void)sig;
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "received ctrl-c");
    running = false;
}

/* -------------------------------------------------- */
/* MODELS                                             */
/* -------------------------------------------------- */

typedef struct {
    int id;
    int type;
    char typeName[STRSZ];
    char addr[STRSZ];
} MeterInfo;

typedef struct {
    int device_type;
    char measure[STRSZ];
    char api_tag[STRSZ];
    char display[STRSZ];
} TagMapping;

typedef struct {
    int meter_id;
    char measure[STRSZ];
    char api_tag[STRSZ];
    UA_Boolean is_datetime;
    double last_value;
    UA_Boolean has_last_value;
    UA_DateTime last_dt_value;
    UA_Boolean has_last_dt_value;
} TagContext;

typedef struct {
    char *data;
    size_t size;
} HttpBuffer;

static MeterInfo g_meters[MAX_METERS];
static size_t g_meterCount = 0;

static TagMapping g_mappings[MAX_MAPPINGS];
static size_t g_mappingCount = 0;

/* -------------------------------------------------- */
/* HTTP                                               */
/* -------------------------------------------------- */

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

static int
http_post_json(const char *url, const char *json, int with_protocol_header, char **response_out) {
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
        char protocol_header[64];
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
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
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
http_get_json(const char *url, const char *protocol, char **response_out) {
    CURL *curl = curl_easy_init();
    if(!curl)
        return -1;

    HttpBuffer chunk;
    chunk.data = (char *)malloc(1);
    chunk.size = 0;

    struct curl_slist *headers = NULL;
    char protocol_header[64];
    snprintf(protocol_header, sizeof(protocol_header), "X-Protocol-USPD: %s", protocol);
    headers = curl_slist_append(headers, protocol_header);
    headers = curl_slist_append(headers, "Accept: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "cookies.txt");
    curl_easy_setopt(curl, CURLOPT_COOKIEJAR, "cookies.txt");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
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

/* -------------------------------------------------- */
/* SIMPLE JSON HELPERS                                */
/* -------------------------------------------------- */

static int
json_extract_int_after(const char *src, const char *key, int *out) {
    const char *p = strstr(src, key);
    if(!p) return 0;

    p += strlen(key);
    while(*p && (*p == ' ' || *p == ':' || *p == '"'))
        p++;

    *out = atoi(p);
    return 1;
}

static int
json_extract_string_after(const char *src, const char *key, char *out, size_t outsz) {
    const char *p = strstr(src, key);
    if(!p) return 0;

    p = strchr(p, ':');
    if(!p) return 0;
    p++;

    while(*p == ' ')
        p++;

    if(*p != '"')
        return 0;
    p++;

    const char *q = strchr(p, '"');
    if(!q) return 0;

    size_t len = (size_t)(q - p);
    if(len >= outsz)
        len = outsz - 1;

    memcpy(out, p, len);
    out[len] = '\0';
    return 1;
}

static int
json_extract_value_for_tag(const char *json, const char *tag, double *out_value) {
    if(!json || !tag || !out_value)
        return 0;

    if(strstr(json, "\"error\""))
        return 0;

    char tag_pattern[256];
    snprintf(tag_pattern, sizeof(tag_pattern), "\"tag\":\"%s\"", tag);

    const char *p = strstr(json, tag_pattern);
    if(!p)
        return 0;

    const char *val_pos = strstr(p, "\"val\"");
    if(!val_pos)
        return 0;

    val_pos = strchr(val_pos, ':');
    if(!val_pos)
        return 0;

    val_pos++;
    while(*val_pos == ' ')
        val_pos++;

    *out_value = atof(val_pos);
    return 1;
}

static int
json_extract_timestamp_from_response(const char *json, char *out, size_t outsz) {
    if(!json || !out)
        return 0;

    if(strstr(json, "\"error\""))
        return 0;

    const char *p = strstr(json, "\"ts\"");
    if(!p)
        return 0;

    p = strchr(p, ':');
    if(!p)
        return 0;
    p++;

    while(*p == ' ')
        p++;

    if(*p != '"')
        return 0;
    p++;

    const char *q = strchr(p, '"');
    if(!q)
        return 0;

    size_t len = (size_t)(q - p);
    if(len >= outsz)
        len = outsz - 1;

    memcpy(out, p, len);
    out[len] = '\0';
    return 1;
}

/* -------------------------------------------------- */
/* TIME                                               */
/* -------------------------------------------------- */

static UA_Boolean
parse_iso_datetime_to_ua(const char *iso, UA_DateTime *out_dt) {
    if(!iso || !out_dt)
        return false;

    int Y, M, D, h, m, s, tz_h = 0, tz_m = 0;
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
    dts.milliSec = 0;
    dts.microSec = 0;
    dts.nanoSec = 0;

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

/* -------------------------------------------------- */
/* API                                                */
/* -------------------------------------------------- */

static int
api_login(void) {
    char url[256];
    snprintf(url, sizeof(url), "%s/auth", API_URL);

    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"login\":\"%s\",\"password\":\"%s\"}",
             LOGIN, PASSWORD);

    char *response = NULL;
    int code = http_post_json(url, payload, 0, &response);

    printf("Login status: %d\n", code);

    if(response)
        free(response);

    return code == 200 ? 0 : -1;
}

static int
api_get_meters(void) {
    char url[256];
    snprintf(url, sizeof(url), "%s/settings/meter/table", API_URL);

    char *response = NULL;
    int code = http_get_json(url, PROTOCOL, &response);

    printf("Meters status: %d\n", code);

    if(code != 200 || !response) {
        if(response)
            free(response);
        return -1;
    }

    g_meterCount = 0;

    const char *meters_arr = strstr(response, "\"Meters\":[");
    if(!meters_arr) {
        free(response);
        return -1;
    }

    const char *p = strchr(meters_arr, '[');
    if(!p) {
        free(response);
        return -1;
    }
    p++; /* after '[' */

    while(*p && *p != ']' && g_meterCount < MAX_METERS) {
        while(*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t' || *p == ',')
            p++;

        if(*p != '{')
            break;

        const char *obj_start = p;
        int depth = 0;

        while(*p) {
            if(*p == '{')
                depth++;
            else if(*p == '}') {
                depth--;
                if(depth == 0) {
                    p++;
                    break;
                }
            }
            p++;
        }

        size_t obj_len = (size_t)(p - obj_start);
        if(obj_len == 0 || obj_len >= 2048)
            continue;

        char obj[2048];
        memcpy(obj, obj_start, obj_len);
        obj[obj_len] = '\0';

        MeterInfo *m = &g_meters[g_meterCount];
        memset(m, 0, sizeof(*m));

        if(!json_extract_int_after(obj, "\"id\"", &m->id))
            continue;
        if(!json_extract_int_after(obj, "\"type\"", &m->type))
            continue;

        json_extract_string_after(obj, "\"typeName\"", m->typeName, sizeof(m->typeName));
        json_extract_string_after(obj, "\"addr\"", m->addr, sizeof(m->addr));

        g_meterCount++;
    }

    free(response);

    printf("Meters count: %zu\n", g_meterCount);

    for(size_t i = 0; i < g_meterCount; i++) {
        printf("METER[%zu]: id=%d type=%d typeName=%s addr=%s\n",
               i,
               g_meters[i].id,
               g_meters[i].type,
               g_meters[i].typeName,
               g_meters[i].addr);
    }

    return 0;
}

static int
api_read_value(int meter_id, const char *measure, const char *tag, double *out_value) {
    char url[256];
    snprintf(url, sizeof(url), "%s/meter/data/moment", API_URL);

    char payload[512];

    if(tag && strlen(tag) > 0) {
        snprintf(payload, sizeof(payload),
                 "{\"ids\":[%d],\"measures\":[\"%s\"],\"tags\":[\"%s\"]}",
                 meter_id, measure, tag);
    } else {
        snprintf(payload, sizeof(payload),
                 "{\"ids\":[%d],\"measures\":[\"%s\"],\"tags\":[]}",
                 meter_id, measure);
    }

    char *response = NULL;
    int code = http_post_json(url, payload, 1, &response);

    if(code != 200 || !response) {
        if(response)
            free(response);
        return -1;
    }

    int ok = json_extract_value_for_tag(response, tag, out_value);

    free(response);
    return ok ? 0 : -1;
}

static int
api_read_time(int meter_id, UA_DateTime *out_dt) {
    char url[256];
    snprintf(url, sizeof(url), "%s/meter/data/moment", API_URL);

    char payload[256];
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

    char ts[128];
    int ok = json_extract_timestamp_from_response(response, ts, sizeof(ts));
    free(response);

    if(!ok)
        return -1;

    return parse_iso_datetime_to_ua(ts, out_dt) ? 0 : -1;
}

/* -------------------------------------------------- */
/* CSV                                                */
/* -------------------------------------------------- */

static void
trim(char *s) {
    size_t len = strlen(s);
    while(len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' || s[len - 1] == ' '))
        s[--len] = '\0';
}

static int
split_csv4(char *line, char **c1, char **c2, char **c3, char **c4) {
    char *p1 = line;
    char *p2 = strchr(p1, ',');
    if(!p2) return 0;
    *p2 = '\0';
    p2++;

    char *p3 = strchr(p2, ',');
    if(!p3) return 0;
    *p3 = '\0';
    p3++;

    char *p4 = strchr(p3, ',');
    if(!p4) return 0;
    *p4 = '\0';
    p4++;

    *c1 = p1;
    *c2 = p2;
    *c3 = p3;
    *c4 = p4;

    return 1;
}

static int
load_mapping_csv(const char *filename) {
    FILE *f = fopen(filename, "r");
    if(!f) {
        perror("fopen tags.csv");
        return -1;
    }

    char line[512];

    if(!fgets(line, sizeof(line), f)) {
        fclose(f);
        return -1;
    }

    g_mappingCount = 0;

    while(fgets(line, sizeof(line), f) && g_mappingCount < MAX_MAPPINGS) {
        trim(line);

        char *col1, *col2, *col3, *col4;
        if(!split_csv4(line, &col1, &col2, &col3, &col4))
            continue;

        TagMapping *m = &g_mappings[g_mappingCount];
        memset(m, 0, sizeof(*m));

        m->device_type = atoi(col1);
        strncpy(m->measure, col2, sizeof(m->measure) - 1);
        strncpy(m->api_tag, col3, sizeof(m->api_tag) - 1);
        strncpy(m->display, col4, sizeof(m->display) - 1);

        g_mappingCount++;
    }

    fclose(f);

    printf("Mappings count: %zu\n", g_mappingCount);
    return 0;
}

static UA_Boolean
has_mapping_for_device_type(int device_type) {
    for(size_t i = 0; i < g_mappingCount; i++) {
        if(g_mappings[i].device_type == device_type)
            return true;
    }
    return false;
}

/* -------------------------------------------------- */
/* HELPERS                                            */
/* -------------------------------------------------- */

static UA_Boolean
isInternalSession(const UA_NodeId *sessionId) {
    if(!sessionId)
        return true;

    if(sessionId->namespaceIndex == 0 &&
       sessionId->identifierType == UA_NODEIDTYPE_NUMERIC &&
       sessionId->identifier.numeric == 0)
        return true;

    return false;
}

/* -------------------------------------------------- */
/* OPC UA DATASOURCE                                  */
/* -------------------------------------------------- */

static UA_StatusCode
readCurrentValue(UA_Server *server,
                 const UA_NodeId *sessionId, void *sessionContext,
                 const UA_NodeId *nodeId, void *nodeContext,
                 UA_Boolean includeSourceTimeStamp,
                 const UA_NumericRange *range,
                 UA_DataValue *dataValue) {
    (void)server;
    (void)sessionContext;
    (void)nodeId;
    (void)range;

    TagContext *ctx = (TagContext *)nodeContext;
    if(!ctx)
        return UA_STATUSCODE_BADINTERNALERROR;

    if(isInternalSession(sessionId)) {
        if(ctx->is_datetime) {
            if(ctx->has_last_dt_value) {
                UA_Variant_setScalarCopy(&dataValue->value, &ctx->last_dt_value,
                                         &UA_TYPES[UA_TYPES_DATETIME]);
                dataValue->hasValue = true;
                dataValue->status = UA_STATUSCODE_GOOD;
            } else {
                dataValue->hasValue = false;
                dataValue->status = UA_STATUSCODE_BADWAITINGFORINITIALDATA;
            }
        } else {
            if(ctx->has_last_value) {
                UA_Variant_setScalarCopy(&dataValue->value, &ctx->last_value,
                                         &UA_TYPES[UA_TYPES_DOUBLE]);
                dataValue->hasValue = true;
                dataValue->status = UA_STATUSCODE_GOOD;
            } else {
                dataValue->hasValue = false;
                dataValue->status = UA_STATUSCODE_BADWAITINGFORINITIALDATA;
            }
        }

        if(includeSourceTimeStamp) {
            dataValue->hasSourceTimestamp = true;
            dataValue->sourceTimestamp = UA_DateTime_now();
        }

        return UA_STATUSCODE_GOOD;
    }

    if(ctx->is_datetime) {
        UA_DateTime dt;
        int rc = api_read_time(ctx->meter_id, &dt);

        if(rc == 0) {
            ctx->last_dt_value = dt;
            ctx->has_last_dt_value = true;

            UA_Variant_setScalarCopy(&dataValue->value, &dt,
                                     &UA_TYPES[UA_TYPES_DATETIME]);
            dataValue->hasValue = true;
            dataValue->status = UA_STATUSCODE_GOOD;
        } else {
            if(ctx->has_last_dt_value) {
                UA_Variant_setScalarCopy(&dataValue->value, &ctx->last_dt_value,
                                         &UA_TYPES[UA_TYPES_DATETIME]);
                dataValue->hasValue = true;
            } else {
                dataValue->hasValue = false;
            }

            dataValue->status = UA_STATUSCODE_BADNODATA;
        }
    } else {
        double value = 0.0;
        int rc = api_read_value(ctx->meter_id, ctx->measure, ctx->api_tag, &value);

        if(rc == 0) {
            ctx->last_value = value;
            ctx->has_last_value = true;

            UA_Variant_setScalarCopy(&dataValue->value, &value,
                                     &UA_TYPES[UA_TYPES_DOUBLE]);
            dataValue->hasValue = true;
            dataValue->status = UA_STATUSCODE_GOOD;
        } else {
            if(ctx->has_last_value) {
                UA_Variant_setScalarCopy(&dataValue->value, &ctx->last_value,
                                         &UA_TYPES[UA_TYPES_DOUBLE]);
                dataValue->hasValue = true;
            } else {
                dataValue->hasValue = false;
            }

            dataValue->status = UA_STATUSCODE_BADNODATA;
        }
    }

    if(includeSourceTimeStamp) {
        dataValue->hasSourceTimestamp = true;
        dataValue->sourceTimestamp = UA_DateTime_now();
    }

    return UA_STATUSCODE_GOOD;
}

/* -------------------------------------------------- */
/* MAIN                                               */
/* -------------------------------------------------- */

int
main(void) {
    signal(SIGINT, stopHandler);
    signal(SIGTERM, stopHandler);

    curl_global_init(CURL_GLOBAL_DEFAULT);

    if(api_login() != 0) {
        fprintf(stderr, "API login failed\n");
        return EXIT_FAILURE;
    }

    if(api_get_meters() != 0) {
        fprintf(stderr, "Get meters failed\n");
        return EXIT_FAILURE;
    }

    if(load_mapping_csv(CSV_FILE) != 0) {
        fprintf(stderr, "Load tags.csv failed\n");
        return EXIT_FAILURE;
    }

    UA_Server *server = UA_Server_new();
    UA_ServerConfig *config = UA_Server_getConfig(server);
    UA_ServerConfig_setDefault(config);

    UA_UInt16 nsIdx = UA_Server_addNamespace(server, "urn:um-smart-opcua");

    UA_NodeId metersFolderId;
    {
        UA_ObjectAttributes oAttr = UA_ObjectAttributes_default;
        oAttr.displayName = UA_LOCALIZEDTEXT("ru-RU", "Meters");

        UA_StatusCode retval = UA_Server_addObjectNode(
            server,
            UA_NODEID_STRING(nsIdx, "Meters"),
            UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
            UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
            UA_QUALIFIEDNAME(nsIdx, "Meters"),
            UA_NODEID_NUMERIC(0, UA_NS0ID_FOLDERTYPE),
            oAttr,
            NULL,
            &metersFolderId
        );

        if(retval != UA_STATUSCODE_GOOD) {
            UA_Server_delete(server);
            curl_global_cleanup();
            return EXIT_FAILURE;
        }
    }

    UA_StatusCode retval = UA_Server_run_startup(server);
    if(retval != UA_STATUSCODE_GOOD) {
        fprintf(stderr, "UA_Server_run_startup failed: 0x%08x\n", retval);
        UA_Server_delete(server);
        curl_global_cleanup();
        return EXIT_FAILURE;
    }

    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                "server startup complete, sockets should be listening");

    for(size_t i = 0; i < g_meterCount; i++) {
    MeterInfo *meter = &g_meters[i];

    /* Если для типа счётчика нет ни одной строки в tags.csv,
       счётчик вообще не показываем в OPC UA дереве */
    if(!has_mapping_for_device_type(meter->type))
        continue;

    char meterNodeIdStr[128];
    snprintf(meterNodeIdStr, sizeof(meterNodeIdStr), "meter|%d", meter->id);

    char meterDisplay[256];
    if(meter->addr[0] != '\0') {
        snprintf(meterDisplay, sizeof(meterDisplay), "%s_%s",
                 meter->typeName[0] ? meter->typeName : "Meter",
                 meter->addr);
    } else {
        snprintf(meterDisplay, sizeof(meterDisplay), "%s",
                 meter->typeName[0] ? meter->typeName : "Meter");
    }

    UA_NodeId meterNodeId;
    UA_ObjectAttributes oAttr = UA_ObjectAttributes_default;
    oAttr.displayName = UA_LOCALIZEDTEXT_ALLOC("ru-RU", meterDisplay);

    retval = UA_Server_addObjectNode(
        server,
        UA_NODEID_STRING(nsIdx, meterNodeIdStr),
        metersFolderId,
        UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
        UA_QUALIFIEDNAME_ALLOC(nsIdx, meterDisplay),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE),
        oAttr,
        NULL,
        &meterNodeId
    );

    UA_LocalizedText_clear(&oAttr.displayName);

    if(retval != UA_STATUSCODE_GOOD)
        continue;

    for(size_t j = 0; j < g_mappingCount; j++) {
    TagMapping *map = &g_mappings[j];

    if(map->device_type != meter->type)
        continue;

    UA_Boolean isGetTime = (strcmp(map->measure, "GetTime") == 0);
    UA_Boolean isMoment = (strncmp(map->measure, "ElMoment", 8) == 0);

    if(!isGetTime && !isMoment)
        continue;

    TagContext *ctx = (TagContext *)UA_malloc(sizeof(TagContext));
    if(!ctx)
        continue;

    memset(ctx, 0, sizeof(*ctx));
    ctx->meter_id = meter->id;
    strncpy(ctx->measure, map->measure, sizeof(ctx->measure) - 1);
    strncpy(ctx->api_tag, map->api_tag, sizeof(ctx->api_tag) - 1);
    ctx->is_datetime = isGetTime;
    ctx->last_value = 0.0;
    ctx->has_last_value = false;
    ctx->last_dt_value = 0;
    ctx->has_last_dt_value = false;

    char safeTag[STRSZ];
    if(map->api_tag[0] != '\0')
        strncpy(safeTag, map->api_tag, sizeof(safeTag) - 1);
    else
        strncpy(safeTag, "no_tag", sizeof(safeTag) - 1);
    safeTag[sizeof(safeTag) - 1] = '\0';

    char varNodeIdStr[384];
    snprintf(varNodeIdStr, sizeof(varNodeIdStr),
             "%s|%s|%d", map->measure, safeTag, meter->id);

    UA_NodeId varNodeId = UA_NODEID_STRING(nsIdx, varNodeIdStr);
    UA_QualifiedName qn = UA_QUALIFIEDNAME_ALLOC(nsIdx, map->display);

    UA_VariableAttributes vAttr = UA_VariableAttributes_default;
    vAttr.displayName = UA_LOCALIZEDTEXT_ALLOC("ru-RU", map->display);
    vAttr.accessLevel = UA_ACCESSLEVELMASK_READ;
    vAttr.userAccessLevel = UA_ACCESSLEVELMASK_READ;
    vAttr.valueRank = -1;

    if(isGetTime) {
        UA_DateTime initDt = UA_DateTime_now();
        vAttr.dataType = UA_TYPES[UA_TYPES_DATETIME].typeId;
        UA_Variant_setScalarCopy(&vAttr.value, &initDt, &UA_TYPES[UA_TYPES_DATETIME]);
    } else {
        double initValue = 0.0;
        vAttr.dataType = UA_TYPES[UA_TYPES_DOUBLE].typeId;
        UA_Variant_setScalarCopy(&vAttr.value, &initValue, &UA_TYPES[UA_TYPES_DOUBLE]);
    }

    /* 1. Сначала создаём обычный VariableNode */
    retval = UA_Server_addVariableNode(
        server,
        varNodeId,
        meterNodeId,
        UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
        qn,
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
        vAttr,
        NULL,
        NULL
    );

    if(retval != UA_STATUSCODE_GOOD) {
        printf("TAG FAILED (add variable): meter_id=%d measure=%s api_tag=%s display=%s status=0x%08x\n",
               meter->id,
               map->measure,
               map->api_tag,
               map->display,
               retval);
        UA_QualifiedName_clear(&qn);
        UA_VariableAttributes_clear(&vAttr);
        UA_free(ctx);
        continue;
    }

    /* 2. Потом навешиваем DataSource */
    UA_DataSource ds;
    ds.read = readCurrentValue;
    ds.write = NULL;

    retval = UA_Server_setVariableNode_dataSource(server, varNodeId, ds);

    if(retval != UA_STATUSCODE_GOOD) {
        printf("TAG FAILED (set datasource): meter_id=%d measure=%s api_tag=%s display=%s status=0x%08x\n",
               meter->id,
               map->measure,
               map->api_tag,
               map->display,
               retval);
        UA_QualifiedName_clear(&qn);
        UA_VariableAttributes_clear(&vAttr);
        UA_free(ctx);
        continue;
    }

    /* 3. Привязываем context к node */
    retval = UA_Server_setNodeContext(server, varNodeId, ctx);
    if(retval != UA_STATUSCODE_GOOD) {
        printf("TAG FAILED (set context): meter_id=%d measure=%s api_tag=%s display=%s status=0x%08x\n",
               meter->id,
               map->measure,
               map->api_tag,
               map->display,
               retval);
        UA_QualifiedName_clear(&qn);
        UA_VariableAttributes_clear(&vAttr);
        UA_free(ctx);
        continue;
    }

    printf("TAG CREATED: meter_id=%d type=%d measure=%s api_tag=%s display=%s\n",
           meter->id,
           meter->type,
           map->measure,
           map->api_tag,
           map->display);

    UA_QualifiedName_clear(&qn);
    UA_VariableAttributes_clear(&vAttr);
}
}
}

    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                "starting main server loop on opc.tcp://0.0.0.0:4840");

    while(running) {
        UA_Server_run_iterate(server, true);
        usleep(10000);
    }

    UA_Server_run_shutdown(server);
    UA_Server_delete(server);
    curl_global_cleanup();

    return EXIT_SUCCESS;
}