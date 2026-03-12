#ifndef OPC_HISTORY_H
#define OPC_HISTORY_H

#include "common.h"

void opc_history_init(UA_Server *server);
void opc_history_register_node(UA_Server *server, const TagContext *ctx);

#endif