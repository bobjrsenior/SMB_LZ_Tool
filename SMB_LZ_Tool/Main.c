#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define READINT(x) ((getc(x)) + (getc(x) << 8) + (getc(x) << 16) + (getc(x) << 24))

uint16_t readShort(FILE* file) {
	char rotStr[3];
	fscanf(file, "%c%c", (rotStr + 1), (rotStr + 0));
	return *((uint16_t*)rotStr);
}

int main(int argc, char* argv[]) {
	if (argc <= 1) {
		printf("Add lz paths as command line params");
	}

	// Go through every command line arg
	for (int i = 1; i < argc; ++i) {
		// Try to open it
		FILE* lz = fopen(argv[i], "rb");
		if (lz == NULL) {
			printf("ERROR: File not found: %s\n", argv[i]);
			continue;
		}
		printf("Decompressing %s\n", argv[i]);

		// Create a temp file for the unfixed lz (SMB lz has a slightly different header than FF7 LZS)
		FILE* normal = tmpfile();

		// Unfix the header (Turn it back into normal FF7 LZSS)
		int csize = getc(lz) + (getc(lz) << 8) + (getc(lz) << 16) + (getc(lz) << 24) - 8;
		fseek(lz, 4, SEEK_CUR);
		putc(csize & 0xFF, normal);
		putc((csize >> 8) & 0xFF, normal);
		putc((csize >> 16) & 0xFF, normal);
		putc((csize >> 24) & 0xFF, normal);
		int lastPos = ftell(normal);
		for (int i = 0; i<(csize); i++) {
			char c = getc(lz);
			lastPos = ftell(lz);
			int pos = ftell(lz);
			putc(c, normal);
		}
		fclose(lz);
		fflush(normal);
		fseek(normal, 0, SEEK_SET);

		// Make the output file name
		char outfileName[512];
		sscanf(argv[i], "%507s", outfileName);
		int length = strlen(outfileName);
		outfileName[length++] = '.';
		outfileName[length++] = 'r';
		outfileName[length++] = 'a';
		outfileName[length++] = 'w';
		outfileName[length++] = '\0';

		// Open the output file for both reading and writing separately
		// This is because you read bytes from the output buffer to write to the end and prevents constant fseeking
		FILE* outfile = fopen(outfileName, "wb");
		FILE* outfileR = fopen(outfileName, "rb");


		// The size the the lzss data + 4 bytes for the header
		uint32_t filesize = READINT(normal) + 4;

		int lastPercentDone = -1;

		// Loop until we reach the end of the data or end of the file
		while ((unsigned) ftell(normal) < filesize && !feof(normal)) {

			float percentDone = (100.0f * ftell(normal)) / filesize;
			int intPercentDone = (int)percentDone;
			if (intPercentDone % 10 == 0 && intPercentDone != lastPercentDone) {
				printf("%d%% Completed\n", intPercentDone);
				lastPercentDone = intPercentDone;
			}

			// Read the first control block
			// Read right to left, each bit specifies how the the next 8 spots of data will be
			// 0 means write the byte directly to the output
			// 1 represents there will be reference (2 byte)
			uint8_t block = getc(normal);

			// Go through every bit in the control block
			for (int j = 0; j < 8 && (unsigned)ftell(normal) < filesize && !feof(normal); ++j) {
				// Literal
				if (block & 0x01) {
					putc(getc(normal), outfile);
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
					int backSet = ((ftell(outfile) - 18 - offset) & 0xFFF);

					// Calculate the actual location in the file
					int readLocation = ftell(outfile) - backSet;
					
					// Handle case where the offset is past the beginning of the file
					while (readLocation < 0 && length > 0) {
						putc(0, outfile);

						--length;
						++readLocation;
					}

					// Flush the file so we don't read something that hasn't been properly updated
					fflush(outfile);

					// Seek to the reference position
					fseek(outfileR, readLocation, SEEK_SET);

					// Read the reference bytes until we reach length bytes written
					while (length > 0) {

						putc(getc(outfileR), outfile);
						// Flush the file so we don't read something that hasn't been properly updated
						fflush(outfile);
						--length;
					}



				}
				// Go to the next reference bit in the block
				block = block >> 1;
			}


		}

		fclose(outfileR);
		fclose(outfile);
		fclose(normal);

		printf("Finished Decompressing %s\n", argv[i]);
	}
	return 0;
}