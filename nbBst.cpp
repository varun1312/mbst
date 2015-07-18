#include <iostream>
#include <climits>
#include <vector>
#include <thread>
#include <atomic>
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

std::atomic<int> count;
int countPrint = 0;
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
	int ancNodeData = GETDATA(ancNode);
	while(true) {
		int currData = GETDATA(curr);
		if (data > currData) {
			if (ISNULL(GETADDR(curr)->child[RIGHT])) 
				break;
			pred = GETADDR(curr), curr = GETADDR(curr)->child[RIGHT];
		}
		else if (data < currData) {
			if (ISNULL(GETADDR(curr)->child[LEFT]))
				break;
			ancNode = pred, ancNodeData = GETDATA(ancNode), pred = GETADDR(curr), curr= GETADDR(curr)->child[LEFT];
		}
		else if (data == currData) {
				break;
		}
	}
	seekNode *mySeek = new seekNode(ancNode, ancNodeData, pred, curr);
	return mySeek;
}

void helpSwapData(Node *succ, Node *ancNode, int* dataPtr) {
//	if (CAS(&ancNode->dataPtr, dataPtr, MARKED, pred->dataPtr, MARKED))
//		return;
	// CAS failed means dataPtr already swapped. Therefore return;
	CAS(&ancNode->dataPtr, dataPtr, MARKED, succ->dataPtr, NORMAL);
	return;
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

markStatus_t markRight(Node *node) {
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


markStatus_t markTreeNode(Node *curr, Node *ancNode, int *currDataPtr, int data) {
	Node *rp = curr->child[RIGHT];
	while(STATUS(rp) != NORMAL) {
		if (STATUS(rp) == MARKED) {
			markLeft(curr);
			break;
		}
		else if (STATUS(rp) == PROMOTE) {
			markLeft(curr);
			return REMOVE_ANCNODE;
		}
		else if (STATUS(rp) == UNQNULL) {
			if (CAS(&curr->child[RIGHT], rp, UNQNULL, NULL, MARKED)) {
				markLeft(curr);
				break;
			}
		}
		rp = curr->child[RIGHT];
	}
	Node *lp = curr->child[LEFT];
	while(STATUS(lp) != NORMAL) {
		if (STATUS(lp) == MARKED) {
			markStatus_t rightStat = markRight(curr);
			if (rightStat == REMOVE_ANCNODE)
				return REMOVE_ANCNODE;
			break;
		}
		else if (STATUS(lp) == UNQNULL) {
			if (CAS(&curr->child[LEFT], lp, UNQNULL, NULL, MARKED)) {
				markStatus_t rightStat = markRight(curr);
				if (rightStat == REMOVE_ANCNODE)
					return REMOVE_ANCNODE;
				break;
			}
		}
		lp = curr->child[LEFT];
	}
	rp = curr->child[RIGHT];
	lp = curr->child[LEFT];
	if ((STATUS(rp) == MARKED) && (STATUS(lp) == MARKED)) {
		if ((GETADDR(rp) == NULL) && (GETADDR(lp) == NULL))
			return REMOVE_0C;
		return REMOVE_1C;
	}
	if (CAS(&curr->dataPtr, currDataPtr, NORMAL, currDataPtr, MARKED))
		return REMOVE_2C;
	return ABORT_REMOVE;
}

bool removeTreeNode(Node *pred, Node *curr, int data) {
	int predData = GETDATA(pred);
	Node *ptr;
	int status;

	if ((GETADDR(curr->child[RIGHT]) == NULL) && (GETADDR(curr->child[LEFT]) == NULL)) {
		if ((STATUS(pred->dataPtr) == MARKED) && (data > predData))
			ptr = root, status = UNQNULL;
		else
			ptr = curr, status = UNQNULL;
	}
	else if (GETADDR(curr->child[RIGHT]) == NULL)
		ptr = GETADDR(curr->child[LEFT]), status = NORMAL;
	else 
		ptr = GETADDR(curr->child[RIGHT]), status = NORMAL;
	
	if (data > predData) {
		if (CAS(&pred->child[RIGHT], curr, NORMAL, ptr, status)) {
			// CAS successful. Means, we need to update pred.
			Node *predRight = pred->child[RIGHT];
			//if ((STATUS(predRight) != MARKED) && (!ISNULL(predRight)))
			if ((!ISNULL(predRight)))
				GETADDR(predRight)->bl = pred;
			return true;
		}
		else {
			Node *predRight = pred->child[RIGHT];
			if ((GETADDR(predRight) == curr) && (STATUS(predRight) == MARKED)) {
				removeTreeNode(pred->bl, pred, predData);
				// We are restarting...but, we must backtrack
				return removeTree(root, data);
			}
			//if ((STATUS(predRight) != MARKED) && (!ISNULL(predRight)))
			if ((!ISNULL(predRight)))
				GETADDR(predRight)->bl = pred;
			return true;
		}
	}
	else {
		if (CAS(&pred->child[LEFT], curr, NORMAL, ptr, status)) {
			Node *predLeft = pred->child[LEFT];
			// CAS successful. Means, we need to update pred.
			//if ((STATUS(predLeft) != MARKED) && (!ISNULL(predLeft)))
			if ((!ISNULL(predLeft)))
				GETADDR(predLeft)->bl = pred;
			return true;
		}
		else {
			Node *predLeft = pred->child[LEFT];
			if ((GETADDR(predLeft) == curr) && (STATUS(predLeft) == MARKED)) {
				removeTreeNode(pred->bl, pred, predData);
				// We are restarting...but, we must backtrack
				return removeTree(root, data);
			}
			//if ((STATUS(predLeft) != MARKED) && (!ISNULL(predLeft)))
			if ((!ISNULL(predLeft)))
				GETADDR(predLeft)->bl = pred;
			return true;
		}
	}
}

bool removeTreeNodeTwoChild(Node *pred, Node *curr, int *currDataPtr, int data) {
	Node *rp = curr->child[RIGHT];
	while(STATUS(rp) != NORMAL) {
		if (ISNULL(rp) && (GETADDR(rp) == root))
			break;
		if (ISNULL(rp) && (GETADDR(rp) != root)) {
			// Mark it.
			if (CAS(&curr->child[RIGHT], rp, UNQNULL, NULL, MARKED)) {
				markLeft(curr);
				return removeTreeNode(pred, curr, data);
			}
		}
		rp = curr->child[RIGHT];
		if (STATUS(rp) == MARKED) {
			markLeft(curr);
			return removeTreeNode(pred, curr, data);
		}
	}
	if ((!ISNULL(rp)) && (GETADDR(rp->child[RIGHT]) == NULL) && (GETADDR(rp->child[LEFT]) == NULL) && (STATUS(rp->child[RIGHT]) == MARKED) && (STATUS(rp->child[LEFT]) == MARKED)) {
		if (CAS(&curr->child[RIGHT], rp, NORMAL, NULL, MARKED)) {
			markLeft(curr);
			return removeTreeNode(pred, curr, data);
		}
		rp = curr->child[RIGHT];
		while(STATUS(rp) != NORMAL) {
			if (ISNULL(rp) && (GETADDR(rp) == root))
				break;
			if (ISNULL(rp) && (GETADDR(rp) != root)) {
				// Mark it.
				if (CAS(&curr->child[RIGHT], rp, UNQNULL, NULL, MARKED)) {
					markLeft(curr);
					return removeTreeNode(pred, curr, data);
				}
			}
			rp = curr->child[RIGHT];
			if (STATUS(rp) == MARKED) {
				markLeft(curr);
				return removeTreeNode(pred, curr, data);
			}	
		}
	}
	do {
		Node *lp = curr->child[LEFT];
		if (ISNULL(lp)) {
			if (CAS(&curr->child[LEFT], lp, UNQNULL, NULL, MARKED))
				markRight(curr);
				return removeTreeNode(pred, curr, data);
		}
		seekNode *succSeek = seekTree(GETADDR(curr->child[LEFT]), INT_MAX);
		Node *succPred = succSeek->pred;
		Node *succ = succSeek->curr;
		Node *succRight = GETADDR(succ)->child[RIGHT];
		if (CAS(&succ->child[RIGHT], succRight, UNQNULL, NULL, PROMOTE)) {
			helpSwapData(GETADDR(succ), curr, currDataPtr);
			markLeft(curr);
			return removeTreeNode(succPred, succ, GETDATA(succ));
		}
		else {
			// CAS failed in marking promote.
			succRight = GETADDR(succ)->child[RIGHT];
			if (STATUS(succRight) == MARKED) {
				markLeft(succ);
				removeTreeNode(succPred, succ, GETDATA(succ));
			}
		}
	} while(true);
	return true;
}

bool removeTree(Node *startNode, int data) {
	seekNode* remSeek = seekTree(startNode, data);
	Node *ancNode = remSeek->ancNode;
	Node *pred = remSeek->pred;
	Node *curr= remSeek->curr;
	int *ancNodeDataPtr = ancNode->dataPtr;
	int ancNodeDataPrev = remSeek->ancNodeData;
	int ancNodeDataCurr = GETDATA(ancNode);
	if (ancNodeDataPrev != ancNodeDataCurr)
		return removeTree(ancNode, data);
	int *currDataPtr = GETADDR(curr)->dataPtr;
	int currData = *(int *)((uintptr_t)currDataPtr & ~0x03);
	if (data == currData) {
		markStatus_t markStat = markTreeNode( GETADDR(curr), ancNode, currDataPtr, data);
			std::cout<<data<<" : "<<markStat<<std::endl;
		//std::cout<<data<<" : "<<markStat<<std::endl;
		if (markStat == ABORT_REMOVE)
			return false;
		else if ((markStat == REMOVE_0C) || (markStat == REMOVE_1C)) {
			return removeTreeNode(pred, GETADDR(curr), data);
		}
		else if (markStat == REMOVE_2C) {
			return removeTreeNodeTwoChild(pred, GETADDR(curr), currDataPtr, data);
		}
		else if (markStat == REMOVE_ANCNODE) {
			helpSwapData(curr, ancNode, ancNodeDataPtr);
			return removeTreeNode(pred, curr, data);
		}
	}
	return false;
}

bool insertTree(Node *startNode, int data) {
	seekNode *insSeek = seekTree(startNode, data);
	Node *ancNode = insSeek->ancNode;
	Node *pred = insSeek->pred;
	Node *curr = insSeek->curr;
	int ancNodeDataPrev = insSeek->ancNodeData;
	int ancNodeDataCurr = GETDATA(ancNode);
	if (ancNodeDataPrev != ancNodeDataCurr)
		return insertTree(ancNode, data);
	int *currDataPtr = GETADDR(curr)->dataPtr;
	int currData = *(int *)((uintptr_t)currDataPtr & ~0x03);
	if (data == currData) {
		// Check for node marking. If yes, help and then return
		return false;
	}
	else if (data > currData) {
		Node *currPtr = GETADDR(curr)->child[RIGHT];
		Node *myNode = new Node(data, curr);
		if (CAS(&GETADDR(curr)->child[RIGHT], currPtr, UNQNULL, myNode, NORMAL)) {
			return true;
		}
		else {
			// CHeck for the reason of failue.	
			currPtr = GETADDR(curr)->child[RIGHT];
			if (STATUS(currPtr) == NORMAL)
				return insertTree(pred, data);
			else if (STATUS(currPtr) == MARKED) {
				// Mark, help remove curr and then insert in your style
				return false;
			}
			else if (STATUS(currPtr) == UNQNULL)
				return insertTree(pred, data);
			else if (STATUS(currPtr) == PROMOTE) {
				// Help Swap data and then return or insert node..
			}
		}
	}
	else if (data < currData) {
		Node *currPtr = GETADDR(curr)->child[LEFT];
		Node *myNode = new Node(data, curr);
		if (CAS(&GETADDR(curr)->child[LEFT], currPtr, UNQNULL, myNode, NORMAL)) {
			return true;
		}
		else {
			if (STATUS(currPtr) == MARKED) {
				// Mark, help remove curr and then insert in your style
				return false;
			}
			return insertTree(pred, data);
		}
	}
}

void printTree(Node *node) {
	if (ISNULL(node) || (STATUS(node) == PROMOTE))
		return;
	printTree(GETADDR(node)->child[LEFT]);
	std::cout<<GETDATA(node)<<"  "<<STATUS(node->child[RIGHT])<<" "<<STATUS(node->child[LEFT])<<std::endl;
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
	count.store(0);
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
	for (int i = 0; i < numThreads; i++)
		removeT[i] = std::thread(removeTree, root, arr[i]);
	for (int i = 0; i < numThreads; i++)
		removeT[i].join();	
	std::cout<<"Printing Removed Elements..."<<std::endl;
	printTree(root->child[LEFT]);
	//std::cout<<count.load()<<" "<<(countPrint - numThreads)<<std::endl;
	
}

int main(void) {
	//testbenchSequential();
	testbenchParallel();
	return 0;
}
