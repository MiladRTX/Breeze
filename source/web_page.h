#pragma once

#include <stddef.h>

/* Embedded single-page control panel served at GET /. */
extern const char kWebPage[];
extern const size_t kWebPageLen;
/* Placeholder in kWebPage the server replaces with the write token. */
#define FC_WEB_TOKEN_MARK "@@FC_TOKEN@@"
