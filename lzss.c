#include "lzss.h"

#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "FunctionsAndDefines.h"

#ifdef DEBUG
#define VALIDATE_TREE checkTreeValidity()
#elif _DEBUG
#define VALIDATE_TREE checkTreeValidity()
#else
#define VALIDATE_TREE 
#endif

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
static TREETYPE binaryTreeIndex = 4095;
static TreeNode binaryTree[4096];
static uint8_t *inputData;
static uint8_t *outputData;
static int maxDepth = 0;

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
		//             Base
		return inputIndex - 1;
	}
	if (treePointer > binaryTreeIndex) {
		//         Base                Amount to start       Amount to wrap
		return (inputIndex - 1) - (binaryTreeIndex) - (4096 - treePointer);
	}
	else {
		//           Base                          Amount behing
		return (inputIndex - 1) - (binaryTreeIndex - treePointer);
	}
}

/*
* Compares two positions in the inputData for equality
* If all 18 bytes are equal, then 0 is returned
* Otherwise the difference between the first byte that's different is returned
*/
static CompareResult compare(uint32_t index1, uint32_t index2) {
	for (uint32_t i = 0; i < 18; i++) {
		int result = inputData[index1 + i] - inputData[index2 + i];
		if (result != 0) {
			return { i, result };
		}
	}
	return { 18, 0 };
}

/*
* Compares two positions in the inputData for equality
* This version does not give the number of similar bytes
*/
static int compareFast(uint32_t index1, uint32_t index2) {
	for (uint32_t i = 0; i < 18; i += 2) {
		uint16_t val1 = (inputData[index1 + i] << 8) | (inputData[index1 + i + 1]);
		uint16_t val2 = (inputData[index2 + i] << 8) | (inputData[index2 + i + 1]);
		int result = val1 - val2;
		if (result != 0) {
			return result;
		}
	}
	return 0;
}

static void checkTreeValidity2(TREETYPE root, int depth) {
	TREETYPE leftChild = binaryTree[root].leftChild;
	TREETYPE rightChild = binaryTree[root].rightChild;
	int result;
	if (leftChild != nullConstant) {
		result = compareFast(convertToOffset(root), convertToOffset(leftChild));
		if (result < 0) {
			puts("Bad Tree");
			result = compareFast(convertToOffset(root), convertToOffset(rightChild));
		}
		else {
			checkTreeValidity2(leftChild, depth + 1);
		}
	}

	if (rightChild != nullConstant) {
		result = compareFast(convertToOffset(root), convertToOffset(rightChild));
		if (result >= 0) {
			puts("Bad Tree");
			result = compareFast(convertToOffset(root), convertToOffset(rightChild));
		}
		else {
			checkTreeValidity2(rightChild, depth + 1);
		}
	}

	if (rightChild == nullConstant && leftChild != nullConstant && depth > maxDepth && inputIndex > 15000) {
		maxDepth = depth;
	}


}

static void checkTreeValidity() {
	TREETYPE root = rootIndex;
	checkTreeValidity2(root, 1);
}

/*
* Takes a node index and inserts it into the binary search tree
*/
static void calculateNode(TREETYPE index) {
	TREETYPE curNodeIndex = rootIndex;

	if (curNodeIndex == nullConstant) {
		rootIndex = index;
		binaryTree[index].parent = rootConstant;
	}

	// Traverse the tree til we find the new nodes perfect match...
	while (1) {
		int result = compareFast(convertToOffset(index), convertToOffset(curNodeIndex));
		if (result == 0) {
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
		else if (result > 0) {
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
			rootIndex = nullConstant;
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
				binaryTree[parentIndex].rightChild = binaryTree[index].leftChild;//
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
				binaryTree[binaryTree[childIndex].parent].leftChild = binaryTree[childIndex].rightChild;//
				binaryTree[binaryTree[childIndex].rightChild].parent = binaryTree[childIndex].parent;
			}
			else {
				binaryTree[index].rightChild = binaryTree[childIndex].rightChild;//
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
				binaryTree[parentIndex].rightChild = childIndex;//
			}
			else {
				binaryTree[parentIndex].leftChild = childIndex;//
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
			binaryTree[binaryTree[childIndex].rightChild].parent = childIndex;//
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
		TREETYPE toRemove = binaryTreeIndex + 1u;
		if (toRemove == 4096) {
			toRemove = 0;
		}

		if (binaryTree[toRemove].parent != nullConstant) {
			removeNode(toRemove);
		}
		VALIDATE_TREE;

		++inputIndex;
		binaryTreeIndex = toRemove;

		VALIDATE_TREE;
		calculateNode(binaryTreeIndex);
		VALIDATE_TREE;
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

	VALIDATE_TREE;
	while (treePointer != nullConstant) {
		uint32_t fileOffset = convertToOffset(treePointer);
		CompareResult result = compare(inputIndex, fileOffset);
		if (result.length > maxReference.length && inputIndex - fileOffset != 4096) {
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
	inputData = (uint8_t *)malloc(sizeof(uint8_t) * paddedFilesize); 
	if (inputData == NULL) {
		puts("Unable to allocate memory");
		return -1;
	}
	memset(inputData, 0, sizeof(uint8_t) * 4096);

	// Worst case scenario is 1/8 bigger thn inputData
	// Make is 1/4 bigger anyways to be safe
	outputData = (uint8_t *)malloc((sizeof(uint8_t) * (filesize + (filesize)))); // TODO Change back to filesize >> 2
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
	int lastPercentDone = -1;

	while (inputIndex < paddedFilesize) {
		float percentDone = (100.0f * (inputIndex - 4096)) / filesize;
		int intPercentDone = (int)percentDone;
		if (intPercentDone % 10 == 0 && intPercentDone != lastPercentDone) {
			printf("%d%% Completed\n", intPercentDone);
			lastPercentDone = intPercentDone;
		}

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