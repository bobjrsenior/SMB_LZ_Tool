#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

inline uint32_t readInt(FILE* file) {
	uint32_t c1 = getc(file);
	uint32_t c2 = getc(file) << 8;
	uint32_t c3 = getc(file) << 16;
	uint32_t c4 = getc(file) << 24;
	return (c1 | c2 | c3 | c4);
}

inline uint16_t readShort(FILE* file) {
	uint32_t c1 = getc(file) << 8;
	uint32_t c2 = getc(file);
	return (uint16_t)(c1 | c2);
}

inline uint32_t readIntData(char* data, int offset) {
	return (data[offset] << 24) | (data[offset + 1] << 16) | (data[offset + 2] << 8) + (data[offset + 3]);
}

inline void writeLittleIntData(char* data, int offset, uint32_t num) {
	data[offset] = (uint8_t)(num);
	data[offset + 1] = (uint8_t)(num >> 8);
	data[offset + 2] = (uint8_t)(num >> 16);
	data[offset + 3] = (uint8_t)(num >> 24);
}

inline void writeLittleInt(FILE* file, uint32_t num) {
	putc(num, file);
	putc(num >> 8, file);
	putc(num >> 16, file);
	putc(num >> 24, file);
}

typedef struct {
	int length;
	int offset;
}ReferenceBlock;


void decompress(char* filename);

void compress(char* filename);

ReferenceBlock findMaxReference(char* fileData, int filesize, int maxOffset);

int main(int argc, char* argv[]) {
	if (argc <= 1) {
		printf("Add lz paths as command line params");
	}

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
	uint32_t csize = readInt(lz) - 8;
	// Filesize of the uncompressed data
	int dataSize = readInt(lz);
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
			block = block >> 1;
		}


	}

	// Open the output file and copy the data into it
	FILE* outfile = fopen(outfileName, "wb");
	fwrite(memBlock, sizeof(char), dataSize, outfile);

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
	int filesize = ftell(rawFile);
	fseek(rawFile, 0, SEEK_SET);
	int lastPercentDone = -1;

	uint8_t * raw = (uint8_t*)malloc(sizeof(uint8_t) * (filesize + 1));
	fread(raw, sizeof(uint8_t), filesize, rawFile);
	int rawPosition = 0;

	uint8_t* comp = (uint8_t*)malloc(sizeof(uint8_t) * (int)(1.25f * filesize));
	
	fclose(rawFile);

	writeLittleIntData(comp, 4, (uint32_t)filesize);

	// 8 bytes for the header and 1 byte for the first reference block
	int compPosition = 9;
	int posInBlock = 0;
	uint8_t curBlock = 0;
	int blockBackset = 1;

	while (rawPosition < filesize) {
		float percentDone = (100.0f * rawPosition) / filesize;
		int intPercentDone = (int)percentDone;
		if (intPercentDone % 10 == 0 && intPercentDone != lastPercentDone) {
			printf("%d%% Completed\n", intPercentDone);
			lastPercentDone = intPercentDone;
		}

		ReferenceBlock maxReference = findMaxReference(raw, filesize, rawPosition);

		if (maxReference.length >= 3) {
			uint32_t backset = rawPosition - maxReference.offset;

			int offset = (rawPosition & 0xFFF) - 18 - backset;

			uint8_t leftByte = (offset & 0xFF);
			uint8_t rightByte = (((offset >> 8) & 0xF) << 4) | ((maxReference.length - 3) & 0xF);

			comp[compPosition] = leftByte;
			comp[compPosition + 1] = rightByte;

			++compPosition;
			++compPosition;

			rawPosition += maxReference.length;
			curBlock = curBlock | (0x0 << (uint8_t)posInBlock);
			blockBackset += 2;
		}// Raw byte copy
		else {
			comp[compPosition] = raw[rawPosition];

			curBlock = curBlock | (0x1 << (uint8_t)posInBlock);
			++rawPosition;
			++blockBackset;
			++compPosition;
		}

		++posInBlock;
		if (posInBlock == 8) {
			comp[compPosition - blockBackset] = curBlock;

			++compPosition;
			posInBlock = 0;
			curBlock = 0;
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

ReferenceBlock findMaxReference(char* data, int filesize, int maxOffset) {
	ReferenceBlock maxReference;
	maxReference.length = 2;
	maxReference.offset = 0;

	int curOffset = maxOffset - 4095;

	// Duplicating loop in here so that starting at > 0 has less loop checks
	if (curOffset >= 0) {
		
		// Naive Search for now
		while (curOffset < maxOffset) {
			int curLength = 0;

			while (data[curOffset + curLength] == data[maxOffset + curLength] && maxOffset + curLength < filesize) {
				++curLength;
			}
			if (curLength > maxReference.length) {
				maxReference.length = curLength;
				maxReference.offset = curOffset;
				if (curLength >= 18) {
					maxReference.length = 18;
					return maxReference;
				}
				else if (maxOffset + curLength >= filesize) {
					return maxReference;
				}
			}

			curLength = 0;
			++curOffset;
		}
	}
	else{
		if (curOffset < -18) {
			curOffset = -18;
		}

		// Naive Search for now
		while (curOffset < maxOffset) {
			int curLength = 0;
			// If the offset is less than zero, the data at that position is onsidered zero
			while (((curOffset + curLength >= 0) ? (data[curOffset + curLength] == data[maxOffset + curLength]) : (0 == data[maxOffset + curLength])) && maxOffset + curLength < filesize) {
				++curLength;
			}
			if (curLength > maxReference.length) {
				maxReference.length = curLength;
				maxReference.offset = curOffset;
				if (curLength >= 18) {
					maxReference.length = 18;
					return maxReference;
				}
			}

			curLength = 0;
			++curOffset;
		}
	}

	return maxReference;
}
