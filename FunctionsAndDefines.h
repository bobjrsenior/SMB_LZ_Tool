#pragma once
#ifndef FUNCTIONS_AND_DEFINES
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <stdint.h>

#define FUNCTIONS_AND_DEFINES
#define SMB2 0

#define SMBD 1

static inline uint32_t readBigInt(FILE *file) {
	uint32_t c1 = getc(file) << 24;
	uint32_t c2 = getc(file) << 16;
	uint32_t c3 = getc(file) << 8;
	uint32_t c4 = getc(file);
	return (c1 | c2 | c3 | c4);
}

static inline uint32_t readLittleInt(FILE *file) {
	uint32_t c1 = getc(file);
	uint32_t c2 = getc(file) << 8;
	uint32_t c3 = getc(file) << 16;
	uint32_t c4 = getc(file) << 24;
	return (c1 | c2 | c3 | c4);
}

static inline uint16_t readBigShort(FILE *file) {
	uint16_t c1 = (uint16_t)getc(file) << 8;
	uint16_t c2 = (uint16_t)getc(file);
	return (uint16_t)(c1 | c2);
}

static inline uint16_t readLittleShort(FILE *file) {
	uint16_t c1 = (uint16_t)getc(file);
	uint16_t c2 = (uint16_t)getc(file) << 8;
	return (uint16_t)(c1 | c2);
}

static inline void writeBigInt(FILE *file, uint32_t value) {
	putc((value >> 24), file);
	putc((value >> 16), file);
	putc((value >> 8), file);
	putc((value), file);
}

static inline void writeLittleIntData(uint8_t* data, int offset, uint32_t num) {
	data[offset] = (uint8_t)(num);
	data[offset + 1] = (uint8_t)(num >> 8);
	data[offset + 2] = (uint8_t)(num >> 16);
	data[offset + 3] = (uint8_t)(num >> 24);
}

static inline void writeLittleInt(FILE *file, uint32_t value) {
	putc((value), file);
	putc((value >> 8), file);
	putc((value >> 16), file);
	putc((value >> 24), file);
}

static inline void writeBigShort(FILE *file, uint16_t value) {
	putc((value >> 8), file);
	putc((value), file);
}

static inline void writeLittleShort(FILE *file, uint16_t value) {
	putc((value), file);
	putc((value >> 8), file);
}

#endif // !FUNCTIONS_AND_DEFINES