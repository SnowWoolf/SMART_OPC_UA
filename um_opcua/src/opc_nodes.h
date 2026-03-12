#ifndef OPC_NODES_H
#define OPC_NODES_H

#include "common.h"

UA_StatusCode create_opc_nodes(UA_Server *server,
                               UA_UInt16 nsIdx,
                               const MeterInfo *meters,
                               size_t meter_count,
                               const TagMapping *mappings,
                               size_t mapping_count);

#endif