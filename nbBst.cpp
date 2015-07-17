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
		bl = this;
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
	int ancNodeData = GETDATA(ancNode), currData;
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
			markLeft(node);
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
		if (STATUS(rp) == MARKED) {
			markLeft(node);
			break;
		}
		else if (STATUS(rp) == PROMOTE) {
			markLeft(node);
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
	if ((GETADDR(curr->child[LEFT]) == NULL) && (GETADDR(curr->child[RIGHT]) == NULL)) {
		if (STATUS(pred->dataPtr) == MARKED)
			ptr = root, status = UNQNULL;	
		else
			ptr = curr, status = UNQNULL;
	}
	else if (GETADDR(curr->child[LEFT]) == NULL)
		ptr = GETADDR(curr->child[RIGHT]), status = NORMAL;
	else
		ptr = GETADDR(curr->child[LEFT]), status = NORMAL;
	if (data > predData) {
		if (CAS(&pred->child[RIGHT], curr, NORMAL, ptr, status)) {
			std::cout<<data<<" "<<__LINE__<<std::endl;
			// Update Back Link and return
			if (ptr != root)
				GETADDR(pred->child[RIGHT])->bl = pred;
			return true;
		}
		else {
			// CAS Failed.
			Node *predPtr = pred->child[RIGHT];
			if ((GETADDR(predPtr) == curr) && (STATUS(predPtr) == MARKED)) {
				removeTreeNode(pred->bl, pred, predData);
				return removeTreeNode(curr->bl, curr, data);
			}
			else {
				// Other than marked, every other status means that the node is deleted.
				if (ptr != root)
					GETADDR(pred->child[RIGHT])->bl = pred;
				return true;
			}
		}
	}
	else {
		if (CAS(&pred->child[LEFT], curr, NORMAL, ptr, status)) {
			std::cout<<data<<" "<<__LINE__<<std::endl;
			// Update Back Link and return
			if (ptr != root) && (GETADD(pred->child[LEFT]) != NULL)
				GETADDR(pred->child[LEFT])->bl = pred;
			return true;
		}
		else {
			// CAS failed.
			std::cout<<data<<" "<<__LINE__<<std::endl;
			Node *predPtr = pred->child[LEFT];
			if ((GETADDR(predPtr) == curr) && (STATUS(predPtr) == MARKED)) {
				removeTreeNode(pred->bl, pred, predData);
				return removeTreeNode(curr->bl, curr, data);
			}
			else {
				std::cout<<data<<" "<<__LINE__<<std::endl;
				// Other than marked, every other status means that the curr node is deleted.
				if (ptr != root)
					GETADDR(pred->child[LEFT])->bl = pred;
				return true;
			}
		}	
	}
}

bool removeTreeNodeTwoChild(Node *pred, Node *curr, int *dataPtr, int data) {
	Node *rp = curr->child[RIGHT];
	if (ISNULL(rp)) {
		return removeTree(pred, data);
	}
	if ( ISMARKED(rp) && (GETADDR(rp->child[LEFT]) == NULL) && (GETADDR(rp->child[RIGHT]) == NULL) ) {
		std::cout<<"In Line : "<<__LINE__<<std::endl;
		// Help remove "rp"(Zero Child) and then this.
		markNode(rp, NULL, NULL, GETDATA(rp));
		removeTreeNode(curr, rp, GETDATA(rp));
		
		return removeTree(pred, data);
	}
	seekNode *succSeek = seekTree(GETADDR(curr->child[LEFT]), INT_MAX);
	Node *succ = succSeek->pred;
	Node *succRight = succSeek->curr;

	// Mark the successor Right for Promote
	if (CAS(&succ->child[RIGHT], succRight, UNQNULL, NULL, PROMOTE)) {
		std::cout<<data<<" IF Line : "<<__LINE__<<std::endl;
		// Here you help swap data. Mark the other child of successor
		// Now remove the successor it self.
		helpSwapData(succ, curr, dataPtr);
		markLeft(succ);
		return removeTreeNode(succ->bl, succ, GETDATA(succ));
	}
	else {
		std::cout<<data<<" Else Line : "<<__LINE__<<std::endl;
		return true;
		// CAS can fail if status of succRight is not UNQNULL
		// or some other node got inserted.
/*		Node *succRP = succ->child[RIGHT];
		if (STATUS(succRP) == PROMOTE) {
			// Here you help swap data. Mark the other child of successor
			// Now remove the successor it self.
			helpSwapData(succ, curr, dataPtr);
			markLeft(succ);
			return removeTreeNode(succ->bl, succ, GETDATA(succ));
		}
		else if (STATUS(succRP) == MARKED) {
			// Remove this node and reseek.
			markNode(rp, NULL, NULL, GETDATA(rp));
			removeTreeNode(succ->bl, succ, GETDATA(succ));
			return removeTree(pred, data);
		}
		else if (STATUS(succRP) == NORMAL) {
			// reseek
			return removeTree(pred, data);
		} */
	}
}

bool removeTree(Node *startNode, int data) {
	seekNode *remSeek = seekTree(startNode, data);
	Node *ancNode = remSeek->ancNode;
	Node *pred = remSeek->pred;
	Node *curr = remSeek->curr;
	int ancNodeDataPrev = remSeek->ancNodeData;
	int ancNodeDataCurr = GETDATA(ancNode);
	if (ancNodeDataPrev != ancNodeDataCurr)
		return removeTree(ancNode->bl, data);
	if (ISNULL(curr))
		return false;
	int *currDataPtr = GETADDR(curr)->dataPtr;
	markStatus_t markStat = markNode(GETADDR(curr), ancNode, currDataPtr, data);
	std::cout<<data<<" "<<markStat<<std::endl;
	if (markStat == ABORT_REMOVE)
		return false;
	else if ((markStat == REMOVE_0C) || (markStat == REMOVE_1C)) {
		return removeTreeNode(pred, curr, data);
	}
	else if (markStat == REMOVE_2C) {
		std::cout<<data<<" : 2C"<<std::endl;
		return removeTreeNodeTwoChild(pred, GETADDR(curr), currDataPtr, data);
	}
	return true;
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
	std::vector<std::thread> removeT(numThreads);
	int arr[numThreads];
	for (int i = 0; i < numThreads; i++)
		arr[i] = rand();
	for (int i = 0; i < numThreads; i++)
		addT[i] = std::thread(insertTree, root, arr[i]);
	for (int i = 0; i < numThreads; i++)
		addT[i].join();
	printTree(root->child[LEFT]);
	std::cout<<"Removing Elements..."<<std::endl;
/*	for (int i = 0; i < numThreads; i++) {
		removeTree(root, arr[i]);
	} */
	for (int i = 0; i < numThreads; i++)
		removeT[i] = std::thread(removeTree, root, arr[i]);
	for (int i = 0; i < numThreads; i++)
		removeT[i].join(); 
	std::cout<<"Printing Removed Elements..."<<std::endl;
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
