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
		int predData = GETDATA(pred);
		if (ISNULL(curr) || ((STATUS(curr) == PROMOTE) && (GETADDR(curr) == ancNode))) 
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
		else if (STATUS(lp) == PROMOTE)
			return;
		lp = node->child[LEFT];
	}
}

markStatus_t markLeftPromote(Node *node) {
	Node *lp = node->child[LEFT];
	while(true) {
		if (STATUS(lp) == UNQNULL) {
			if (CAS(&(node->child[LEFT]), lp, UNQNULL, NULL, PROMOTE))
				return LEFT_MARKED;
		}
		else if (STATUS(lp) == MARKED)
			return REMOVE_ANCNODE;
		else if (STATUS(lp) == NORMAL) {
			if (CAS(&(node->child[LEFT]), lp, NORMAL, lp, PROMOTE))
				return LEFT_MARKED;
		}
		else if (STATUS(lp) == PROMOTE)
			return LEFT_MARKED;

		lp = node->child[LEFT];
	}
}

markStatus_t markRight(Node *node) {
	Node *rp = node->child[RIGHT];
	while(STATUS(rp) != MARKED) {
		if (STATUS(rp) == UNQNULL) {
			if (CAS(&(node->child[RIGHT]), rp, UNQNULL, NULL, MARKED)) {
				return RIGHT_MARKED;
			}
		}
		else if (STATUS(rp) == NORMAL) {
			if (CAS(&(node->child[RIGHT]), rp, NORMAL, rp, MARKED)) {
				return RIGHT_MARKED;
			}		
		}
		else if (STATUS(rp) == PROMOTE)
			return REMOVE_ANCNODE;
		rp = node->child[RIGHT];
	}
}

markStatus_t markTreeNode(Node *node, int *dp) {
	Node *rp = node->child[RIGHT];
	while(STATUS(rp) != NORMAL) {
		if (STATUS(rp) == UNQNULL && GETADDR(rp) == root)
			return HELP_REMOVE;
		else if (STATUS(rp) == UNQNULL && GETADDR(rp) != root) {
			if (CAS(&(node->child[RIGHT]), rp, UNQNULL, NULL, MARKED)) {
				markLeft(node);
				break;
			}
		}
		else if (STATUS(rp) == MARKED) {
			markLeft(node);
			break;
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
			markStatus_t markStat = markRight(node);
			if (markStat == REMOVE_ANCNODE)
				return REMOVE_ANCNODE;
			break;
		}
		else if (STATUS(lp) == PROMOTE) {
			return REMOVE_ANCNODE;
		}
		else if (STATUS(lp) == UNQNULL) {
			if (CAS(&(node->child[LEFT]), lp, UNQNULL, NULL, MARKED)) {
				markStatus_t markStat = markRight(node);
				if (markStat == REMOVE_ANCNODE)
					return REMOVE_ANCNODE;
				break;
			}
		}
		lp = node->child[LEFT];
	}
	rp = node->child[RIGHT];
	lp = node->child[LEFT];
	if (ISNULL(lp) && ISNULL(rp))
		return REMOVE_0C;
	else if (ISNULL(lp) || ISNULL(rp))
		return REMOVE_1C;
	if (CAS(&(node->dataPtr), dp, NORMAL, dp, MARKED))
		return REMOVE_2C;
	if (STATUS(node->dataPtr) == MARKED)
		return REMOVE_2C;
	return ABORT_REMOVE;
}

bool removeTreeNode(Node *pred, Node *curr, int data) {
	int pd = GETDATA(pred), status = NORMAL;
	Node *ptr;
	Node *rp = curr->child[RIGHT];
	Node *lp = curr->child[LEFT];
	if ((ISNULL(rp) || (STATUS(rp) == PROMOTE)) && (ISNULL(lp))) {
		if (STATUS(pred->dataPtr) == MARKED)
			ptr = root, status = UNQNULL;
		ptr = curr, status = UNQNULL;
	}
	else if (ISNULL(lp))
		ptr = GETADDR(rp), status = NORMAL;
	else
		ptr = GETADDR(lp), status = NORMAL;
	if (data > pd) {
		if (CAS(&(pred->child[RIGHT]), curr, NORMAL, ptr, status)) {
			if (ptr != root && ptr != NULL)
				ptr->bl = pred;
			return true;
		}
		else {
			return removeTree(root, data);
			Node *predPtr = pred->child[RIGHT];
			if ((STATUS(predPtr) == MARKED) && (GETADDR(predPtr) == curr)) {
				Node *predBl = pred->bl;
				if ((GETADDR(predBl->child[LEFT]) == pred) || (GETADDR(predBl->child[RIGHT]) == pred)) {
					// pred is a part of the tree.
					markLeft(pred);
					removeTreeNode(predBl, pred, pd);
				}
				return removeTreeNode(curr->bl, curr, data);
			}
			if (ptr != root && ptr != NULL && ptr->bl == curr)
				ptr->bl = pred;
		}
		return false;
	}
	else {
		if (CAS(&(pred->child[LEFT]), curr, NORMAL, ptr, status)) {
			if (ptr != root && ptr != NULL)
				ptr->bl = pred;
			return true;
		}
		else {
			return removeTree(root, data);
			Node *predPtr = pred->child[LEFT];
			if (((STATUS(predPtr) == MARKED) || (STATUS(predPtr) == PROMOTE)) && (GETADDR(predPtr) == curr)) {
				Node *predBl = pred->bl;
				if ((GETADDR(predBl->child[LEFT]) == pred) || (GETADDR(predBl->child[RIGHT]) == pred)) {
					// pred is a part of the tree.
					markStatus_t rs = markRight(pred);
					if (rs == REMOVE_ANCNODE) {
						helpSwapData(pred->dataPtr, GETADDR(pred->child[RIGHT]),GETADDR(pred->child[RIGHT])->dataPtr );
						removeTreeNode(predBl, pred, pd);
						removeTree(root, pd);
					}
					else 
						removeTreeNode(predBl, pred, pd);
				}
				return removeTreeNode(curr->bl, curr, data);
			}
		}
		return false;
	}
}

bool removeTreeNodeTwoChild(Node *pred, Node *curr, int *dp, int data) {
	Node *rp = curr->child[RIGHT];
	if (STATUS(rp) == NORMAL) {
		Node *rPtr = GETADDR(rp);
		if ((GETADDR(rPtr->child[RIGHT]) == NULL) && (GETADDR(rPtr->child[LEFT]) == NULL) && (STATUS(rPtr->child[RIGHT]) == MARKED) && (STATUS(rPtr->child[LEFT]) == MARKED)) {
			if (CAS(&(curr->child[RIGHT]), rPtr, NORMAL, rPtr, UNQNULL)) {
				return removeTree(root, data);
			}
			return removeTree(root, data);
			return removeTreeNodeTwoChild(pred, curr, dp, data);
		}
	}
	else if (STATUS(rp) == MARKED) {
		return removeTree(root, data);
	}
	else if ((STATUS(rp) == UNQNULL) && (GETADDR(rp) != root)) {
		return removeTree(root, data);
	}
	else if (STATUS(rp) == PROMOTE) {
		helpSwapData(dp, GETADDR(rp), GETADDR(rp)->dataPtr);
		markLeftPromote(curr);
		removeTreeNode(pred, curr, data);
		return removeTree(root, data);
	}
	Node *lp = curr->child[LEFT];
	if (STATUS(lp) != NORMAL)
		return removeTree(root, data);
	
	seekNode *ss = seekTree(GETADDR(lp), root, data);
	Node *sc = ss->pred;
	Node *sr = ss->curr;
	int *sdp = sc->dataPtr;
	int sd = *(int *)((uintptr_t)sdp & ~0x03);
	if (data != GETDATA(curr))
		return false;
	if ((GETADDR(sr) == root) && (STATUS(sdp) == MARKED)) {
		removeTreeNodeTwoChild(sc->bl, sc, sdp, sd);
		removeTree(root, sd);
		//return removeTreeNodeTwoChild(pred, curr, dp, data);
		return removeTree(root, data);
	}
	else if (CAS(&(sc->child[RIGHT]), sr, UNQNULL, curr, PROMOTE)) {
		helpSwapData(sdp, curr, dp);
		markStatus_t leftStat = markLeftPromote(sc);
		removeTreeNode(sc->bl, sc, sd);
		if (leftStat == REMOVE_ANCNODE)
			return removeTree(root, sd);
	}
	else if (STATUS(sr) == PROMOTE) {
		helpSwapData(sdp, curr, dp);
		markStatus_t leftStat = markLeftPromote(sc);
		removeTreeNode(sc->bl, sc, sd);
		if (leftStat == REMOVE_ANCNODE)
			return removeTree(root, sd);
	}
	else if (STATUS(sr) == MARKED) {
		markLeft(sc);
		removeTreeNode(sc->bl, sc, sd);
		return removeTree(root, data);
	}
		return removeTree(root, data);
}

bool removeTreeNodeZeroChild(Node *pred, Node *curr, int data) {
	int predData = GETDATA(pred);
	if (data > predData) {
		if (STATUS(curr->dataPtr) == MARKED) {
			if (CAS(&(pred->child[RIGHT]), curr, NORMAL, root, UNQNULL))
				return true;
			else
				return removeTree(root, data);
		}
		else {
			if (CAS(&(pred->child[RIGHT]), curr, NORMAL, curr, UNQNULL)) {
				return true;
			}
			else
				return removeTree(root, data);
		}
	}
	else {
		if (CAS(&(pred->child[LEFT]), curr, NORMAL, curr, UNQNULL)) {
			return true;
		}
		else
			return removeTree(root, data);
	}
}

bool removeTree(Node *sn, int data) {
//	myMutex.lock();
	//std::cout<<"Removing : "<<data<<std::endl;
//	myMutex.unlock();
	seekNode *rs = seekTree(sn, root, data);	
	Node *ancNode = rs->ancNode;
	Node *pred = rs->pred;
	Node *curr = rs->curr;
	int ancNodeDataPrev = rs->ancNodeData;
	int ancNodeDataCurr = GETDATA(ancNode);
	if (ancNodeDataPrev != ancNodeDataCurr)
		return removeTree(root, data);
	if (ISNULL(curr) || ((STATUS(curr) == PROMOTE) && (GETADDR(curr) == ancNode)))
		return false;
	int* cdp = GETADDR(curr)->dataPtr;
	int cd = *(int *)((uintptr_t)cdp & ~0x03);
	if (data == cd) {
		markStatus_t stat = markTreeNode(GETADDR(curr), cdp);
	//	myMutex.lock();
	//	std::cout<<data<<" : "<<stat<<std::endl;
	//	myMutex.unlock();
		if (stat == REMOVE_0C) {
			return removeTreeNodeZeroChild(pred, GETADDR(curr), data);
		}
		if ((stat == REMOVE_1C))
			return removeTreeNode(pred, GETADDR(curr), data);
		else if (stat == REMOVE_2C) 
			return removeTreeNodeTwoChild(pred, GETADDR(curr), cdp, data);
		else if (stat == REMOVE_ANCNODE) {
			markLeft(curr);
			helpSwapData(cdp, GETADDR(curr->child[RIGHT]), GETADDR(curr->child[RIGHT])->dataPtr);
			removeTree(root, data);
			return removeTree(root, data);
		}
		else if (stat == HELP_REMOVE) {
			return removeTree(root, data);
		}
		return false;
	}
	return removeTree(root, data);
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
		else if (data < predData){
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
	std::cout<<GETDATA(node)<<" "<<STATUS(GETADDR(node)->child[LEFT])<<" "<<STATUS(GETADDR(node)->child[RIGHT])<<" "<<STATUS(GETADDR(node)->dataPtr)<<std::endl;
	printTree(GETADDR(node)->child[RIGHT]);
}

void printTreeRem(Node *node) {
	if (ISNULL(node) || (STATUS(node) == PROMOTE))
		return;
	printTree(GETADDR(node)->child[LEFT]);
	std::cout<<"[VARUN] : "<<GETDATA(node)<<" "<<STATUS(GETADDR(node)->child[LEFT])<<" "<<STATUS(GETADDR(node)->child[RIGHT])<<" "<<STATUS(GETADDR(node)->dataPtr)<<std::endl;
	printTree(GETADDR(node)->child[RIGHT]);
}

void testbenchParallel() {
	const int numThreads = 1000;
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
		removeT[i] = std::thread(&removeTree, root->child[LEFT], arr[i]);
	for (int i = 0; i < numThreads; i++)
		removeT[i].join();
	std::cout<<"Printing Removed Elements"<<std::endl;
	printTreeRem(root->child[LEFT]);
}

int main(void) {
	//testbenchSequential();
	testbenchParallel();
	return 0;
}
