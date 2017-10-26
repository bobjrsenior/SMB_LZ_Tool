#include "lzss.h"

#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "FunctionsAndDefines.h"

// Used for convienence in case a
// different type is faster (ie uint16_t)
#define TREETYPE uint32_t

typedef struct {
	TREETYPE parent;
	TREETYPE leftChild;
	TREETYPE rightChild;
}TreeNode;

typedef struct {
	uint32_t length;
	uint32_t offset;
}ReferenceBlock;

typedef struct {
	uint32_t length;
	int value;
}CompareResult;

static const TREETYPE rootConstant = 0xFFFF;
static const TREETYPE nullConstant = 0xFFFD;
static uint32_t filesize;
static uint32_t compressedsize;
static uint32_t inputIndex = 4096; // Offset for the 4096 "negative" values
static uint32_t outputIndex = 0;
static TREETYPE rootIndex;
static TREETYPE binaryTreeIndex = 0;
static TreeNode binaryTree[4096];
static uint8_t *inputData;
static uint8_t *outputData;

/*
* Initializes the Binary Search Tree to its initial state
*/
static void initializeBinaryTree() {
	// Initialize the tree to all null values
	memset(binaryTree, nullConstant, sizeof(TreeNode) * 4096);

	// All values are initially negative
	// The longest length is -18, so make
	// the 18th from the end the initial root
	binaryTree[0].parent = rootConstant;
	rootIndex = 0;
}

static uint32_t convertToOffset(TREETYPE treePointer) {
	if (treePointer > binaryTreeIndex) {
		return (inputIndex - 4096 + (binaryTreeIndex - treePointer));
	}
	else {
		return (inputIndex + (4096 - binaryTreeIndex) + treePointer);
	}
}

/*
* Compares two positions in the inputData for equality
* If all 18 bytes are equal, then 0 is returned
* Otherwise the difference between the first byte that's different is returned
*/
static CompareResult compare(TREETYPE index1, TREETYPE index2) {
	for (uint32_t i = 0; i < 18; i++) {
		int result = inputData[index1] = inputData[index2];
		if (result != 0) {
			return { i, result };
		}
	}
	return { 18, 0 };
}

/*
* Takes a node index and inserts it into the binary search tree
*/
static void calculateNode(TREETYPE index) {
	TREETYPE curNodeIndex = rootIndex;

	// Traverse the tree til we find the new nodes perfect match...
	while (binaryTree[curNodeIndex].parent != nullConstant) {
		CompareResult result = compare(index, curNodeIndex);
		if (result.value == 0) {
			// Set the new node's parent/children to the stale version's parent/children
			binaryTree[index].parent = binaryTree[curNodeIndex].parent;
			binaryTree[index].leftChild = binaryTree[curNodeIndex].leftChild;
			binaryTree[index].rightChild = binaryTree[curNodeIndex].rightChild;

			// Set the parents of the stale version's children to the new node
			binaryTree[binaryTree[curNodeIndex].leftChild].parent = index;
			binaryTree[binaryTree[curNodeIndex].rightChild].parent = index;

			return;
		}
		else if (result.value > 0) {
			// If we reached a leaf. Insert the node here
			if (binaryTree[curNodeIndex].rightChild == nullConstant) {
				binaryTree[curNodeIndex].rightChild = index;
				binaryTree[index].parent = curNodeIndex;

				return;
			}

			// Continue searching down the right subtree
			curNodeIndex = binaryTree[curNodeIndex].rightChild;
		}
		else {
			// If we reached a leaf. Insert the node here
			if (binaryTree[curNodeIndex].leftChild == nullConstant) {
				binaryTree[curNodeIndex].leftChild = index;
				binaryTree[index].parent = curNodeIndex;

				return;
			}

			// Continue searching down the left subtree
			curNodeIndex = binaryTree[curNodeIndex].rightChild;
		}
	}
}

/*
* Takes a node index and removed it from the binary search tree
* Its parent/children will become the nullConstant
*/
static void removeNode(TREETYPE index) {
	// No children
	if (binaryTree[index].leftChild == nullConstant && binaryTree[index].rightChild == nullConstant) {
		// Make sure the node's parent doesn't point here anymore
		TREETYPE parentIndex = binaryTree[index].parent;
		if (binaryTree[parentIndex].rightChild == index) {
			binaryTree[parentIndex].rightChild = nullConstant;
		}
		else {
			binaryTree[parentIndex].leftChild = nullConstant;
		}

		// Set the node's parent to the nullConstant
		binaryTree[index].parent = nullConstant;
	}// Only right child
	else if (binaryTree[index].leftChild == nullConstant) {
		// Make the node's parent point to this node's right child
		TREETYPE parentIndex = binaryTree[index].parent;
		if (binaryTree[parentIndex].rightChild == index) {
			binaryTree[parentIndex].rightChild = binaryTree[index].rightChild;
		}
		else {
			binaryTree[parentIndex].leftChild = binaryTree[index].rightChild;
		}

		// Set the node's parent and right child to the nullConstant
		binaryTree[index].parent = nullConstant;
		binaryTree[index].rightChild = nullConstant;

	} // Onle left child
	else if (binaryTree[index].rightChild == nullConstant) {
		// Make the node's parent point to this node's left child
		TREETYPE parentIndex = binaryTree[index].parent;
		if (binaryTree[parentIndex].rightChild == index) {
			binaryTree[parentIndex].rightChild = binaryTree[index].leftChild;
		}
		else {
			binaryTree[parentIndex].leftChild = binaryTree[index].leftChild;
		}

		// Set the node's parent and right child to the nullConstant
		binaryTree[index].parent = nullConstant;
		binaryTree[index].rightChild = nullConstant;
	} // Both children
	else {
		// Grab the furthest left right child

		TREETYPE childIndex = binaryTree[index].rightChild;

		while (binaryTree[childIndex].leftChild != nullConstant) {
			childIndex = binaryTree[childIndex].leftChild;
		}

		// Detach the child from the bottom
		if (binaryTree[childIndex].rightChild != nullConstant) {
			binaryTree[binaryTree[childIndex].parent].leftChild = binaryTree[childIndex].rightChild;
		}
		else {
			binaryTree[binaryTree[childIndex].parent].leftChild = nullConstant;
		}

		// Replace this node with the child

		// Make the node's parent point to the new child
		TREETYPE parentIndex = binaryTree[index].parent;
		if (binaryTree[parentIndex].rightChild == index) {
			binaryTree[parentIndex].rightChild = childIndex;
		}
		else {
			binaryTree[parentIndex].leftChild = childIndex;
		}

		// Copy all of this node's attributes to the new child
		binaryTree[childIndex].parent = binaryTree[index].parent;
		binaryTree[childIndex].leftChild = binaryTree[index].leftChild;
		binaryTree[childIndex].rightChild = binaryTree[index].rightChild;
	}
}

/*
* Fixes a tree after the sliding door is removed
* This is done to remove references older than 4k back
*/
static void fixTree(uint32_t length) {

	for (uint32_t i = 0; i < length; i++) {
		if (binaryTree[binaryTreeIndex].parent != nullConstant) {
			removeNode(binaryTreeIndex);
		}
		calculateNode(binaryTreeIndex);

		binaryTreeIndex++;
		if (binaryTreeIndex == 4096) {
			binaryTreeIndex = 0;
		}
	}
}

/*
* Finds the longest reference in the Binary Tree available
* The tree will be fixed afterwards using the fixTree(uint32_t) method
*/
static ReferenceBlock findMaxReference() {
	ReferenceBlock maxReference = { 2, 0 };
	TREETYPE convert = (TREETYPE) (inputIndex - 4096 + binaryTreeIndex);
	TREETYPE treePointer = rootIndex;

	while (treePointer != nullConstant) {
		CompareResult result = compare(inputIndex, treePointer + convert);
		if (result.length > maxReference.length) {
			maxReference.length = result.length;
			maxReference.offset = (treePointer + convert);
		}

		if (result.value == 0) {
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

	inputData = (uint8_t *)malloc(sizeof(uint8_t) * filesize + 4096); // Add 4096 for "negative" values
	if (inputData == NULL) {
		puts("Unable to allocate memory");
		return -1;
	}
	outputData = (uint8_t *)malloc((sizeof(uint8_t) * (filesize + (filesize << 2)))); // Worst case scenario is 1/8 bigger thn inputData
	if (outputData == NULL) {
		puts("Unable to allocate memory");
		return -1;
	}

	outputData += 8;

	initializeBinaryTree();

	uint32_t posInBlock = 0;
	uint8_t curBlock = 0;
	uint32_t blockBackset = 1;

	while (inputIndex < filesize) {
		ReferenceBlock maxReference = findMaxReference();

		// If the reference is long enough to use
		if (maxReference.length > 3) {
			// Calculate the reference
			uint32_t backset = inputIndex - maxReference.offset;

			uint32_t offset = (inputIndex & 0xFFF) - 18 - backset;
			uint8_t leftByte = (offset & 0xFF);
			uint8_t rightByte = (((offset >> 8) & 0xF) << 4) | ((maxReference.length - 3) & 0xF);

			// Write it out
			outputData[outputIndex] = leftByte;
			outputData[outputIndex + 1] = rightByte;
			outputIndex += 2;

			// Set control values and update data positions
			inputIndex += maxReference.length;
			curBlock = (uint8_t)(curBlock | (0x0 << (uint8_t)posInBlock));
			blockBackset += 2;

		}// The reference is too short (write raw value)
		else {
			outputData[outputIndex] = inputData[inputIndex];

			curBlock = (uint8_t)(curBlock | (0x1 << (uint8_t)posInBlock));
			++inputIndex;
			++blockBackset;
			++outputIndex;
		}
	}

	// Make sure you don't have any data bytes without a reference block
	if (posInBlock != 0) {
		outputData[outputIndex - blockBackset] = curBlock;
	}

	// Write compressed filesize
	writeLittleIntData(outputData, 0, outputIndex);

	// Write uncompressed filezise
	writeLittleIntData(outputData, 4, filesize);

	// Write actual data
	fwrite(outputData, sizeof(uint8_t), outputIndex, output);

	// Cleanup
	free(inputData);
	free(outputData);
	return 0;
}