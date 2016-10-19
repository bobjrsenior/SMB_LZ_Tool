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
	uint8_t test = 0xF;

	if (argc <= 1) {
		printf("Add lz paths as command line params");
	}
	uint16_t reference = 0x5312;

	// Length is the last four bits + 3
	// Any less than a lengh of 3 i pointess since a reference takes up 3 bytes
	int length = (reference & 0x000F) + 3;

	for (int i = 1; i < argc; ++i) {
		FILE* lz = fopen(argv[i], "rb");
		if (lz == NULL) {
			printf("ERROR: File not found: %s\n", argv[i]);
			continue;
		}
		FILE* normal = fopen("test.lzs", "wb");

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
		fclose(normal);
		normal = fopen("test.lzs", "rb");

		char outfileName[512];
		sscanf(argv[i], "%507s", outfileName);
		int length = strlen(outfileName);
		outfileName[length++] = '.';
		outfileName[length++] = 'r';
		outfileName[length++] = 'a';
		outfileName[length++] = 'w';
		outfileName[length++] = '\0';

		FILE* outfile = fopen(outfileName, "wb");
		FILE* outfileR = fopen(outfileName, "rb");

		int curPosition = 0;

		uint32_t filesize = READINT(normal) + 4;



		while ((unsigned) ftell(normal) < filesize && !feof(normal)) {
			uint8_t block = getc(normal);

			for (int j = 0; j < 8 && (unsigned)ftell(normal) < filesize && !feof(normal); ++j) {
				// Literal
				if (block & 0x01) {
					putc(getc(normal), outfile);
				}// Reference
				else {
					uint16_t reference = readShort(normal);

					// Length is the last four bits + 3
					// Any less than a lengh of 3 i pointess since a reference takes up 3 bytes
					int length = (reference & 0x000F) + 3;

					int offset = ((reference & 0xFF00) >> 8) | ((reference & 0x00F0) << 4);

					int backSet = ((ftell(outfile) - 18 - offset) & 0xFFF);

					int readLocation = ftell(outfile) - backSet;
					//printf("%d\n%d\n", ftell(outfile), ((ftell(outfile) - 18 - offset) % 4096));
					
					// Handle case where the offset is past the beginning of the file
					while (readLocation < 0 && length > 0) {
						putc(0, outfile);

						--length;
						++readLocation;
					}
					fflush(outfile);

					fseek(outfileR, readLocation, SEEK_SET);

					while (length > 0) {

						putc(getc(outfileR), outfile);
						fflush(outfile);
						--length;
						++readLocation;
					}



				}
				// Go to the next reference bit in the block
				block = block >> 1;
			}


		}

		fclose(outfileR);
		fclose(outfile);



		fclose(normal);
	}
}