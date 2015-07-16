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
 
void markLeft(Node *node) {
	Node *lp = node->child[LEFT];
	while(STATUS(lp) != MARKED) {
		if (STATUS(lp) == NORMAL) {
			if (CAS(&node->child[LEFT], lp, NORMAL, lp, MARKED)) 
				return;
		}	
		else if (STATUS(lp) == UNQNULL) {
			if (CAS(&node->child[LEFT], lp, UNQNULL, NULL, MARKED)) 
				return;					
		}
		lp = node->child[LEFT];
	}
	return;
}

markStatus_t markRight(Node *node, Node *ancNode, int* dataPtr) {
	Node *rp = node->child[RIGHT];
	while(STATUS(rp) != MARKED) {
		if (STATUS(rp) == NORMAL) {
			if (CAS(&node->child[RIGHT], rp, NORMAL, rp, MARKED))
				return RIGHT_MARKED;
		}
		else if (STATUS(rp) == PROMOTE) {
			return REMOVE_ANCNODE;
		}
		else if (STATUS(rp) == UNQNULL) {
			if (CAS(&node->child[RIGHT], rp , UNQNULL, NULL, MARKED))
				return RIGHT_MARKED;
		}
		rp = node->child[RIGHT];
	}
}

markStatus_t markNode(Node *node, Node *ancNode, int *dataPtr, int data) {
	Node *rp = node->child[RIGHT];
	while(STATUS(rp) != NORMAL) {
		if ((GETADDR(rp) == root) && (STATUS(rp) == UNQNULL) && (STATUS(dataPtr) == MARKED)) {
			// Do the helping.
			// Find successor.
			seekNode* succSeek = seekTree(node, INT_MAX);
			// Mark Successor	
			// help swap data.
			// remove successor
			// then return false;
			return REMOVE_ANCNODE;
		}
		else if (STATUS(rp) == MARKED) {
			markLeft(node);
			break;
		}
		else if (STATUS(rp) == PROMOTE) {
			return REMOVE_ANCNODE;
		}
		else if (STATUS(rp) == UNQNULL) {
			if (CAS(&node->child[RIGHT], rp, UNQNULL, NULL, MARKED)) {
				markLeft(node);
				break;
			}
		}
		rp = node->child[RIGHT];
	}
	Node *lp = node->child[LEFT];
	while(STATUS(lp) != NORMAL) {
		if (STATUS(lp) == MARKED) {
			markStatus_t rightStat = markRight(node, ancNode, dataPtr);
			if (rightStat == REMOVE_ANCNODE)
				return REMOVE_ANCNODE;
			break;
		}
		else if (STATUS(lp) == UNQNULL) {
			if (CAS(&node->child[LEFT], lp, UNQNULL, NULL, MARKED)) {
				markStatus_t rightStat = markRight(node, ancNode, dataPtr);
				if (rightStat == REMOVE_ANCNODE)
					return REMOVE_ANCNODE;
				break;
			}
		}
		lp = node->child[LEFT];
	}
	rp = node->child[RIGHT];
	lp = node->child[LEFT];
	if (((STATUS(rp) == MARKED) || (STATUS(rp) == PROMOTE)) && (STATUS(lp) == MARKED)) {
		if ((GETADDR(rp) == NULL) && (GETADDR(lp) == NULL))
			return REMOVE_0C;
		return REMOVE_1C;
	}
	if (CAS(&(node->dataPtr), dataPtr, NORMAL, dataPtr, MARKED)) {
		return REMOVE_2C;
	}
	return ABORT_REMOVE;	
}

bool removeTreeNode(Node *pred, Node *curr, int data) {
	if (ISNULL(pred))
		return false;
	int predData = GETDATA(pred);
	Node *ptr;
	int status;
	if ((ISNULL(curr->child[RIGHT]) || (STATUS(curr->child[RIGHT]) == PROMOTE)) && ISNULL(curr->child[LEFT]))
		ptr = curr, status = UNQNULL;
	else if (ISNULL(curr->child[RIGHT]))
		ptr = curr->child[LEFT], status = NORMAL;
	else
		ptr = curr->child[RIGHT], status = NORMAL;
	if (data > predData) {
		if (STATUS(pred->dataPtr) == MARKED)
			ptr = root, status = UNQNULL;
		if (CAS(&pred->child[RIGHT], curr, NORMAL, ptr, status)) {
			GETADDR(pred->child[RIGHT])->bl = pred;
			return true;
		}
		else {
			if ((STATUS(GETADDR(pred)->child[LEFT]) == MARKED) || (STATUS(GETADDR(pred)->child[RIGHT]) == MARKED)) {
				// Help Pred Removal, by marking it first
				// Node with 0/1C. Marking doesn't require ancNode in this case
				markNode(pred, NULL, pred->dataPtr, predData);
				removeTreeNode(pred->bl, pred, predData);
				return removeTreeNode(curr->bl, curr, data);
			}
			GETADDR(pred->child[RIGHT])->bl = pred;
			return true;
		}
	}
	else {
		if (CAS(&pred->child[LEFT], curr, NORMAL, ptr, status))	 {
			GETADDR(pred->child[LEFT])->bl = pred;
			return true;
		}
		else {
			// CAS failed because it is either deleted 
			// or pred is marked. Deleted means return update bl and return true.
			// Marked means, try removing pred.
			if ((STATUS(GETADDR(pred)->child[LEFT]) == MARKED) || (STATUS(GETADDR(pred)->child[RIGHT]) == MARKED)) {
				// Help Pred Removal, by marking it first
				// Node with 0/1C. Marking doesn't require ancNode in this case
				markNode(pred, NULL, pred->dataPtr, predData);
				removeTreeNode(pred->bl, pred, predData);
				return removeTreeNode(curr->bl, curr, data);
			}
			GETADDR(pred->child[LEFT])->bl = pred;
			return true;
		}			
	}
}

bool removeTreeNode2C(Node *pred, Node *curr, int *ancNodeDataPtr, int data) {
	// Find the successor.
	seekNode *succSeek = seekTree(curr, INT_MAX);
	Node *succPred = succSeek->pred;
	Node *succCurr = succSeek->curr;
	int succData = GETDATA(succPred);
	// Mark the successor
	while(ISNULL(succCurr)) {
		if (CAS(&succPred->child[RIGHT], succCurr, UNQNULL, succCurr, PROMOTE)) {
			// help swap data
			helpSwapData(GETADDR(succPred), curr, ancNodeDataPtr);
			markLeft(succPred);
			return removeTreeNode(succPred->bl, succPred, succData);
		}
		else {
			// CAS failed means with "pred" is marked or another node inserted.
			// For pred marked, help removal of pred and then reseek
			if (ISMARKED(succPred)) {
				markLeft(succPred);
				removeTreeNode(succPred->bl, succPred, succData);
				return removeTree(curr, data);
			}
			else {
				// reseek
				return removeTree(curr, data);
			}
		}
	}
}

bool removeTree(Node *startNode, int data) {
	seekNode *remSeek = seekTree(startNode, data);
	Node *ancNode = remSeek->ancNode;
	int ancNodeDataPrev = remSeek->ancNodeData;
	Node *pred = remSeek->pred;
	Node *curr = remSeek->curr;
	int *ancNodeDataPtr = ancNode->dataPtr;
	int ancNodeDataCurr = GETDATA(ancNode);
	if (ancNodeDataCurr != ancNodeDataPrev)
		return removeTree(ancNode, data);
	if (ISNULL(curr))
		return false;
	int *dataPtr = GETADDR(curr)->dataPtr;
	markStatus_t stat = markNode(curr, ancNode, dataPtr, data);
	if (stat == ABORT_REMOVE)
		return false;
	if (stat == REMOVE_ANCNODE) {
		// Help Swap Data. Mark Curr's Left and remove curr. Then Remove ancNode.
		helpSwapData(GETADDR(curr), ancNode, ancNodeDataPtr);
		markLeft(curr);
		removeTreeNode(pred, curr, data); 
		return removeTree(ancNode, data);
	}
	else if ((stat == REMOVE_0C) || (stat == REMOVE_1C)) {
		return removeTreeNode(pred, curr, data);
	}
	else if (stat == REMOVE_2C) {	
		// Find and mark successor.
		// helpSwapData 
		// remove successor
		std::cout<<data<<" : 2C"<<std::endl;
		return removeTreeNode2C(pred, curr, dataPtr, data);
	}
	return false;
}

void helpSwapData(Node *succ, Node *ancNode, int *ancNodeDataPtr) {
	// Here if else condition can be put but it is redundent.
	// CAS failed means dataPtr is swapped and therefore no
	// help is required.
	CAS(&ancNode->dataPtr, ancNodeDataPtr, MARKED, succ->dataPtr, NORMAL);
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
			helpSwapData(pred, ancNode, ancNodeDataPtr);
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

void testbenchParallel() {
	const int numThreads = 10;
	srand(time(NULL));
	std::vector<std::thread> addT(numThreads);
	int arr[numThreads];
	for (int i = 0; i < numThreads; i++)
		arr[i] = rand();
	for (int i = 0; i < numThreads; i++)
		addT[i] = std::thread(insertTree, root, arr[i]);
	for (int i = 0; i < numThreads; i++)
		addT[i].join();
	printTree(root->child[LEFT]);
	std::cout<<"Removing Elements..."<<std::endl;
	for (int i = 0; i < numThreads; i++)
		removeTree(root, arr[i]);
	printTree(root->child[LEFT]);
/*	do {
		int removeElement;
		std::cout<<"Enter an element to remove : ";
		std::cin>>removeElement;
		removeTree(root, removeElement);
		printTree(root->child[LEFT]);
	} while(removeElement != -1); */
}

int main(void) {
	//testbenchSequential();
	testbenchParallel();
	return 0;
}
