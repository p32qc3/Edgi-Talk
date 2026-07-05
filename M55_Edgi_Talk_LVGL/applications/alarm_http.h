#ifndef EDGI_ALARM_HTTP_H
#define EDGI_ALARM_HTTP_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * POST alarm JSON to cloud (same payload as MSH alarm_post).
 * JSON 含 alarm_code（英文）、alarm_msg（UTF-8 中文，与 M33 五类告警对应）。
 * @return 0 on success, negative on failure.
 */
int edgi_alarm_http_post(const char *alarm_code, int level);

#ifdef __cplusplus
}
#endif

#endif /* EDGI_ALARM_HTTP_H */
