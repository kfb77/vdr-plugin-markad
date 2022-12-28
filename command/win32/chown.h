#pragma once

/* no posix owner, no posix chmod() */
int chown(const char* pathname, int owner, int group);
