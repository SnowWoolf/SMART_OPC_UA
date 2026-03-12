#ifndef TAG_CONFIG_H
#define TAG_CONFIG_H

#include "common.h"

int load_mapping_csv(const char *filename,
                     TagMapping *mappings,
                     size_t max_mappings,
                     size_t *out_count);

UA_Boolean has_mapping_for_device_type(const TagMapping *mappings,
                                       size_t mapping_count,
                                       int device_type);

TagKind detect_tag_kind(const char *measure);
ValueType detect_value_type(const char *measure);

#endif