#include "tag_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
trim(char *s) {
    size_t len = strlen(s);
    while(len > 0 &&
          (s[len - 1] == '\n' || s[len - 1] == '\r' ||
           s[len - 1] == ' '  || s[len - 1] == '\t'))
        s[--len] = '\0';
}

static int
split_csv7(char *line,
           char **c1, char **c2, char **c3,
           char **c4, char **c5, char **c6, char **c7) {
    char *p1 = line;
    char *p2 = strchr(p1, ',');
    if(!p2) return 0;
    *p2 = '\0'; p2++;

    char *p3 = strchr(p2, ',');
    if(!p3) return 0;
    *p3 = '\0'; p3++;

    char *p4 = strchr(p3, ',');
    if(!p4) return 0;
    *p4 = '\0'; p4++;

    char *p5 = strchr(p4, ',');
    if(!p5) return 0;
    *p5 = '\0'; p5++;

    char *p6 = strchr(p5, ',');
    if(!p6) return 0;
    *p6 = '\0'; p6++;

    char *p7 = strchr(p6, ',');
    if(!p7) return 0;
    *p7 = '\0'; p7++;

    *c1 = p1;
    *c2 = p2;
    *c3 = p3;
    *c4 = p4;
    *c5 = p5;
    *c6 = p6;
    *c7 = p7;
    return 1;
}

TagKind
parse_tag_kind(const char *s) {
    if(!s || !s[0])
        return TAGKIND_UNKNOWN;

    if(strcmp(s, "current") == 0)
        return TAGKIND_CURRENT;

    if(strcmp(s, "history") == 0)
        return TAGKIND_HISTORY;

    return TAGKIND_UNKNOWN;
}

ValueType
parse_value_type(const char *s) {
    if(!s || !s[0])
        return VALTYPE_DOUBLE;

    if(strcmp(s, "datetime") == 0)
        return VALTYPE_DATETIME;

    return VALTYPE_DOUBLE;
}

int
load_mapping_csv(const char *filename,
                 TagMapping *mappings,
                 size_t max_mappings,
                 size_t *out_count) {
    if(!filename || !mappings || !out_count)
        return -1;

    FILE *f = fopen(filename, "r");
    if(!f) {
        perror("fopen tags.csv");
        return -1;
    }

    char line[1024];

    if(!fgets(line, sizeof(line), f)) {
        fclose(f);
        return -1;
    }

    size_t count = 0;

    while(fgets(line, sizeof(line), f) && count < max_mappings) {
        trim(line);
        if(line[0] == '\0')
            continue;

        char *col1, *col2, *col3, *col4, *col5, *col6, *col7;
        if(!split_csv7(line, &col1, &col2, &col3, &col4, &col5, &col6, &col7))
            continue;

        TagMapping *m = &mappings[count];
        memset(m, 0, sizeof(*m));

        m->device_type = atoi(col1);
        m->channel = atoi(col2);  /* 0 = без канала */

        strncpy(m->measure, col3, sizeof(m->measure) - 1);
        strncpy(m->api_tag, col4, sizeof(m->api_tag) - 1);
        strncpy(m->display, col5, sizeof(m->display) - 1);

        m->kind = parse_tag_kind(col6);
        m->value_type = parse_value_type(col7);

        if(m->kind == TAGKIND_UNKNOWN)
            continue;

        count++;
    }

    fclose(f);

    *out_count = count;

    printf("Mappings count: %zu\n", count);
    for(size_t i = 0; i < count; i++) {
        printf("MAP[%zu]: type=%d channel=%d measure=%s api_tag=%s display=%s kind=%d vtype=%d\n",
               i,
               mappings[i].device_type,
               mappings[i].channel,
               mappings[i].measure,
               mappings[i].api_tag,
               mappings[i].display,
               (int)mappings[i].kind,
               (int)mappings[i].value_type);
    }

    return 0;
}

UA_Boolean
has_mapping_for_device_type(const TagMapping *mappings,
                            size_t mapping_count,
                            int device_type) {
    for(size_t i = 0; i < mapping_count; i++) {
        if(mappings[i].device_type == device_type)
            return true;
    }
    return false;
}
