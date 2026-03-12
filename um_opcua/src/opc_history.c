#include "opc_history.h"

#include <stdio.h>

void
opc_history_init(UA_Server *server) {
    (void)server;
    printf("opc_history_init: history backend scaffold loaded\n");
}

void
opc_history_register_node(UA_Server *server, const TagContext *ctx) {
    (void)server;
    (void)ctx;
}