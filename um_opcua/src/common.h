#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <open62541/plugin/log_stdout.h>

#define API_URL   "http://localhost"
#define LOGIN     "admin"
#define PASSWORD  "admin"
#define PROTOCOL  "40"
#define CSV_FILE  "../config/tags.csv"

#define MAX_METERS       256
#define MAX_MAPPINGS     4096
#define MAX_ARCH_POINTS  4096
#define STRSZ            128

typedef enum {
    TAGKIND_UNKNOWN = 0,
    TAGKIND_CURRENT,
    TAGKIND_HISTORY_DAY,
    TAGKIND_HISTORY_MONTH
} TagKind;

typedef enum {
    VALTYPE_DOUBLE = 0,
    VALTYPE_DATETIME
} ValueType;

typedef struct {
    int id;
    int type;
    char typeName[STRSZ];
    char addr[STRSZ];
    char serial[STRSZ];
} MeterInfo;

typedef struct {
    int device_type;
    char measure[STRSZ];
    char api_tag[STRSZ];
    char display[STRSZ];
    TagKind kind;
    ValueType value_type;
} TagMapping;

typedef struct {
    UA_DateTime ts;
    double value;
    UA_Boolean has_value;
} ArchivePoint;

typedef struct {
    ArchivePoint points[MAX_ARCH_POINTS];
    size_t count;
} ArchiveResult;

typedef struct {
    int meter_id;
    int device_type;
    char meter_name[STRSZ];

    char measure[STRSZ];
    char api_tag[STRSZ];
    char display[STRSZ];

    TagKind kind;
    ValueType value_type;

    UA_NodeId nodeId;

    double last_value;
    UA_Boolean has_last_value;

    UA_DateTime last_dt_value;
    UA_Boolean has_last_dt_value;
} TagContext;

typedef struct {
    MeterInfo meters[MAX_METERS];
    size_t meter_count;

    TagMapping mappings[MAX_MAPPINGS];
    size_t mapping_count;
} AppConfig;

typedef struct {
    char *data;
    size_t size;
} HttpBuffer;

#endif