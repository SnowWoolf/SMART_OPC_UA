#include "common.h"
#include "api_config.h"

#include <stdio.h>
#include <string.h>

char API_URL[STRSZ]  = "http://localhost";
char LOGIN[STRSZ]    = "admin";
char PASSWORD[STRSZ] = "admin";
char PROTOCOL[STRSZ] = "40";

static void
trim_line(char *s) {
    if(!s)
        return;

    for(char *p = s; *p; ++p) {
        if(*p == '\r' || *p == '\n') {
            *p = '\0';
            break;
        }
    }
}

static void
copy_cfg_value(char *dst, const char *src) {
    if(!dst || !src)
        return;

    strncpy(dst, src, STRSZ - 1);
    dst[STRSZ - 1] = '\0';
}

void
load_api_config(void) {
    FILE *f = fopen(API_CFG_FILE, "r");
    if(!f) {
        printf("API config not found: %s, using defaults\n", API_CFG_FILE);
        printf("URL=%s\nLOGIN=%s\nPROTOCOL=%s\n", API_URL, LOGIN, PROTOCOL);
        return;
    }

    char line[256];

    while(fgets(line, sizeof(line), f)) {
        trim_line(line);

        if(line[0] == '\0' || line[0] == '#')
            continue;

        char *eq = strchr(line, '=');
        if(!eq)
            continue;

        *eq = '\0';
        char *key = line;
        char *val = eq + 1;

        if(strcmp(key, "URL") == 0) {
            copy_cfg_value(API_URL, val);
        } else if(strcmp(key, "LOGIN") == 0) {
            copy_cfg_value(LOGIN, val);
        } else if(strcmp(key, "PASSWORD") == 0) {
            copy_cfg_value(PASSWORD, val);
        } else if(strcmp(key, "PROTOCOL") == 0) {
            copy_cfg_value(PROTOCOL, val);
        }
    }

    fclose(f);

    printf("API config loaded from %s\n", API_CFG_FILE);
    printf("URL=%s\n", API_URL);
    printf("LOGIN=%s\n", LOGIN);
    printf("PROTOCOL=%s\n", PROTOCOL);
}