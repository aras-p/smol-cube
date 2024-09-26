#pragma once

#include <stdint.h>
#include <stddef.h>

void Filter_Null(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);
void UnFilter_Null(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);

void Filter_Split(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);
void UnFilter_Split(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);

// Part 6
void Filter_A(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);
void UnFilter_A(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);

void Filter_D(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);
void UnFilter_D(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);

// Fetch process 16xN bytes at once
void Filter_H(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);
void UnFilter_H(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);
