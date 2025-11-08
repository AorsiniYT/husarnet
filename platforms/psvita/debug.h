#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void vita_debug_log(const char *s, ...);
void vita_log_connectivity(const char *event_type, const char *details);

#ifdef __cplusplus
}
#endif
