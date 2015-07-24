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

#define DEBUG_MSG(node)	//std::cout<<"[SIES] "<<__LINE__<<" "<<GETDATA(node)<<"  "<<STATUS(GETADDR(node)->child[RIGHT])<<" "<<STATUS(GETADDR(node)->child[LEFT])<<std::endl;
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
	Node *curr = GETADDR(startNode);
	Node *pred = curr->bl;
	Node *ancNode = myAncNode;
	int ancNodeData = GETDATA(ancNode);
	while(true) {
		if (ISNULL(curr) || STATUS(curr) == PROMOTE)
			break;
		int currData = GETDATA(curr);
		if (data > currData) {
			pred = GETADDR(curr), curr = GETADDR(curr)->child[RIGHT];
		}
		else if (data < currData) {
			ancNode = pred, ancNodeData = GETDATA(ancNode), pred = GETADDR(curr), curr= GETADDR(curr)->child[LEFT];
		}
		else if (data == currData) {
				break;
		}
	}
	seekNode *mySeek = new seekNode(ancNode, ancNodeData, pred, curr);
	return mySeek;
}

void helpSwapData(int *succDataPtr, Node *ancNode, int* dataPtr) {
//	if (CAS(&ancNode->dataPtr, dataPtr, MARKED, pred->dataPtr, MARKED))
//		return;
	// CAS failed means dataPtr already swapped. Therefore return;
	CAS(&ancNode->dataPtr, dataPtr, MARKED, succDataPtr, STATUS(succDataPtr));
	return;
}

void markLeft(Node *node) {
	Node *lp = node->child[LEFT];
	while(STATUS(lp) != MARKED) {
		if (STATUS(lp) == NORMAL) {
			if (CAS(&(node->child[LEFT]), lp, NORMAL, lp, MARKED)) 
				return;
		}	
		else if (STATUS(lp) == UNQNULL) {
			if (CAS(&(node->child[LEFT]), lp, UNQNULL, NULL, MARKED)) 
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
			if (CAS(&(node->child[RIGHT]), rp, NORMAL, rp, MARKED))
				return RIGHT_MARKED;
		}
		else if (STATUS(rp) == PROMOTE) {
			markLeft(node);
			return REMOVE_ANCNODE;
		} 
		else if (STATUS(rp) == UNQNULL) {
			if (CAS(&(node->child[RIGHT]), rp , UNQNULL, NULL, MARKED))
				return RIGHT_MARKED;
		}
		rp = node->child[RIGHT];
	}
}

markStatus_t markTreeNode(Node *curr, int *currDataPtr) {
	Node *rp = curr->child[RIGHT];
	while(STATUS(rp) != NORMAL) {
		if (STATUS(rp) == UNQNULL && GETADDR(rp) != root) {
			if (CAS(&(curr->child[RIGHT]), rp, UNQNULL, NULL, MARKED)) {
				markLeft(curr);
				break;
			}
		}
		else if ((STATUS(rp) == UNQNULL) && (GETADDR(rp) == root))
			return HELP_REMOVE;
		else if (STATUS(rp) == MARKED) {
			markLeft(curr);
			break;
		}
		else if (STATUS(rp) == PROMOTE) {
			markLeft(curr);
			return REMOVE_ANCNODE;
		}
		rp = curr->child[RIGHT];
	}
	Node *lp = curr->child[LEFT];
	while(STATUS(lp) != NORMAL) {
		if (STATUS(lp) == UNQNULL) {
			if (CAS(&(curr->child[LEFT]), lp, UNQNULL, NULL, MARKED)) {
				markRight(curr);
				break;
			}
		}
		else if (STATUS(lp) == MARKED) {
			markRight(curr);
			break;
		}
		lp = curr->child[LEFT];
	}
	rp = curr->child[RIGHT];
	lp = curr->child[LEFT];
	if ((GETADDR(rp) == NULL) && (GETADDR(lp) == NULL)) {
		return REMOVE_0C;
	}
	else if ((GETADDR(rp) == NULL) || (GETADDR(lp) == NULL))
		return REMOVE_1C;
	if (CAS(&curr->dataPtr, currDataPtr, NORMAL, currDataPtr, MARKED))
		return REMOVE_2C;
	else if (STATUS(curr->dataPtr) == MARKED)
		return REMOVE_2C;
	return ABORT_REMOVE;
}

bool removeTreeNode(Node *pred, Node *curr, int data) {
	Node *ptr, *rp, *lp;
	int predData = GETDATA(pred), status = NORMAL;
	rp = curr->child[RIGHT];
	lp = curr->child[LEFT];
	if (ISNULL(lp) && (ISNULL(rp) || (STATUS(rp) == PROMOTE))) {
		if (STATUS(pred->dataPtr) == MARKED)
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
			if (STATUS(predPtr) == MARKED && (GETADDR(predPtr) == curr)) {
				Node *predBl = pred->bl;
				if ((GETADDR(predBl->child[LEFT]) == pred) || (GETADDR(predBl->child[RIGHT]) == pred))  {
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
			if (STATUS(predPtr) == MARKED && (GETADDR(predPtr) == curr)) {
				Node *predBl = pred->bl;
				if ((GETADDR(predBl->child[LEFT]) == pred) || (GETADDR(predBl->child[RIGHT]) == pred))  {
						markStatus_t rightStat = markRight(pred);
						// Here we must help swap Data
						// This is a very important fix.
						// This is a vey big culprit which will change many things.
						if (rightStat == REMOVE_ANCNODE) {
							helpSwapData(pred->dataPtr, GETADDR(pred->child[RIGHT]), GETADDR(pred->child[RIGHT])->dataPtr);
							removeTreeNode(predBl, pred, predData);
							removeTree(root->child[LEFT], root, predData);
						}
						removeTreeNode(predBl, pred, predData);
				}
				return removeTreeNode(curr->bl, curr, data);
			}
			return false;
		}
	}
	return true;
}

bool removeTreeNodeTwoChild(Node *pred, Node *curr, int *currDataPtr, int data) {
	Node *rp = curr->child[RIGHT];
	if (STATUS(rp) == NORMAL) {
		Node *rPtr = GETADDR(rp);
		if ((GETADDR(rPtr->child[LEFT]) == NULL) && (GETADDR(rPtr->child[RIGHT]) == NULL) && (STATUS(rPtr->child[LEFT]) == MARKED) && (STATUS(rPtr->child[RIGHT]) == MARKED)) {
			if (CAS(&curr->child[RIGHT], rPtr, NORMAL, NULL, MARKED)) {
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
	else if (STATUS(rp) == UNQNULL && GETADDR(rp) != root) {
		return removeTree(root->child[LEFT], root, data);
	}

	Node *lp = curr->child[LEFT];
	if (STATUS(lp) != NORMAL)
		return removeTree(root->child[LEFT], root, data);
	
	seekNode* succSeek = seekTree(GETADDR(lp), curr, data);
	Node *sc = succSeek->pred;
	Node *sr = sc->child[RIGHT];
	int *sdp = sc->dataPtr;
	int sd = *(int *)((uintptr_t)sdp & ~0x03);
	if (data != GETDATA(curr)) 
		return false;
	if (GETADDR(sr) == root && STATUS(sdp) == MARKED) {
		removeTreeNodeTwoChild(sc->bl, sc, sdp, sd);
		return removeTreeNodeTwoChild(pred, curr, currDataPtr, data);
	}
	else if (CAS(&(sc->child[RIGHT]), sr, UNQNULL, curr, PROMOTE)) {
		helpSwapData(sdp, curr, currDataPtr);
		if (ISMARKED(sc)) {
			removeTreeNode(sc->bl, sc, sd);
			return removeTree(root->child[LEFT], root, sd);
		}
			markLeft(sc);
			return removeTreeNode(sc->bl, sc, sd);
	}
	else if (STATUS(sr) == PROMOTE) {
		helpSwapData(sdp, curr, currDataPtr);
		if (ISMARKED(sc)) {
			removeTreeNode(sc->bl, sc, sd);
			return removeTree(root->child[LEFT], root, sd);
		}
			markLeft(sc);
			return removeTreeNode(sc->bl, sc, sd);
	}
	else if (STATUS(sr) == MARKED) {
		markLeft(sc);
		removeTreeNode(sc->bl, sc, sd);
		return removeTreeNodeTwoChild(pred, curr, currDataPtr, data);
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
	if (ISNULL(curr))
		return false;
	curr = GETADDR(curr);
	int *currDataPtr = curr->dataPtr;
	int currData = *(int *)((uintptr_t)currDataPtr & ~0x03); 
	if (data == currData) {
		markStatus_t markStat = markTreeNode(curr, currDataPtr);
		std::cout<<data <<" : "<<markStat<<std::endl;
		if (markStat == REMOVE_0C) {
			return removeTreeNode(pred, curr, data);
		}
		else if (markStat == REMOVE_1C) {
			return removeTreeNode(pred, curr, data);
		}
		else if (markStat == REMOVE_2C) {
			return removeTreeNodeTwoChild(pred, curr, currDataPtr, data);
		}
		else if (markStat == REMOVE_ANCNODE) {
			helpSwapData(currDataPtr, GETADDR(curr->child[RIGHT]),GETADDR(curr->child[RIGHT])->dataPtr );
			removeTreeNode(pred, curr, data);
			return removeTree(root->child[LEFT], root, data);
		}
		else if (markStat == ABORT_REMOVE)
			return false;	
	}
	else 
		return removeTree(root->child[LEFT], root, data);
}

bool insertTree(Node *startNode, int data) {
	seekNode *insSeek = seekTree(startNode, root, data);
	Node *ancNode = insSeek->ancNode;
	Node *pred = insSeek->pred;
	Node *curr = insSeek->curr;
	int ancNodeDataPrev = insSeek->ancNodeData;
	int ancNodeDataCurr = GETDATA(ancNode);
	if (ancNodeDataPrev != ancNodeDataCurr)
		return insertTree(ancNode, data);
	if (ISNULL(curr)) {
		Node *myNode = new Node(data, pred);
		int predData = GETDATA(pred);
		if (data > predData) {
			pred->child[RIGHT] = myNode; 
		}
		else if (data < predData)
			pred->child[LEFT] = myNode; 
	}
	else {
		int *currDataPtr = GETADDR(curr)->dataPtr;
		int currData = *(int *)((uintptr_t)currDataPtr & ~0x03);
		if (data == currData) {
			// Check for node marking. If yes, help and then return
			return false;
		}
		return false;
	}	
	
//	
//	else if (data > currData) {
//		Node *currPtr = GETADDR(curr)->child[RIGHT];
//		Node *myNode = new Node(data, curr);
//		if (CAS(&GETADDR(curr)->child[RIGHT], currPtr, UNQNULL, myNode, NORMAL)) {
//			return true;
//		}
//		else {
//			// CHeck for the reason of failue.	
//			currPtr = GETADDR(curr)->child[RIGHT];
//			if (STATUS(currPtr) == NORMAL)
//				return insertTree(pred, data);
//			else if (STATUS(currPtr) == MARKED) {
//				// Mark, help remove curr and then insert in your style
//				return false;
//			}
//			else if (STATUS(currPtr) == UNQNULL)
//				return insertTree(pred, data);
///*			else if (STATUS(currPtr) == PROMOTE) {
//				// Help Swap data and then return or insert node..
//			} */
//		}
//	}
//	else if (data < currData) {
//		Node *currPtr = GETADDR(curr)->child[LEFT];
//		Node *myNode = new Node(data, curr);
//		if (CAS(&GETADDR(curr)->child[LEFT], currPtr, UNQNULL, myNode, NORMAL)) {
//			return true;
//		}
//		else {
//			if (STATUS(currPtr) == MARKED) {
//				// Mark, help remove curr and then insert in your style
//				return false;
//			}
//			return insertTree(pred, data);
//		}
//	}
}

void printTree(Node *node) {
	if (ISNULL(node)) 
		return;
	printTree(GETADDR(node)->child[LEFT]);
	//std::cout<<GETADDR(node)<<std::endl;
	if (GETADDR(node) != root) {
	countPrint++;
	std::cout<<"[VARUN] "<<GETDATA(node)<<"  "<<STATUS(GETADDR(node)->child[RIGHT])<<" "<<STATUS(GETADDR(node)->child[LEFT])<<std::endl;
//	if ((STATUS(GETADDR(node)->child[RIGHT]) ) !=  (STATUS(GETADDR(node)->child[LEFT])))
//		std::cout<<"ERROR"<<std::endl;
	}
	printTree(GETADDR(node)->child[RIGHT]);
}

void testbenchSequential() {
	srand(time(NULL));
	for (int i = 0; i < 10; i++)
		insertTree(root, rand());
	printTree(root->child[LEFT]);
}

void testbenchParallel() {
	const int numThreads = 100;
	count.store(0);
	srand(time(NULL));
	std::vector<std::thread> addT(numThreads);
	std::vector<std::thread> removeT(numThreads);
	int arr[numThreads];
	for (int i = 0; i < numThreads; i++) {
		do {
			arr[i] = rand();
		} while(arr[i] == INT_MAX);
	}
//	for (int i = 0; i < numThreads; i++)
//		addT[i] = std::thread(insertTree, root, arr[i]);
//	for (int i = 0; i < numThreads; i++)
//		addT[i].join();	
	for (int i = 0; i <numThreads; i++)
		insertTree(root, arr[i]);
//	printTree(root);
	for (int i = 0; i < numThreads; i++)
		std::cout<<arr[i]<<std::endl;
	std::cout<<"Removing Elements..."<<std::endl;
//	for (int i = 0; i < numThreads; i++)
//		removeTree(root->child[LEFT], root, arr[i]);
	for (int i = 0; i < numThreads; i++)
		removeT[i] = std::thread(removeTree, root->child[LEFT], root, arr[i]);
	for (int i = 0; i < numThreads; i++)
		removeT[i].join();	
	std::cout<<"Printing Removed Elements..."<<std::endl;
	printTree(root);
	if ((countPrint) > count.load())
		std::cout<<"[VARUN] This is an error"<<std::endl;
	std::cout<<count.load()<<" "<<(countPrint )<<std::endl;
	
}

int main(void) {
	//testbenchSequential();
	testbenchParallel();
	return 0;
}
