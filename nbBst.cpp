#include <iostream>
#include <climits>
#include <vector>
#include <thread>
#include <atomic>
#include "declerations.h"
#include <mutex>

std::mutex myMutex;
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


#define DEBUG_MSG(node)	std::cout<<"[SIES] "<<__LINE__<<" "<<GETDATA(node)<<"  "<<STATUS(GETADDR(node)->child[RIGHT])<<" "<<STATUS(GETADDR(node)->child[LEFT])<<std::endl;
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
		child[LEFT] = MARKNODE(NULL, UNQNULL);
		child[RIGHT] = MARKNODE(NULL, UNQNULL);
		bl = this;
	}

	Node(int data, Node *bl) {
		dataPtr = new int(data);
		child[LEFT] = MARKNODE(NULL, UNQNULL);
		child[RIGHT] = MARKNODE(NULL, UNQNULL);
		this->bl = GETADDR(bl);
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

seekNode* seekTree(Node *startNode, Node *myAncNode, int data) {
	Node *ancNode = myAncNode;
	Node *curr = startNode;
	Node *pred = curr->bl;
	int ancNodeData = GETDATA(ancNode);
	while(true) {
		if (ISNULL(curr) || (STATUS(curr) == PROMOTE))
			break;
		int currData = GETDATA(curr);
		if (data == currData) {
			break;
		}
		else if (data < currData) {
			ancNode = pred;
			ancNodeData = GETDATA(ancNode);
			pred = GETADDR(curr);
			curr = GETADDR(curr)->child[LEFT]; 
		}
		else if (data > currData) {
			pred = GETADDR(curr);
			curr = GETADDR(curr)->child[RIGHT];
		}
	}
	seekNode *mySeek = new seekNode(ancNode, ancNodeData, pred, curr);
	return mySeek;
}

void helpSwapData(int *succDp, Node *ancNode, int *ancNodeDp) {
	CAS(&(ancNode->dataPtr), ancNodeDp, MARKED, succDp, STATUS(succDp));
	return;
}

void markLeft(Node *node) {
	Node *lp = node->child[LEFT];
	while(STATUS(lp) != MARKED) {
		if (STATUS(lp) == UNQNULL) {
			if (CAS(&(node->child[LEFT]), lp, UNQNULL, NULL, MARKED))
				return;
		}
		else if (STATUS(lp) == NORMAL) {
			if (CAS(&(node->child[LEFT]), lp, NORMAL, lp, MARKED))
				return;
		}
		else if (STATUS(lp) == PROMOTE) {
			if (CAS(&(node->child[LEFT]), lp, PROMOTE, lp, MARKED))
				return;
		}
		lp = node->child[LEFT];
	}
	return;
}

void markLeftPromote(Node *node) {
	Node *lp = node->child[LEFT];
	while(STATUS(lp) != PROMOTE) {
		if (STATUS(lp) == NORMAL) {
			if (CAS(&(node->child[LEFT]), lp, NORMAL, lp, PROMOTE)) {
				return;
			}
		}
		else if (STATUS(lp) == MARKED) {
			return;
		}
		else if (STATUS(lp) == UNQNULL) {
			if (CAS(&(node->child[LEFT]), lp, NORMAL, UNQNULL, PROMOTE)) {
				return;
			}
		}
		lp = node->child[LEFT];
	}
	return;
}

markStatus_t markRight(Node *node) {
	Node *rp = node->child[RIGHT];
	while(STATUS(rp) != MARKED) {
		if (STATUS(rp) == PROMOTE) {
			return REMOVE_ANCNODE;
		}
		else if (STATUS(rp) == UNQNULL) {
			if (CAS(&(node->child[RIGHT]), rp, UNQNULL, NULL, MARKED)) {
				return RIGHT_MARKED;
			}
		}
		else if (STATUS(rp) == NORMAL) {
			if (CAS(&(node->child[RIGHT]), rp, NORMAL, rp, MARKED)) {
				return RIGHT_MARKED;
			}
		}
		rp = node->child[RIGHT];
	}
	return RIGHT_MARKED;
}

markStatus_t markTreeNode(Node *node, int *dataPtr) {
	Node *rp = node->child[RIGHT];
	while(STATUS(rp) != NORMAL) {
		if (STATUS(rp) == MARKED) {
			markLeft(node);
			break;
		}
		else if (STATUS(rp) == UNQNULL) {
			if (CAS(&(node->child[RIGHT]), rp, UNQNULL, NULL, MARKED)) {
				markLeft(node);
				break;
			}
		}
		else if (STATUS(rp) == PROMOTE) {
			markLeft(node);
			return REMOVE_ANCNODE;
		}
		rp = node->child[RIGHT];
	}

	Node *lp = node->child[LEFT];
	while(STATUS(lp) != NORMAL) {
		if (STATUS(lp) == MARKED) {
			markStatus_t rightStat = markRight(node);
			if (rightStat == REMOVE_ANCNODE)
				return REMOVE_ANCNODE;
			break;
		}
		else if (STATUS(lp) == UNQNULL) {
			if (CAS(&(node->child[LEFT]), lp, UNQNULL, NULL, MARKED)) {
				markStatus_t rightStat = markRight(node);
				if (rightStat == REMOVE_ANCNODE)
					return REMOVE_ANCNODE;
				break;
			}
		}
		else if (STATUS(lp) == PROMOTE) {
			if (CAS(&(node->child[LEFT]), lp, PROMOTE, lp, MARKED)) {
				markStatus_t rightStat = markRight(node);
				if (rightStat == REMOVE_ANCNODE)
					return REMOVE_ANCNODE;
				break;
			}
		}
		lp = node->child[LEFT];
	}
	rp = node->child[RIGHT];
	lp = node->child[LEFT];
	if ((ISNULL(rp) || (STATUS(rp) == PROMOTE)) && ISNULL(lp))
		return REMOVE_0C;
	else if ((ISNULL(lp)) || (ISNULL(rp) || (STATUS(rp) == PROMOTE) ))
		return REMOVE_1C;
	if (CAS(&(node->dataPtr), dataPtr, NORMAL, dataPtr, MARKED))
		return REMOVE_2C;
	else if (STATUS(node->dataPtr) == MARKED)
		return REMOVE_2C;
	return ABORT_REMOVE;
}

bool removeTreeNode(Node *pred, Node *curr, int data) {
	int predData = GETDATA(pred);
	int status = -1;
	Node *ptr, *rp, *lp;
	rp = curr->child[RIGHT];
	lp = curr->child[LEFT];
	if ( (ISNULL(rp) || (STATUS(rp) == PROMOTE)) && (ISNULL(lp)) ) {
		if ((STATUS(pred->dataPtr) == MARKED) && (data > predData))
			ptr = root, status = UNQNULL;
		ptr = curr, status = UNQNULL;
	}
	else if (ISNULL(rp) || (STATUS(rp) == PROMOTE)) {
		ptr = GETADDR(lp), status = NORMAL;
	}
	else 
		ptr = GETADDR(rp), status = NORMAL;

	if (data > predData) {
		if (CAS(&(pred->child[RIGHT]), curr, NORMAL, ptr, status)) {
			if (ptr != root && ptr != NULL)
				CAS(&(ptr->bl), curr, NORMAL, pred, NORMAL);
			return true;
		}
        else {
			Node *predPtr = pred->child[RIGHT];
			if ((STATUS(predPtr) == MARKED) && (GETADDR(predPtr) == curr)) {
				Node *predBl = pred->bl;
				if ((GETADDR(predBl->child[RIGHT]) == pred) || (GETADDR(predBl->child[LEFT]) == pred)) {
					markLeft(pred);
					removeTreeNode(predBl, pred, predData);
				}
				return removeTreeNode(curr->bl, curr, data);
			}
			return false;
		}
	}
	else {
		if (CAS(&(pred->child[LEFT]), curr, NORMAL, ptr, status)) {
			if (ptr != root && ptr != NULL)
				CAS(&(ptr->bl), curr, NORMAL, pred, NORMAL);
			return true;
		}
        else {
			Node *predPtr = pred->child[LEFT];
			if ((STATUS(predPtr) == MARKED) && (GETADDR(predPtr) == curr)) {
				Node *predBl = pred->bl;
				if ((GETADDR(predBl->child[RIGHT]) == pred) || (GETADDR(predBl->child[LEFT]) == pred)) {
					markStatus_t rightStat = markRight(pred);
					if (rightStat == REMOVE_ANCNODE) {
						helpSwapData(pred->dataPtr, GETADDR(pred->child[RIGHT]), GETADDR(pred->child[LEFT])->dataPtr);
						removeTreeNode(predBl, pred, predData);
						removeTree(root->child[LEFT], root, predData);
					}
					else 
						removeTreeNode(predBl, pred, predData);
				}
				return removeTreeNode(curr->bl, curr, data);
			}
			return false;
		}
	}
}

bool removeTreeNodeTwoChild(Node *pred, Node *curr, int *currDataPtr, int data) {
	Node *rp = curr->child[RIGHT];
	if (STATUS(rp) == NORMAL) {
		if ((GETADDR(curr->child[RIGHT]) == NULL) && (GETADDR(curr->child[LEFT]) == NULL) && (STATUS(curr->child[LEFT]) == MARKED) && (STATUS(curr->child[RIGHT]) == MARKED)) {
			Node *rPtr = GETADDR(rp);
			if (CAS(&(curr->child[RIGHT]), rPtr, NORMAL, NULL, MARKED)) {
				markLeft(curr);
				return removeTreeNode(pred, curr, data);
			}
			return removeTreeNodeTwoChild(pred, curr, currDataPtr, data);
		}
	}
	else if (STATUS(rp) == MARKED) {
		markLeft(curr);
		return removeTreeNode(pred, curr, data);
	}
	else if (STATUS(rp) == PROMOTE) {
		helpSwapData(currDataPtr, GETADDR(GETADDR(curr)->child[RIGHT]), GETADDR(GETADDR(curr)->child[LEFT])->dataPtr);
		removeTreeNode(pred, curr, data);
		return removeTree(root->child[LEFT], root, data);
	}
	else if ((STATUS(rp) == UNQNULL) && (GETADDR(rp) != root)) {
		return removeTree(root->child[LEFT], root, data);
	}

	Node *lp = curr->child[LEFT];
	if (STATUS(lp) != NORMAL)
		return removeTree(root->child[LEFT], root, data);
	
	seekNode *succSeek = seekTree(GETADDR(lp), curr, INT_MAX);
	Node *sc = succSeek->pred;
	Node *sr = succSeek->curr;
	int *sdp = sc->dataPtr;
	int sd = *(int *)((uintptr_t)sdp & ~0x03);
	if (data != GETDATA(curr))
		return false;
	if ((GETADDR(sr) == root) && (STATUS(sdp) == MARKED)) {
		removeTreeNodeTwoChild(sc->bl, sc, sdp, sd);
		return removeTreeNodeTwoChild(pred, curr, currDataPtr, data);
	}
	else if (CAS(&(sc->child[RIGHT]), sr, UNQNULL, curr, PROMOTE)) {
		helpSwapData(sdp, curr, currDataPtr);
		markLeftPromote(sc);
		if (ISMARKED(sc)) {
			removeTreeNode(sc->bl, sc, sd);
			return removeTree(root->child[LEFT], root, sd);
		}
		// This must be modified.
		return removeTreeNode(sc->bl, sc, sd);
	}
	else if (STATUS(sr) == PROMOTE) {
		helpSwapData(sdp, curr, currDataPtr);
		markLeftPromote(sc);
		if (ISMARKED(sc)) {
			removeTreeNode(sc->bl, sc, sd);
			return removeTree(root->child[LEFT], root, sd);
		}
		// This must be modified.
		return removeTreeNode(sc->bl, sc, sd);
	}
	else if (STATUS(sr) == MARKED) {
		markLeft(sc);
		removeTreeNode(sc->bl, sc, sd);
	}
	return removeTreeNodeTwoChild(pred, curr, currDataPtr, data);
}

bool removeTree(Node *startNode, Node *myAncNode, int data) {
	seekNode *remSeek = seekTree(startNode, myAncNode, data);
	Node *ancNode = remSeek->ancNode;
	Node *pred = remSeek->pred;
	Node *curr = remSeek->curr;
	int ancNodeDataPrev = remSeek->ancNodeData;
	int ancNodeDataCurr = GETDATA(ancNode);
	if (ancNodeDataPrev != ancNodeDataCurr)
		return removeTree(root->child[LEFT], root, data);
	if (ISNULL(curr) || (STATUS(curr) == PROMOTE))
		return false;
	int *currDataPtr = GETADDR(curr)->dataPtr;
	int currData = *(int *)((uintptr_t)currDataPtr & ~0x03);
	if (data == currData) {
		markStatus_t markStat = markTreeNode(GETADDR(curr), currDataPtr);
		std::cout<<data<<" : "<<markStat<<std::endl;
		if ((markStat == REMOVE_0C) || (markStat == REMOVE_1C))
			return removeTreeNode(pred, GETADDR(curr), data);
		else if (markStat == REMOVE_2C) 
			return removeTreeNodeTwoChild(pred, GETADDR(curr), currDataPtr, data); 
		else if (markStat == REMOVE_ANCNODE) {
			DEBUG_MSG(curr)
			helpSwapData(currDataPtr, ancNode, ancNode->dataPtr);
			removeTreeNode(pred, GETADDR(curr), data);
			return removeTree(root->child[LEFT], root, data);
		}
		else if (markStat == ABORT_REMOVE)
			return false;
	}
	else 
		return removeTree(root->child[LEFT], root, data);
}

bool insertTree(Node *startNode, int data) {
	seekNode *insSeek = seekTree(startNode, startNode, data);
	Node *pred = insSeek->pred;
	Node *curr = insSeek->curr;
	int predData = GETDATA(pred);
	if (ISNULL(curr) || STATUS(curr == PROMOTE)) {
		Node *myNode = new Node(data, pred);
		if (data > predData) {
			pred->child[RIGHT] = myNode;
			return true;	
		}
		else {
			pred->child[LEFT] = myNode;
			return true;	
		}
	}
	return false;
}

void printTree(Node *node) {
	if (ISNULL(node) || (STATUS(node) == PROMOTE))
		return;
	printTree(GETADDR(node)->child[LEFT]);
	std::cout<<"[VARUN] : "<<GETDATA(node)<<std::endl;
	printTree(GETADDR(node)->child[RIGHT]);
}

void testbenchParallel() {
	const int numThreads = 100;
	srand(time(NULL));
	int arr[numThreads];
	std::vector<std::thread> removeT(numThreads);
	for (int i = 0; i < numThreads; i++) {
		do {
			arr[i] = rand();
		}while(arr[i] == INT_MAX);
	}
	for (int i = 0; i < numThreads; i++) {
		insertTree(root, arr[i]);
	}
//	printTree(root->child[LEFT]);
	std::cout<<"Removing Elements"<<std::endl;
	for (int i = 0; i < numThreads; i++) 
		removeT[i] = std::thread(&removeTree, root->child[LEFT], root, arr[i]);
	for (int i = 0; i < numThreads; i++)
		removeT[i].join();
	std::cout<<"Printing Removed Elements"<<std::endl;
	printTree(root->child[LEFT]);
}

int main(void) {
	//testbenchSequential();
	testbenchParallel();
	return 0;
}
