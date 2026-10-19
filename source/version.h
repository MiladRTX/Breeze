#pragma once

#include <stddef.h>

/* -1, 0 or 1 as version a is lower than, equal to or higher than b, part by
 * part: "1.8" < "1.8.1" < "1.9" < "1.10". A leading 'v' and anything after
 * the numbers are ignored. */
int fc_version_cmp(const char *a, const char *b);

/* The version in a GitHub release document's "tag_name" ("v1.9" -> "1.9")
 * into out. Returns 0, or -1 when the tag is missing, cut off (a partial
 * read) or not a version (digits and dots, starting with a digit). */
int fc_release_tag(const char *json, char *out, size_t n);
