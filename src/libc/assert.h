/* Minimal assert.h: assertions compile to nothing in firmware builds. */
#pragma once
#define assert(x) ((void)0)
