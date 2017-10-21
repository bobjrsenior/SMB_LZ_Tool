#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <omp.h>

#define NUM_THREADS 2

inline uint32_t readInt(FILE* file) {
	uint32_t c1 = (uint32_t)(getc(file));
	uint32_t c2 = (uint32_t)(getc(file) << 8);
	uint32_t c3 = (uint32_t)(getc(file) << 16);
	uint32_t c4 = (uint32_t)(getc(file) << 24);
	return (c1 | c2 | c3 | c4);
}

inline uint16_t readShort(FILE* file) {
	uint32_t c1 = (uint32_t)(getc(file) << 8);
	uint32_t c2 = (uint32_t)(getc(file));
	return (uint16_t)(c1 | c2);
}

inline uint32_t readIntData(char* data, int offset) {
	return (uint32_t)((data[offset] << 24) | (data[offset + 1] << 16) | (data[offset + 2] << 8) + (data[offset + 3]));
}

inline void writeLittleIntData(uint8_t* data, int offset, uint32_t num) {
	data[offset] = (uint8_t)(num);
	data[offset + 1] = (uint8_t)(num >> 8);
	data[offset + 2] = (uint8_t)(num >> 16);
	data[offset + 3] = (uint8_t)(num >> 24);
}

inline void writeLittleInt(FILE* file, uint32_t num) {
	putc((uint8_t)(num), file);
	putc((uint8_t)(num >> 8), file);
	putc((uint8_t)(num >> 16), file);
	putc((uint8_t)(num >> 24), file);
}

typedef struct {
	uint32_t length;
	uint32_t offset;
}ReferenceBlock;


void decompress(char* filename);

void compress(char* filename);

ReferenceBlock findMaxReference(const uint8_t* fileData, uint32_t filesize, uint32_t maxOffset);

int main(int argc, char* argv[]) {
	if (argc <= 1) {
		printf("Add lz paths as command line params");
	}

	omp_set_num_threads(NUM_THREADS);

	// Go through every command line arg
	for (int i = 1; i < argc; ++i) {
		int strLen = (int)strlen(argv[i]);
		if (strLen > 0) {
			char fileCheck = argv[i][strLen - 1];
			if (fileCheck == 'z') {
				decompress(argv[i]);
			}
			else if (fileCheck == 'w') {
				compress(argv[i]);
			}
			else {
				printf("Unable to identify whether to compress or decompress file\nEnter 'D' to decompress, 'C' to compress, or any other character to skip\n");
				int answer = (char)getc(stdin);
				if (answer == 'D' || answer == 'd') {
					decompress(argv[i]);
				}
				else if (answer == 'C' || answer == 'c') {
					compress(argv[i]);
				}
				else {
					continue;
				}
			}
		}
	}
	return 0;
}

void decompress(char* filename) {
	// Try to open it
	FILE* lz = fopen(filename, "rb");
	if (lz == NULL) {
		printf("ERROR: File not found: %s\n", filename);
		return;
	}
	printf("Decompressing %s\n", filename);

	// Create a temp file for the unfixed lz (SMB lz has a slightly different header than FF7 LZS)
	FILE* normal = tmpfile();

	// Unfix the header (Turn it back into normal FF7 LZSS)
	int csize = (int)readInt(lz) - 8;
	// Filesize of the uncompressed data
	int dataSize = (int)readInt(lz);
	putc(csize & 0xFF, normal);
	putc((csize >> 8) & 0xFF, normal);
	putc((csize >> 16) & 0xFF, normal);
	putc((csize >> 24) & 0xFF, normal);
	for (int j = 0; j < (int)(csize); j++) {
		char c = (char)getc(lz);
		putc(c, normal);
	}
	fclose(lz);
	fflush(normal);
	fseek(normal, 0, SEEK_SET);

	// Make the output file name
	char outfileName[512];
	sscanf(filename, "%507s", outfileName);
	{
		int nameLength = (int)strlen(outfileName);
		outfileName[nameLength++] = '.';
		outfileName[nameLength++] = 'r';
		outfileName[nameLength++] = 'a';
		outfileName[nameLength++] = 'w';
		outfileName[nameLength++] = '\0';
	}

	// The size the the lzss data + 4 bytes for the header
	uint32_t filesize = readInt(normal) + 4;

	char * memBlock = (char*)malloc(sizeof(char) * (dataSize));
	int memPosition = 0;

	// Loop until we reach the end of the data or end of the file
	while ((unsigned)ftell(normal) < filesize && !feof(normal)) {

		// Read the first control block
		// Read right to left, each bit specifies how the the next 8 spots of data will be
		// 0 means write the byte directly to the output
		// 1 represents there will be reference (2 byte)
		uint8_t block = (uint8_t)getc(normal);

		// Go through every bit in the control block
		for (int j = 0; j < 8 && (unsigned)ftell(normal) < filesize && !feof(normal); ++j) {
			// Literal byte copy
			if (block & 0x01) {
				memBlock[memPosition] = (char)getc(normal);
				++memPosition;
			}// Reference
			else {
				uint16_t reference = readShort(normal);

				// Length is the last four bits + 3
				// Any less than a lengh of 3 i pointess since a reference takes up 3 bytes
				// Length is the last nibble (last 4 bits) of the 2 reference bytes
				int length = (reference & 0x000F) + 3;

				// Offset if is all 8 bits in the first reference byte and the first nibble (4 bits) in the second reference byte
				// The nibble from the second reference byte comes before the first reference byte
				// EX: reference bytes = 0x12 0x34
				//     offset = 0x312
				int offset = ((reference & 0xFF00) >> 8) | ((reference & 0x00F0) << 4);

				// Convert the offset to how many bytes away from the end of the buffer to start reading from
				int backSet = (memPosition - 18 - offset) & 0xFFF;

				// Calculate the actual location in the file
				int readLocation = memPosition - backSet;

				// Handle case where the offset is past the beginning of the file
				if (readLocation < 0) {
					// Determine how many zeros to write
					int amt = -readLocation;
					if (length <= amt) {
						amt = length;
					}
					// Write the zeros
					memset(&memBlock[memPosition], 0, sizeof(char) * amt);
					// Ajuest positions and number of bytes left to copy
					length -= amt;
					readLocation += amt;
					memPosition += amt;
				}

				// Copy the rest of the reference bytes
				while (length-- > 0) {
					memBlock[memPosition++] = memBlock[readLocation++];
				}


			}
			// Go to the next reference bit in the block
			block = (uint8_t) (block >> 1);
		}


	}

	// Open the output file and copy the data into it
	FILE* outfile = fopen(outfileName, "wb");
	fwrite(memBlock, sizeof(char), (size_t)dataSize, outfile);

	// Close files and free memory
	free(memBlock);
	fclose(outfile);
	fclose(normal);

	printf("Finished Decompressing %s\n", filename);
	return;
}

void compress(char* filename) {
	// Try to open it
	FILE* rawFile = fopen(filename, "rb");
	if (rawFile == NULL) {
		printf("ERROR: File not found: %s\n", filename);
		return;
	}
	printf("Compressing %s\n", filename);

	// Make the output file name
	char outfileName[512];
	sscanf(filename, "%507s", outfileName);
	{
		int nameLength = (int)strlen(outfileName);
		outfileName[nameLength++] = '.';
		outfileName[nameLength++] = 'l';
		outfileName[nameLength++] = 'z';
		outfileName[nameLength++] = '\0';
	}

	// Open the output file
	FILE* outfile = fopen(outfileName, "wb");

	fseek(rawFile, 0, SEEK_END);
	uint32_t filesize = (uint32_t)ftell(rawFile);
	fseek(rawFile, 0, SEEK_SET);
	int lastPercentDone = -1;

	// Allocate 4096 bytes in the beginning to avoid bounds checking
	// negative offsets count as 0, so it can be treated as normal
	// Allocate 18 bytes at the end to account for overrun at the end of the file
	uint8_t * raw = (uint8_t*)calloc((filesize + 1) + 4096 + 18, sizeof(uint8_t));
	//uint8_t *raw = (rawActualPtr + 4096);
	fread((raw + 4096), sizeof(uint8_t), filesize, rawFile);
	uint32_t rawPosition = 4096;
	uint32_t filesizeStandardized = filesize + 4096;

	uint8_t* comp = (uint8_t*)malloc(sizeof(uint8_t) * (int)(1.25f * filesize));
	
	fclose(rawFile);

	writeLittleIntData(comp, 4, filesize);

	// 8 bytes for the header and 1 byte for the first reference block
	uint32_t compPosition = 9;
	uint32_t posInBlock = 0;
	uint8_t curBlock = 0;
	uint32_t blockBackset = 1;

	while (rawPosition < filesizeStandardized) {
		float percentDone = (100.0f * rawPosition) / filesize;
		int intPercentDone = (int)percentDone;
		if (intPercentDone % 10 == 0 && intPercentDone != lastPercentDone) {
			printf("%d%% Completed\n", intPercentDone);
			lastPercentDone = intPercentDone;
		}
		++posInBlock;
		ReferenceBlock maxReference = findMaxReference(raw, filesizeStandardized, rawPosition);

		if (maxReference.length >= 3) {
			uint32_t backset = rawPosition - maxReference.offset;

			uint32_t offset = (rawPosition & 0xFFF) - 18 - backset;

			uint8_t leftByte = (offset & 0xFF);
			uint8_t rightByte = (((offset >> 8) & 0xF) << 4) | ((maxReference.length - 3) & 0xF);

			comp[compPosition] = leftByte;
			comp[compPosition + 1] = rightByte;

			compPosition += 2;

			rawPosition += maxReference.length;
			curBlock = (uint8_t)(curBlock | (0x0 << (uint8_t)posInBlock));
			blockBackset += 2;
		}// Raw byte copy
		else {
			comp[compPosition] = raw[rawPosition];

			curBlock = (uint8_t)(curBlock | (0x1 << (uint8_t)posInBlock));
			++rawPosition;
			++blockBackset;
			++compPosition;
		}

		if (posInBlock == 8) {
			comp[compPosition - blockBackset] = curBlock;

			posInBlock = 0;
			curBlock = 0;
			++compPosition;
			blockBackset = 1;
		}

	}

	// Make sure you don't have any data bytes without a reference block
	if (posInBlock != 0) {
		comp[compPosition - blockBackset] = curBlock;
	}

	// Write size of compressed data to header
	writeLittleIntData(comp, 0, (uint32_t)compPosition);

	// Write compressed data to file
	// Check if 4 byte aligned
	if (((((uint8_t) compPosition) & 0b00001111) ^ 0b1011) == 0b1111) {
		fwrite(comp, sizeof(uint32_t), compPosition >> 2, outfile);
	}
	else {
		fwrite(comp, sizeof(uint8_t), compPosition, outfile);
	}
	
	free(raw);
	free(comp);
	fclose(outfile);

	printf("Finished Decompressing %s\n", filename);
	return;
}

ReferenceBlock findMaxReference(const uint8_t* data, uint32_t filesize, uint32_t maxOffset) {
	ReferenceBlock maxReference = { 2, 0 };
	ReferenceBlock maxReferences[NUM_THREADS];
	maxReferences[0] = { 2, 0 };
	uint32_t startOffsets[NUM_THREADS];
	uint32_t maxOffsets[NUM_THREADS];

	uint32_t curOffset = maxOffset - 4095;

	if (curOffset < (4096 - 18)) {
		curOffset = (4096 - 18);
	}

	uint32_t maxLength = 18;
	if (maxOffset + maxLength >= filesize) {
		maxLength = filesize - maxOffset;
		if (maxLength < 3) {
			return maxReferences[0];
		}
	}

	uint32_t distance = maxOffset - curOffset;
	uint32_t threadDistance = distance >> (NUM_THREADS - 1);
	uint32_t curStartOffset = curOffset;
	for (int i = 0; i < NUM_THREADS; i++) {
		startOffsets[i] = curStartOffset;
		curStartOffset += threadDistance;
		maxOffsets[i] = curStartOffset;
		maxReferences[i] = { 0, 2 };
	}
	maxOffsets[NUM_THREADS - 1] = maxOffset;

	/*uint32_t kmpTable[19] = { 1 };
	for (uint32_t i = 0; i < maxLength; i++) {
		uint32_t skip = 1;
		for (uint32_t j = i - 1; j < 0xFF; j--) {
			if (data[maxOffset + i] == data[maxOffset + j]) {
				int good = 1;
				for (uint32_t k = 1; k <= j; k++) {
					if (data[maxOffset + j - k] != data[maxOffset + i - k]) {
						good = 0;
						skip += kmpTable[j + 1 - k];
						j -= kmpTable[j + 1 - k];
						break;
					}
				}
				if (good) break;
				continue;
			}
			skip++;
		}
		kmpTable[i + 1] = skip;
	}*/

	int continueChecking = 1;

#pragma omp parallel for
	for (int i = 0; i < NUM_THREADS; i++) {
		uint32_t localCurOffset = startOffsets[i];
		uint32_t localMaxOffset = maxOffsets[i];
		ReferenceBlock localMaxReference = { 2, 0 };

		while (localCurOffset < localMaxOffset && continueChecking) {
			// Greedy checking for max length is optimal
			// See Corollary 3.3.4 on page 44 of https://hal.archives-ouvertes.fr/tel-00804215/document
			// Alessio Langiu. Optimal Parsing for dictionary text compression. Other [cs.OH]. Universite
			//	Paris - Est, 2012. English. .
			// (Note that the last e in Universite has an accent aigu (looks like a forward tick),
			//   but is removed for ascii compatable source code)
			uint32_t curLength = 0;
			while (data[localCurOffset + curLength] == data[maxOffset + curLength] && curLength != maxLength) {
				curLength++;
			}

			if (curLength > localMaxReference.length) {
				localMaxReference.length = curLength;
				localMaxReference.offset = localCurOffset;
				if (curLength == maxLength) {
					continueChecking = 0;
					break;
				}
			}
			localCurOffset++;// += kmpTable[curLength];
		}

		maxReferences[i] = localMaxReference;
	}

	for (int i = 0; i < NUM_THREADS; i++) {
		if (maxReferences[i].length > maxReference.length) {
			maxReference = maxReferences[i];
		}
	}

	return maxReference;
}
