#pragma once

#include <stdint.h>
#include <stddef.h>

void Filter_D(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);
void UnFilter_D(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);
