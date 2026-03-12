#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <curl/curl.h>

#include "common.h"
#include "api_client.h"
#include "tag_config.h"
#include "opc_nodes.h"
#include "opc_history.h"

static volatile UA_Boolean running = true;

static void
stopHandler(int sig) {
    (void)sig;
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "received ctrl-c");
    running = false;
}

int
main(void) {
    signal(SIGINT, stopHandler);
    signal(SIGTERM, stopHandler);

    curl_global_init(CURL_GLOBAL_DEFAULT);

    AppConfig app;
    memset(&app, 0, sizeof(app));

    if(api_login() != 0) {
        fprintf(stderr, "API login failed\n");
        curl_global_cleanup();
        return EXIT_FAILURE;
    }

    if(api_get_meters(app.meters, MAX_METERS, &app.meter_count) != 0) {
        fprintf(stderr, "Get meters failed\n");
        curl_global_cleanup();
        return EXIT_FAILURE;
    }

    if(load_mapping_csv(CSV_FILE, app.mappings, MAX_MAPPINGS, &app.mapping_count) != 0) {
        fprintf(stderr, "Load tags.csv failed\n");
        curl_global_cleanup();
        return EXIT_FAILURE;
    }

    UA_Server *server = UA_Server_new();
    UA_ServerConfig *config = UA_Server_getConfig(server);
    UA_ServerConfig_setDefault(config);

    UA_UInt16 nsIdx = UA_Server_addNamespace(server, "urn:um-smart-opcua");

    opc_history_init(server);

    UA_StatusCode retval = create_opc_nodes(server,
                                            nsIdx,
                                            app.meters,
                                            app.meter_count,
                                            app.mappings,
                                            app.mapping_count);
    if(retval != UA_STATUSCODE_GOOD) {
        fprintf(stderr, "create_opc_nodes failed: 0x%08x\n", retval);
        UA_Server_delete(server);
        curl_global_cleanup();
        return EXIT_FAILURE;
    }

    retval = UA_Server_run_startup(server);
    if(retval != UA_STATUSCODE_GOOD) {
        fprintf(stderr, "UA_Server_run_startup failed: 0x%08x\n", retval);
        UA_Server_delete(server);
        curl_global_cleanup();
        return EXIT_FAILURE;
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