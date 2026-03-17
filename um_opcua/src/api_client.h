#ifndef API_CLIENT_H
#define API_CLIENT_H

#include "common.h"

int api_login(void);

int api_get_meters(MeterInfo *meters,
                   size_t max_meters,
                   size_t *out_count);

int api_read_current_double(int meter_id,
                            const char *measure,
                            const char *tag,
                            double *out_value);

int api_read_current_datetime(int meter_id,
                              UA_DateTime *out_dt);

int api_read_archive(int meter_id,
                     const char *measure,
                     const char *tag,
                     int channel,
                     long start_ts,
                     long end_ts,
                     ArchiveResult *out);

UA_Boolean parse_iso_datetime_to_ua(const char *iso,
                                    UA_DateTime *out_dt);

#endif
