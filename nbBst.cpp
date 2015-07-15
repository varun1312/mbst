#include <iostream>
#include <climits>
#include <vector>
#include <thread>
#include "declerations.h"

#define GETADDR(node) ((Node *)((uintptr_t)node & ~0x03))
#define STATUS(node) ((uintptr_t)node & 0x03)
#define ISNULL(node) ((GETADDR(node) == NULL) || (STATUS(node) == UNQNULL))
#define ISUNQNULL(node) (STATUS(node) == UNQNULL)
#define GETDATA(node) *(int *)((uintptr_t)(GETADDR(node)->dataPtr) & ~0x03)
#define MARKNODE(node, status) (Node *)(((uintptr_t)node & ~0x03) | status)
#define CAS(ptr, source, sourceState, target, targetState) \
		__sync_bool_compare_and_swap(\
		ptr, \
		MARKNODE(source, sourceState), \
		MARKNODE(target, targetState))
#define ISMARKED(node) ((STATUS(GETADDR(node)->child[RIGHT]) == MARKED) || (STATUS(GETADDR(node)->child[LEFT]) == MARKED) || (STATUS(GETADDR(node)->dataPtr) == MARKED))

const int LEFT = 0, RIGHT = 1;
const int NORMAL = 0, MARKED = 1, PROMOTE = 2, UNQNULL = 3;
enum markStatus_t {
	REMOVE_0C,
	REMOVE_1C,
	REMOVE_2C,
	ABORT_REMOVE,
	REMOVE_ANCNODE,
	RIGHT_MARKED
};


struct Node {
	int *dataPtr;
	Node *child[2];
	Node *bl;

	Node(int data) {
		dataPtr = new int(data);
		child[LEFT] = child[RIGHT] = (Node *)UNQNULL;
	}

	Node(int data, Node *bl) {
		dataPtr = new int(data);
		child[LEFT] = child[RIGHT] = (Node *)UNQNULL;
		this->bl = bl;
	}
};
Node *root = new Node(INT_MAX);
struct seekNode {
	Node *ancNode;
	int ancNodeData;
	Node *pred;
	Node *curr;

	seekNode(Node *ancNode, int ancNodeData, Node *pred, Node *curr) {
		this->ancNode = ancNode;
		this->ancNodeData = ancNodeData;
		this->pred = pred;
		this->curr = curr;		
	}
};

seekNode* seekTree(Node *startNode, int data) {
	Node *ancNode = startNode;
	Node *pred = startNode;
	Node *curr = startNode;
	int ancNodeData, currData;
	while(true) {
		if (ISNULL(curr)) {
			seekNode *mySeek = new seekNode(ancNode, ancNodeData, pred, curr);
			return mySeek;
		}
		currData = GETDATA(curr);
		if (data > currData) {
			pred = GETADDR(curr);
			curr = GETADDR(curr)->child[RIGHT];
		}
		else if (data < currData) {
			ancNode = pred;
			ancNodeData = GETDATA(ancNode);
			pred = GETADDR(curr);
			curr = GETADDR(curr)->child[LEFT];
		}
		else if (data == currData) {
			seekNode *mySeek = new seekNode(ancNode, ancNodeData, pred, curr);
			return mySeek;
		}
	}
} 

void helpSwapData(Node *curr, Node *ancNode, int *dataPtr) {
	// Here if else condition can be put but it is redundent.
	// CAS failed means dataPtr is swapped and therefore no
	// help is required.
	CAS(&ancNode->dataPtr, dataPtr, MARKED, dataPtr, NORMAL);
	return;
}

bool insertTreeNode(Node *pred, Node *curr, int status, int data) {
	int predData = GETDATA(pred);
	Node *myNode = new Node(data, pred);
	if (data > predData) {
		if (CAS(&pred->child[RIGHT], curr, UNQNULL, myNode, NORMAL))
			return true;
		else {
			// CAS failed means pred child changed or pred 	
			// is marked. Both the scenarions are handled by
			// insert method. We need to restart the insert
			// from pred
			return insertTree(pred, data);
		}
	}
	else {
		if (CAS(&pred->child[LEFT], curr, UNQNULL, myNode, NORMAL))
			return true;
		else {
			// CAS failed means pred child changed or pred 	
			// is marked. Both the scenarions are handled by
			// insert method. We need to restart the insert
			// from pred
			return insertTree(pred, data);
		}
	}
}

bool insertTree(Node *startNode, int data) {
	seekNode *insSeek = seekTree(startNode, data);
	Node *ancNode = insSeek->ancNode;
	int ancNodeDataPrev = insSeek->ancNodeData;
	Node *pred = insSeek->pred;
	Node *curr= insSeek->curr;
	int *ancNodeDataPtr = ancNode->dataPtr;
	int ancNodeDataCurr = GETDATA(ancNode);
	if (ancNodeDataCurr != ancNodeDataPrev)
		return insertTree(ancNode, data);
	if (ISNULL(curr)) {
		if (STATUS(curr) == UNQNULL) {
			return insertTreeNode(pred, GETADDR(curr), UNQNULL, data);
		}
		else if (STATUS(curr) == PROMOTE) {
			helpSwapData(pred, GETADDR(curr), ancNodeDataPtr);
			return insertTree(ancNode, data);
		}
		else if (STATUS(curr) == MARKED) {
			// Help Mark the node and then
			// remove it in insert style.
			// returning false for now.
			return false;
		}
	}
	else if (data == GETDATA(curr)) {
		if (ISMARKED(curr)) {
			/* Help the removal of node and retry insert */
			return false;
		}
		return false;			
	}
} 

void printTree(Node *node) {
	if (ISNULL(node)) 
		return;
	printTree(GETADDR(node)->child[LEFT]);
	std::cout<<GETDATA(node)<<std::endl;
	printTree(GETADDR(node)->child[RIGHT]);
}

void testbenchSequential() {
	srand(time(NULL));
	for (int i = 0; i < 10; i++)
		insertTree(root, rand());
	printTree(root->child[LEFT]);
}


int main(void) {
	//testbenchParallel();
	testbenchSequential();
	return 0;
}
