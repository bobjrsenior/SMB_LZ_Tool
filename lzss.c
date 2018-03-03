#include "lzss.h"

#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "FunctionsAndDefines.h"

// Used for convienence in case a
// different type is faster (ie uint16_t vs uint32_t)
#define TREETYPE uint16_t

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
	for (TREETYPE i = 0; i < 4096; i++) {
		binaryTree[i].parent = nullConstant;
		binaryTree[i].leftChild = nullConstant;
		binaryTree[i].rightChild = nullConstant;
	}

	// All values are initially negative
	// The longest length is -18, so make
	// the 18th from the end the initial root
	binaryTree[4096 - 18].parent = rootConstant;
	rootIndex = 4096 - 18;
}

/*
* Converts a tree index into a file index
*/
static uint32_t convertToOffset(TREETYPE treePointer) {
	if (treePointer == binaryTreeIndex) {
		return inputIndex + binaryTreeIndex - 4096;
		//return (inputIndex + binaryTreeIndex) - 4096;
	}
	if (treePointer > binaryTreeIndex) {
		return inputIndex + treePointer - binaryTreeIndex - 4096;
		//return (inputIndex + (treePointer - binaryTreeIndex)) - 4096;
	}
	else {
		return (inputIndex - binaryTreeIndex) + treePointer;
		//return (inputIndex + (4096 - binaryTreeIndex) + treePointer) - 4096;
	}
}

/*
* Compares two positions in the inputData for equality
* If all 18 bytes are equal, then 0 is returned
* Otherwise the difference between the first byte that's different is returned
*/
static CompareResult compare(TREETYPE index1, TREETYPE index2) {
	for (uint32_t i = 0; i < 18; i++) {
		int result = inputData[index1 + i] - inputData[index2 + i];
		if (result != 0) {
			return { i, result };
		}
	}
	return { 18, 0 };
}

static void printTreeRecurse(int curDepth, int depth, TREETYPE curIndex) {
	if (curDepth == depth) {
		if (binaryTree[curIndex].leftChild != nullConstant) {
			printf("%d\t", convertToOffset(binaryTree[curIndex].leftChild) - 4096);
		}
		else {
			putchar(' ');
		}

		if (binaryTree[curIndex].rightChild != nullConstant) {
			printf("%d\t", convertToOffset(binaryTree[curIndex].rightChild) - 4096);
		}
		else {
			putchar(' ');
		}
	}
	else {
		curDepth++;
		if (binaryTree[curIndex].leftChild != nullConstant) {
			printTreeRecurse(curDepth, depth, binaryTree[curIndex].leftChild);
		}
		if (binaryTree[curIndex].rightChild != nullConstant) {
			printTreeRecurse(curDepth, depth, binaryTree[curIndex].rightChild);
		}
	}
}

static void printTree(int maxDepth, TREETYPE startIndex) {
	int numTabs = (maxDepth * 4) >> 1;

	printf("TREE:\n%d\n", convertToOffset(startIndex) - 4096);

	for (int depth = 0; depth < maxDepth; depth++) {
		printTreeRecurse(0, depth, startIndex);
		putchar('\n');
	}
}

/*
* Takes a node index and inserts it into the binary search tree
*/
static void calculateNode(TREETYPE index) {
	TREETYPE curNodeIndex = rootIndex;

	// Traverse the tree til we find the new nodes perfect match...
	while (1) {
		CompareResult result = compare(convertToOffset(index), convertToOffset(curNodeIndex));
		if (result.value == 0) {
			// Set the new node's parent/children to the stale version's parent/children
			binaryTree[index].parent = binaryTree[curNodeIndex].parent;
			binaryTree[index].leftChild = binaryTree[curNodeIndex].leftChild;
			binaryTree[index].rightChild = binaryTree[curNodeIndex].rightChild;

			// Set the node's parent to point to the new node
			if (binaryTree[index].parent != rootConstant) {
				if (binaryTree[binaryTree[index].parent].leftChild == curNodeIndex) {
					binaryTree[binaryTree[index].parent].leftChild = index;
				}
				else {
					binaryTree[binaryTree[index].parent].rightChild = index;
				}
			}

			// Set the parents of the stale version's children to the new node
			if (binaryTree[curNodeIndex].leftChild != nullConstant) {
				binaryTree[binaryTree[curNodeIndex].leftChild].parent = index;
			}
			if (binaryTree[curNodeIndex].rightChild != nullConstant) {
				binaryTree[binaryTree[curNodeIndex].rightChild].parent = index;
			}

			binaryTree[curNodeIndex].parent = nullConstant;
			binaryTree[curNodeIndex].leftChild = nullConstant;
			binaryTree[curNodeIndex].rightChild = nullConstant;

			break;
		}
		else if (result.value > 0) {
			// If we reached a leaf. Insert the node here
			if (binaryTree[curNodeIndex].rightChild == nullConstant) {
				binaryTree[curNodeIndex].rightChild = index;
				binaryTree[index].parent = curNodeIndex;

				break;
			}


			// Continue searching down the right subtree
			curNodeIndex = binaryTree[curNodeIndex].rightChild;
		}
		else {
			// If we reached a leaf. Insert the node here
			if (binaryTree[curNodeIndex].leftChild == nullConstant) {
				binaryTree[curNodeIndex].leftChild = index;
				binaryTree[index].parent = curNodeIndex;

				break;
			}

			// Continue searching down the left subtree
			curNodeIndex = binaryTree[curNodeIndex].leftChild;
		}
	}

	// Change the root index if needed
	if (binaryTree[index].parent == rootConstant) {
		rootIndex = index;
	}
}

/*
* Takes a node index and removed it from the binary search tree
* Its parent/children will become the nullConstant
*/
static void removeNode(TREETYPE index) {
	TREETYPE parentIndex = binaryTree[index].parent;
	// No children
	if (binaryTree[index].leftChild == nullConstant && binaryTree[index].rightChild == nullConstant) {
		// Make sure the node's parent doesn't point here anymore
		TREETYPE parentIndex = binaryTree[index].parent;
		if (parentIndex != rootConstant) {
			if (binaryTree[parentIndex].rightChild == index) {
				binaryTree[parentIndex].rightChild = nullConstant;
			}
			else {
				binaryTree[parentIndex].leftChild = nullConstant;
			}
		}
		else {
			//puts("There");
		}

		// Set the node's parent to the nullConstant
		binaryTree[index].parent = nullConstant;
	}// Only right child
	else if (binaryTree[index].leftChild == nullConstant) {
		// Make the node's parent point to this node's right child
		TREETYPE parentIndex = binaryTree[index].parent;
		if (parentIndex != rootConstant) {
			if (binaryTree[parentIndex].rightChild == index) {
				binaryTree[parentIndex].rightChild = binaryTree[index].rightChild;
			}
			else {
				binaryTree[parentIndex].leftChild = binaryTree[index].rightChild;
			}
		}
		else {
			rootIndex = binaryTree[index].rightChild;
		}

		binaryTree[binaryTree[index].rightChild].parent = binaryTree[index].parent;

		// Set the node's parent and right child to the nullConstant
		binaryTree[index].parent = nullConstant;
		binaryTree[index].rightChild = nullConstant;

	} // Only left child
	else if (binaryTree[index].rightChild == nullConstant) {
		// Make the node's parent point to this node's left child
		TREETYPE parentIndex = binaryTree[index].parent;
		if (parentIndex != rootConstant) {
			if (binaryTree[parentIndex].rightChild == index) {
				binaryTree[parentIndex].rightChild = binaryTree[index].leftChild;
			}
			else {
				binaryTree[parentIndex].leftChild = binaryTree[index].leftChild;
			}
		}
		else {
			rootIndex = binaryTree[index].leftChild;
		}

		binaryTree[binaryTree[index].leftChild].parent = binaryTree[index].parent;

		// Set the node's parent and right child to the nullConstant
		binaryTree[index].parent = nullConstant;
		binaryTree[index].leftChild = nullConstant;
	} // Both children
	else {
		// Grab the furthest left right child

		TREETYPE childIndex = binaryTree[index].rightChild;

		while (binaryTree[childIndex].leftChild != nullConstant) {
			childIndex = binaryTree[childIndex].leftChild;
		}

		// Detach the child from the bottom
		if (binaryTree[childIndex].rightChild != nullConstant) {
			if (binaryTree[childIndex].parent != index) {
				binaryTree[binaryTree[childIndex].parent].leftChild = binaryTree[childIndex].rightChild;
				binaryTree[binaryTree[childIndex].rightChild].parent = binaryTree[childIndex].parent;
			}
			else {
				binaryTree[index].rightChild = binaryTree[childIndex].rightChild;
			}
		}
		else {
			if (binaryTree[childIndex].parent != index) {
				binaryTree[binaryTree[childIndex].parent].leftChild = nullConstant;
			}
			else {
				binaryTree[index].rightChild = nullConstant;
			}
		}

		// Replace this node with the child

		// Make the node's parent point to the new child
		TREETYPE parentIndex = binaryTree[index].parent;
		if (parentIndex != rootConstant) {
			if (binaryTree[parentIndex].rightChild == index) {
				binaryTree[parentIndex].rightChild = childIndex;
			}
			else {
				binaryTree[parentIndex].leftChild = childIndex;
			}
		}
		else {
			rootIndex = childIndex;
		}

		// Copy all of this node's attributes to the new child
		binaryTree[childIndex].parent = binaryTree[index].parent;
		binaryTree[childIndex].leftChild = binaryTree[index].leftChild;
		binaryTree[childIndex].rightChild = binaryTree[index].rightChild;

		binaryTree[binaryTree[childIndex].leftChild].parent = childIndex;
		if (binaryTree[childIndex].rightChild != nullConstant) {
			binaryTree[binaryTree[childIndex].rightChild].parent = childIndex;
		}

		binaryTree[index].parent = nullConstant;
		binaryTree[index].leftChild = nullConstant;
		binaryTree[index].rightChild = nullConstant;
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

		uint32_t curIndex = binaryTreeIndex;

		++inputIndex;
		binaryTreeIndex++;
		if (binaryTreeIndex == 4096) {
			binaryTreeIndex = 0;
		}

		calculateNode(curIndex);
	}

	inputIndex -= length;
}

/*
* Finds the longest reference in the Binary Tree available
* The tree will be fixed afterwards using the fixTree(uint32_t) method
*/
static ReferenceBlock findMaxReference() {
	ReferenceBlock maxReference = { 2, 0 };
	TREETYPE treePointer = rootIndex;

	//printTree(10, rootIndex);

	while (treePointer != nullConstant) {
		uint32_t fileOffset = convertToOffset(treePointer);
		CompareResult result = compare(inputIndex, fileOffset);
		if (result.length > maxReference.length) {
			maxReference.length = result.length;
			maxReference.offset = fileOffset;
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

	// Add 4096 for "negative" values
	uint32_t paddedFilesize = filesize + 4096;
	inputData = (uint8_t *)malloc(paddedFilesize); 
	if (inputData == NULL) {
		puts("Unable to allocate memory");
		return -1;
	}
	memset(inputData, 0, sizeof(uint8_t) * 4096);

	// Worst case scenario is 1/8 bigger thn inputData
	// Make is 1/4 bigger anyways to be safe
	outputData = (uint8_t *)malloc((sizeof(uint8_t) * (filesize + (filesize >> 2))));
	if (outputData == NULL) {
		puts("Unable to allocate memory");
		return -1;
	}

	fread(&inputData[4096], sizeof(uint8_t), filesize, input);

	outputIndex += 8;

	initializeBinaryTree();

	uint32_t posInBlock = 0;
	uint8_t curBlock = 0;
	uint32_t blockBackset = 1;
	// Make room for initial control block
	outputIndex++;

	while (inputIndex < paddedFilesize) {
		ReferenceBlock maxReference = findMaxReference();

		// If the reference is long enough to use
		if (maxReference.length >= 3) {
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

		++posInBlock;
		if (posInBlock == 8) {
			outputData[outputIndex - blockBackset] = curBlock;
			posInBlock = 0;
			curBlock = 0;
			// Make room for the next control block
			++outputIndex;
			blockBackset = 1;
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