#include "opc_nodes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api_client.h"
#include "tag_config.h"
#include "opc_history.h"

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

    if(ctx->kind != TAGKIND_CURRENT) {
        dataValue->hasValue = false;
        dataValue->status = UA_STATUSCODE_BADNOTREADABLE;

        if(includeSourceTimeStamp) {
            dataValue->hasSourceTimestamp = true;
            dataValue->sourceTimestamp = UA_DateTime_now();
        }
        return UA_STATUSCODE_GOOD;
    }

    if(isInternalSession(sessionId)) {
        if(ctx->value_type == VALTYPE_DATETIME) {
            if(ctx->has_last_dt_value) {
                UA_Variant_setScalarCopy(&dataValue->value, &ctx->last_dt_value,
                                         &UA_TYPES[UA_TYPES_DATETIME]);
                dataValue->hasValue = true;
                dataValue->status = UA_STATUSCODE_GOOD;
            } else {
                dataValue->status = UA_STATUSCODE_BADWAITINGFORINITIALDATA;
            }
        } else {
            if(ctx->has_last_value) {
                UA_Variant_setScalarCopy(&dataValue->value, &ctx->last_value,
                                         &UA_TYPES[UA_TYPES_DOUBLE]);
                dataValue->hasValue = true;
                dataValue->status = UA_STATUSCODE_GOOD;
            } else {
                dataValue->status = UA_STATUSCODE_BADWAITINGFORINITIALDATA;
            }
        }

        if(includeSourceTimeStamp) {
            dataValue->hasSourceTimestamp = true;
            dataValue->sourceTimestamp = UA_DateTime_now();
        }

        return UA_STATUSCODE_GOOD;
    }

    if(ctx->value_type == VALTYPE_DATETIME) {
        UA_DateTime dt;
        int rc = api_read_current_datetime(ctx->meter_id, &dt);

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
            }
            dataValue->status = UA_STATUSCODE_BADNODATA;
        }
    } else {
        double value = 0.0;
        int rc = api_read_current_double(ctx->meter_id, ctx->measure, ctx->api_tag, &value);

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

UA_StatusCode
create_opc_nodes(UA_Server *server,
                 UA_UInt16 nsIdx,
                 const MeterInfo *meters,
                 size_t meter_count,
                 const TagMapping *mappings,
                 size_t mapping_count) {
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

        if(retval != UA_STATUSCODE_GOOD)
            return retval;
    }

    for(size_t i = 0; i < meter_count; i++) {
        const MeterInfo *meter = &meters[i];

        if(!has_mapping_for_device_type(mappings, mapping_count, meter->type))
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

        UA_StatusCode retval = UA_Server_addObjectNode(
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

        for(size_t j = 0; j < mapping_count; j++) {
            const TagMapping *map = &mappings[j];

            if(map->device_type != meter->type)
                continue;

            TagContext *ctx = (TagContext *)UA_malloc(sizeof(TagContext));
            if(!ctx)
                continue;

            memset(ctx, 0, sizeof(*ctx));
            ctx->meter_id = meter->id;
            ctx->device_type = meter->type;
            strncpy(ctx->meter_name, meterDisplay, sizeof(ctx->meter_name) - 1);
            strncpy(ctx->measure, map->measure, sizeof(ctx->measure) - 1);
            strncpy(ctx->api_tag, map->api_tag, sizeof(ctx->api_tag) - 1);
            strncpy(ctx->display, map->display, sizeof(ctx->display) - 1);
            ctx->kind = map->kind;
            ctx->value_type = map->value_type;

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
            ctx->nodeId = varNodeId;

            UA_QualifiedName qn = UA_QUALIFIEDNAME_ALLOC(nsIdx, map->display);

            UA_VariableAttributes vAttr = UA_VariableAttributes_default;
            vAttr.displayName = UA_LOCALIZEDTEXT_ALLOC("ru-RU", map->display);
            vAttr.accessLevel = UA_ACCESSLEVELMASK_READ;
			vAttr.userAccessLevel = UA_ACCESSLEVELMASK_READ;

			if(map->kind != TAGKIND_CURRENT) {
				vAttr.accessLevel |= UA_ACCESSLEVELMASK_HISTORYREAD;
				vAttr.userAccessLevel |= UA_ACCESSLEVELMASK_HISTORYREAD;
			}

			vAttr.valueRank = -1;
			vAttr.historizing = (map->kind != TAGKIND_CURRENT);

            if(map->value_type == VALTYPE_DATETIME) {
                UA_DateTime initDt = UA_DateTime_now();
                vAttr.dataType = UA_TYPES[UA_TYPES_DATETIME].typeId;
                UA_Variant_setScalarCopy(&vAttr.value, &initDt, &UA_TYPES[UA_TYPES_DATETIME]);
            } else {
                double initValue = 0.0;
                vAttr.dataType = UA_TYPES[UA_TYPES_DOUBLE].typeId;
                UA_Variant_setScalarCopy(&vAttr.value, &initValue, &UA_TYPES[UA_TYPES_DOUBLE]);
            }

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
                       meter->id, map->measure, map->api_tag, map->display, retval);
                UA_QualifiedName_clear(&qn);
                UA_VariableAttributes_clear(&vAttr);
                UA_free(ctx);
                continue;
            }

            UA_DataSource ds;
            ds.read = readCurrentValue;
            ds.write = NULL;

            retval = UA_Server_setVariableNode_dataSource(server, varNodeId, ds);
            if(retval != UA_STATUSCODE_GOOD) {
                printf("TAG FAILED (set datasource): meter_id=%d measure=%s api_tag=%s display=%s status=0x%08x\n",
                       meter->id, map->measure, map->api_tag, map->display, retval);
                UA_QualifiedName_clear(&qn);
                UA_VariableAttributes_clear(&vAttr);
                UA_free(ctx);
                continue;
            }

            retval = UA_Server_setNodeContext(server, varNodeId, ctx);
            if(retval != UA_STATUSCODE_GOOD) {
                printf("TAG FAILED (set context): meter_id=%d measure=%s api_tag=%s display=%s status=0x%08x\n",
                       meter->id, map->measure, map->api_tag, map->display, retval);
                UA_QualifiedName_clear(&qn);
                UA_VariableAttributes_clear(&vAttr);
                UA_free(ctx);
                continue;
            }

            if(map->kind != TAGKIND_CURRENT)
                opc_history_register_node(server, ctx);

            printf("TAG CREATED: meter_id=%d type=%d measure=%s api_tag=%s display=%s kind=%d\n",
                   meter->id, meter->type, map->measure, map->api_tag, map->display, (int)map->kind);

            UA_QualifiedName_clear(&qn);
            UA_VariableAttributes_clear(&vAttr);
        }
    }

    return UA_STATUSCODE_GOOD;
}