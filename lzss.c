#include "lzss.h"

#include <string.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct {
	uint32_t parent;
	uint32_t leftChild;
	uint32_t rightChild;
}TreeNode;

typedef struct {
	uint32_t length;
	uint32_t offset;
}ReferenceBlock;

typedef struct {
	uint32_t length;
	int value;
}CompareResult;

static const uint32_t rootConstant = 0xFFFFFFFF;
static const uint32_t nullConstant = 0xFFFFFFFD;
static uint32_t filesize;
static uint32_t compressedsize;
static uint32_t inputIndex = 4096; // Offset for the 4096 "negative" values
static uint32_t outputIndex = 0;
static uint32_t rootIndex;
static uint32_t binaryTreeIndex = 0;
static TreeNode binaryTree[4096];
static char *inputData;
static char *outputData;

/*
* Initializes the Binary Search Tree to its initial state
*/
static void initializeBinaryTree() {
	// Initialize the tree to all null values
	memset(binaryTree, nullConstant, sizeof(TreeNode) * 4096);

	// All values are initially negative
	// The longest length is -18, so make
	// the 18th from the end the initial root
	binaryTree[4096 - 18].parent = rootConstant;
	rootIndex = 4096 - 18;
}

/*
* Compares two positions in the inputData for equality
* If all 18 bytes are equal, then 0 is returned
* Otherwise the difference between the first byte that's different is returned
*/
static CompareResult compare(uint32_t index1, uint32_t index2) {
	for (uint32_t i = 0; i < 18; i++) {
		int result = inputData[index1] = inputData[index2];
		if (result != 0) {
			return{ i, result };
		}
	}
	return{ 18, 0 };
}

/*
* Fixes a tree after the sliding door is removed
* This is done to remove references older than 4k back
*/
static void fixTree(uint32_t length) {


	binaryTreeIndex += length;
	if (binaryTreeIndex >= 4096) {
		binaryTreeIndex -= 4096;
	}
}

/*
* Finds the longest reference in the Binary Tree available
* The tree will be fixed afterwards using the fixTree(uint32_t) method
*/
static ReferenceBlock findMaxReference() {
	ReferenceBlock maxReference = { 2, 0 };
	uint32_t convert = inputIndex - 4096 + binaryTreeIndex;
	uint32_t treePointer = rootIndex;

	while (treePointer != nullConstant) {
		CompareResult result = compare(inputIndex, treePointer + convert);
		if (result.length > maxReference.length) {
			maxReference.length = result.length;
			maxReference.offset = (treePointer + convert);
		}

		if (result.value == 0) {
			// TODO Update tree node immediately?
			break;
		}
		else if (result.value > 0) {
			treePointer = binaryTree[treePointer].rightChild;
		}
		else {
			treePointer = binaryTree[treePointer].leftChild;
		}
	}

	if (maxReference.length > 2) {
		fixTree(maxReference.length);
	}
	else {
		fixTree(1);
	}

	return maxReference;
}

int compressFile(char *filename) {
	// Try to open it
	FILE* rawfile = fopen(filename, "rb");
	if (rawfile == NULL) {
		printf("ERROR: File not found: %s\n", filename);
		return -1;
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
	if (outfile == NULL) {
		printf("ERROR: File not found: %s\n", outfileName);
		return -1;
	}

	int result = compress(rawfile, outfile);
	fclose(rawfile);
	fclose(outfile);
	return result;
}

int compress(FILE *input, FILE *output) {

	fseek(input, 0, SEEK_END);
	filesize = (uint32_t)ftell(input);
	fseek(input, 0, SEEK_SET);

	inputData = (char *)malloc(sizeof(char) * filesize + 4096); // Add 4096 for "negative" values
	outputData = (char *)malloc((size_t) (sizeof(char) * (filesize * 1.25))); // Worst case scenario is 1/8 bigger thn inputData

	initializeBinaryTree();

	uint32_t posInBlock = 0;
	uint8_t curBlock = 0;
	uint32_t blockBackset = 1;

	while (inputIndex < filesize) {
		ReferenceBlock maxReference = findMaxReference();
	}

	free(inputData);
	free(outputData);
	return -1;
}