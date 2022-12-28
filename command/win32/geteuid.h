#pragma once

/* posix user IDs dont exist in WIN32 */
int geteuid(void);
