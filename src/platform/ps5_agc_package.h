#ifndef PS5_AGC_PACKAGE_H
#define PS5_AGC_PACKAGE_H

#include <stddef.h>
#include <stdint.h>

#include "psbc_compile.h"

int ps5_agc_package_build(const PsbcShaderOutput *shader,
                          uint32_t esgs_ring_itemsize,
                          uint8_t **package, size_t *package_size);

#endif
