#include "opc_history.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <open62541/plugin/historydatabase.h>

#include "api_client.h"

#define MAX_HISTORY_NODES 4096

typedef struct {
    UA_Boolean used;
    UA_NodeId nodeId;
} HistoryNodeEntry;

typedef struct {
    HistoryNodeEntry entries[MAX_HISTORY_NODES];
    size_t count;
} HistoryRegistry;

static HistoryRegistry g_registry;

static time_t
ua_datetime_to_unix_seconds(UA_DateTime dt) {
    UA_DateTimeStruct dts = UA_DateTime_toStruct(dt);

    struct tm tmv;
    memset(&tmv, 0, sizeof(tmv));
    tmv.tm_year = (int)dts.year - 1900;
    tmv.tm_mon  = (int)dts.month - 1;
    tmv.tm_mday = (int)dts.day;
    tmv.tm_hour = (int)dts.hour;
    tmv.tm_min  = (int)dts.min;
    tmv.tm_sec  = (int)dts.sec;
    tmv.tm_isdst = 0;

    return timegm(&tmv);
}

static UA_Boolean
history_node_registered(const UA_NodeId *nodeId) {
    if(!nodeId)
        return false;

    for(size_t i = 0; i < g_registry.count; i++) {
        if(!g_registry.entries[i].used)
            continue;

        if(UA_NodeId_equal(&g_registry.entries[i].nodeId, nodeId))
            return true;
    }

    return false;
}

static void
history_database_clear(UA_HistoryDatabase *hdb) {
    (void)hdb;

    for(size_t i = 0; i < g_registry.count; i++) {
        if(g_registry.entries[i].used) {
            UA_NodeId_clear(&g_registry.entries[i].nodeId);
            g_registry.entries[i].used = false;
        }
    }

    g_registry.count = 0;
}

static void
history_database_setValue(UA_Server *server,
                          void *hdbContext,
                          const UA_NodeId *sessionId,
                          void *sessionContext,
                          const UA_NodeId *nodeId,
                          UA_Boolean historizing,
                          const UA_DataValue *value) {
    (void)server;
    (void)hdbContext;
    (void)sessionId;
    (void)sessionContext;
    (void)nodeId;
    (void)historizing;
    (void)value;

    /* Пока ничего не сохраняем локально.
       История читается напрямую из внешнего API /meter/data/arch */
}

static void
history_database_readRaw(UA_Server *server,
                         void *hdbContext,
                         const UA_NodeId *sessionId,
                         void *sessionContext,
                         const UA_RequestHeader *requestHeader,
                         const UA_ReadRawModifiedDetails *historyReadDetails,
                         UA_TimestampsToReturn timestampsToReturn,
                         UA_Boolean releaseContinuationPoints,
                         size_t nodesToReadSize,
                         const UA_HistoryReadValueId *nodesToRead,
                         UA_HistoryReadResponse *response,
                         UA_HistoryData * const * const historyData) {
    (void)hdbContext;
    (void)sessionId;
    (void)sessionContext;
    (void)requestHeader;
    (void)releaseContinuationPoints;

    if(!server || !historyReadDetails || !nodesToRead || !response || !historyData)
        return;

    for(size_t i = 0; i < nodesToReadSize; i++) {
        const UA_NodeId *nodeId = &nodesToRead[i].nodeId;
        UA_HistoryData *out = historyData[i];

        if(!out)
            continue;

        UA_HistoryData_clear(out);

        if(!history_node_registered(nodeId)) {
            response->results[i].statusCode = UA_STATUSCODE_BADHISTORYOPERATIONUNSUPPORTED;
            continue;
        }

        void *nodeContext = NULL;
        UA_StatusCode rc = UA_Server_getNodeContext(server, *nodeId, &nodeContext);
        if(rc != UA_STATUSCODE_GOOD || !nodeContext) {
            response->results[i].statusCode = UA_STATUSCODE_BADNODEIDUNKNOWN;
            continue;
        }

        TagContext *ctx = (TagContext *)nodeContext;
        if(ctx->kind == TAGKIND_CURRENT) {
            response->results[i].statusCode = UA_STATUSCODE_BADHISTORYOPERATIONUNSUPPORTED;
            continue;
        }

        time_t start_unix = ua_datetime_to_unix_seconds(historyReadDetails->startTime);
        time_t end_unix   = ua_datetime_to_unix_seconds(historyReadDetails->endTime);

        UA_Boolean reverse = false;
        if(start_unix > end_unix) {
            time_t tmp = start_unix;
            start_unix = end_unix;
            end_unix = tmp;
            reverse = true;
        }

        ArchiveResult ar;
        memset(&ar, 0, sizeof(ar));

        ArchiveResult ar;
        memset(&ar, 0, sizeof(ar));

        if(api_read_archive(ctx->meter_id,
                            ctx->measure,
                            ctx->api_tag,
                            ctx->channel,
                            (long)start_unix,
                            (long)end_unix,
                            &ar) != 0) {
        response->results[i].statusCode = UA_STATUSCODE_BADNODATA;
        continue;
        }
        
        size_t limit = ar.count;

        if(historyReadDetails->numValuesPerNode > 0 &&
           (size_t)historyReadDetails->numValuesPerNode < limit) {
            limit = (size_t)historyReadDetails->numValuesPerNode;
        }

        if(limit == 0) {
            response->results[i].statusCode = UA_STATUSCODE_GOOD;
            out->dataValues = NULL;
            out->dataValuesSize = 0;
            continue;
        }

        out->dataValues = (UA_DataValue *)UA_Array_new(limit, &UA_TYPES[UA_TYPES_DATAVALUE]);
        if(!out->dataValues) {
            response->results[i].statusCode = UA_STATUSCODE_BADOUTOFMEMORY;
            continue;
        }

        out->dataValuesSize = limit;
        UA_DateTime serverNow = UA_DateTime_now();

        for(size_t j = 0; j < limit; j++) {
            size_t srcIndex = reverse ? (ar.count - 1 - j) : j;
            ArchivePoint *pt = &ar.points[srcIndex];
            UA_DataValue *dv = &out->dataValues[j];

            UA_DataValue_init(dv);
            dv->status = pt->has_value ? UA_STATUSCODE_GOOD : UA_STATUSCODE_BADNODATA;

            if(pt->has_value) {
                UA_Variant_setScalarCopy(&dv->value, &pt->value, &UA_TYPES[UA_TYPES_DOUBLE]);
                dv->hasValue = true;
            }

            if(timestampsToReturn == UA_TIMESTAMPSTORETURN_SOURCE ||
               timestampsToReturn == UA_TIMESTAMPSTORETURN_BOTH) {
                dv->sourceTimestamp = pt->ts;
                dv->hasSourceTimestamp = true;
            }

            if(timestampsToReturn == UA_TIMESTAMPSTORETURN_SERVER ||
               timestampsToReturn == UA_TIMESTAMPSTORETURN_BOTH) {
                dv->serverTimestamp = serverNow;
                dv->hasServerTimestamp = true;
            }
        }

        response->results[i].statusCode = UA_STATUSCODE_GOOD;
    }
}

void
opc_history_init(UA_Server *server) {
    if(!server)
        return;

    memset(&g_registry, 0, sizeof(g_registry));

    UA_ServerConfig *config = UA_Server_getConfig(server);
    config->historizingEnabled = true;

    memset(&config->historyDatabase, 0, sizeof(config->historyDatabase));
    config->historyDatabase.context = &g_registry;
    config->historyDatabase.clear = history_database_clear;
    config->historyDatabase.setValue = history_database_setValue;
    config->historyDatabase.readRaw = history_database_readRaw;

    printf("opc_history_init: custom history database enabled\n");
}

void
opc_history_register_node(UA_Server *server, const TagContext *ctx) {
    (void)server;

    if(!ctx)
        return;

    if(ctx->kind == TAGKIND_CURRENT)
        return;

    if(g_registry.count >= MAX_HISTORY_NODES) {
        printf("opc_history_register_node: registry full for %s\n", ctx->display);
        return;
    }

    if(history_node_registered(&ctx->nodeId)) {
        printf("opc_history_register_node: already registered %s\n", ctx->display);
        return;
    }

    HistoryNodeEntry *entry = &g_registry.entries[g_registry.count];
    memset(entry, 0, sizeof(*entry));

    if(UA_NodeId_copy(&ctx->nodeId, &entry->nodeId) != UA_STATUSCODE_GOOD) {
        printf("opc_history_register_node: NodeId copy failed for %s\n", ctx->display);
        return;
    }

    entry->used = true;
    g_registry.count++;

    printf("opc_history_register_node: %s registered\n", ctx->display);
}
