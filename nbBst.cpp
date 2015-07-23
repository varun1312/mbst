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

#define DEBUG_MSG	std::cout<<"[SIES] "<<__LINE__<<" "<<GETDATA(curr)<<"  "<<STATUS(GETADDR(curr)->child[RIGHT])<<" "<<STATUS(GETADDR(curr)->child[LEFT])<<std::endl;
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

seekNode* seekTree(Node *startNode, int data) {
	Node *curr = GETADDR(startNode);
	Node *pred = curr->bl;
	Node *ancNode = pred;
	int ancNodeData = GETDATA(ancNode);
	while(true) {
		int currData = GETDATA(curr);
		if (data > currData) {
			if (ISNULL(GETADDR(curr)->child[RIGHT])) 
				break;
			pred = (curr), curr = GETADDR(curr->child[RIGHT]);
		}
		else if (data < currData) {
			if (ISNULL(GETADDR(curr)->child[LEFT]))
				break;
			ancNode = pred, ancNodeData = GETDATA(ancNode), pred = (curr), curr= GETADDR(curr->child[LEFT]);
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
		if (STATUS(rp) == UNQNULL) {
			if (CAS(&(curr->child[RIGHT]), rp, UNQNULL, NULL, MARKED)) {
				markLeft(curr);
				break;
			}
		}
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
	int predData = GETDATA(pred), status = NORMAL;
	Node *ptr, *rp, *lp;
	rp = curr->child[RIGHT];
	lp = curr->child[LEFT];
	if ((ISNULL(rp)) && (ISNULL(lp))) {
		if (STATUS(pred->dataPtr) == MARKED)
			ptr = root, status = UNQNULL;
		ptr = curr, status = UNQNULL;
	}
	else if (ISNULL(rp))
		ptr = GETADDR(lp), status = NORMAL;
	else
		ptr = GETADDR(rp), status = NORMAL;
		
	if (data > predData) {
		if (CAS(&(pred->child[RIGHT]), curr, NORMAL, ptr, status)) {
			if (ptr != root && ptr != NULL)
				ptr->bl = pred;
			return true;
		}
		else {
			Node *predPtr = pred->child[RIGHT];
			if ((STATUS(predPtr) == MARKED) && (GETADDR(predPtr) == curr)) {
				// This means that pred can either be deleted from tree or it is present and marked
				Node *predBl = pred->bl;
				int predBlData = GETDATA(predBl);
				if (predData > predBlData) {
					if (GETADDR(predBl->child[RIGHT]) == pred) {
						markLeft(pred);
						removeTreeNode(predBl, pred, predData);
					}
				}
				else {
					if (GETADDR(predBl->child[LEFT]) == pred) {
						markLeft(pred);
						removeTreeNode(predBl, pred, predData);
					}
				}
				return removeTreeNode(curr->bl, curr, data);
			}
			// Node is deleted from tree.
			return false;
		}
	}
	else {
		if (CAS(&(pred->child[LEFT]), curr, NORMAL, ptr, status)) {
			if (ptr != root && ptr != NULL)
				ptr->bl = pred;
			return true;
		}
		else {
			Node *predPtr = pred->child[LEFT];
			if ((STATUS(predPtr) == MARKED) && (GETADDR(predPtr) == curr)) {
				// This means that pred can either be deleted from tree or it is present and marked
				Node *predBl = pred->bl;
				int predBlData = GETDATA(predBl);
				if (predData > predBlData) {
					if (GETADDR(predBl->child[RIGHT]) == pred) {
						markRight(pred);
						removeTreeNode(predBl, pred, predData);
					}
				}
				else {
					if (GETADDR(predBl->child[LEFT]) == pred) {
						markRight(pred);
						removeTreeNode(predBl, pred, predData);
					}
				}
				return removeTreeNode(curr->bl, curr, data);
			}
			// Node is deleted from tree.
			return false;
		}
	}
	return true;
}

bool removeTreeNodeTwoChild(Node *pred, Node *curr, int *currDataPtr, Node *ancNode, int *ancNodeDataPtr, int data) {
	Node *rp = curr->child[RIGHT];
	if (ISNULL(rp) && (GETADDR(rp) != root)) {
		return removeTree(pred, data);
	}
	else if (ISNULL(rp) && (GETADDR(rp) == root)) {
		// Seek...Handled later.
	}
	else if (STATUS(rp) == MARKED) {
		markLeft(curr);
		return removeTreeNode(pred, curr, data);
	}
	else if (STATUS(rp) == PROMOTE) {	
		// Removing a promote node.
		helpSwapData(currDataPtr, ancNode, ancNodeDataPtr);
		markLeft(curr);
		removeTreeNode(pred, curr, data);
		return removeTree(ancNode->bl, data);
	}
	if (STATUS(rp) == NORMAL) {
		Node *rPtr = GETADDR(rp);
		if ((GETADDR(rPtr->child[RIGHT]) == NULL) && (GETADDR(rPtr->child[LEFT]) == NULL) && (STATUS(rPtr->child[RIGHT]) == MARKED) && (STATUS(rPtr->child[LEFT]) == MARKED)) {
			DEBUG_MSG
			if (CAS(&curr->child[RIGHT], rPtr, NORMAL, NULL, MARKED)) {
				markLeft(curr);
				return removeTreeNode(pred, curr, data);
			}
			DEBUG_MSG
			return removeTreeNodeTwoChild(pred, curr, currDataPtr, ancNode, ancNodeDataPtr, data);
		}
	}
	DEBUG_MSG
	Node *lp = curr->child[LEFT];
	if (ISNULL(lp) || (STATUS(lp) == MARKED)) 
		return removeTree(pred, data);
	seekNode *succSeek = seekTree(GETADDR(lp), data);
	Node *succPred = succSeek->pred;
	Node *succ = succSeek->curr;
	int *succDataPtr = succ->dataPtr;
	int succData = *(int *)((uintptr_t)succDataPtr & ~0x03);
	if (data != GETDATA(curr))
		return removeTree(ancNode->bl, data);
	Node *succRight = succ->child[RIGHT];
	if ((GETADDR(succRight) == root) && (STATUS(succDataPtr) == MARKED)) {
		removeTreeNodeTwoChild(succPred, succ, succDataPtr, curr, currDataPtr, succData);
		return removeTreeNodeTwoChild(pred, curr, currDataPtr, ancNode, ancNodeDataPtr, data);
	}
	else if (CAS(&(succ->child[RIGHT]), succRight, UNQNULL, NULL, PROMOTE)) {
		helpSwapData(succDataPtr, curr, currDataPtr);
		markLeft(succ);
		return removeTreeNode(succPred, succ, succData);
	}
	succRight = succ->child[RIGHT];
	if (STATUS(succRight) == PROMOTE) {
		helpSwapData(succDataPtr, curr, currDataPtr);
		markLeft(succ);
		return removeTreeNode(succPred, succ, succData);
	}
	else if (STATUS(succRight) == MARKED) {
		markLeft(succ);
		removeTreeNode(succPred, succ, succData);
		return removeTreeNodeTwoChild(pred, curr, currDataPtr, ancNode, ancNodeDataPtr, data);
	}
	else if ((STATUS(succRight) == NORMAL) || (STATUS(succRight) == UNQNULL))
		return removeTreeNodeTwoChild(pred, curr, currDataPtr, ancNode, ancNodeDataPtr, data);
	std::cout<<"[SIES] : "<<data<<" "<<STATUS(succRight)<<std::endl;
	return true;
}

bool removeTree(Node *startNode, int data) {
	seekNode *succSeek = seekTree(startNode, data);
	Node *ancNode = succSeek->ancNode;
	Node *pred = succSeek->pred;
	Node *curr = succSeek->curr;
	int *ancNodeDataPtr = ancNode->dataPtr;
	int ancNodeDataPrev = succSeek->ancNodeData;
	int ancNodeDataCurr = GETDATA(ancNode);
	if (ancNodeDataPrev != ancNodeDataCurr)
		return removeTree(ancNode->bl, data);
	if (ISNULL(curr))
		return false;
	int *currDataPtr = curr->dataPtr;
	int currData = *(int *)((uintptr_t)currDataPtr & ~0x03);
	if (data == currData) {
		//std::cout<<data<<" "<<STATUS(curr->child[RIGHT])<<" "<<STATUS(curr->child[LEFT])<<" "<<std::endl;
		markStatus_t markStat = markTreeNode(curr, currDataPtr);
		myMutex.lock();
		std::cout<<data<<" : "<<markStat<<std::endl;	
		myMutex.unlock();
		if ((markStat == REMOVE_0C) || (markStat == REMOVE_1C))
			return removeTreeNode(pred, curr, data);
		else if (markStat == REMOVE_2C) 
			return removeTreeNodeTwoChild(pred, curr, currDataPtr, ancNode, ancNodeDataPtr, data);
		else if (markStat == REMOVE_ANCNODE) {
			helpSwapData(currDataPtr, ancNode, ancNodeDataPtr);
			removeTreeNode(pred, curr, data);
			return removeTree(root, data);
		}
	}
	return true;	
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
/*			else if (STATUS(currPtr) == PROMOTE) {
				// Help Swap data and then return or insert node..
			} */
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
	if (ISNULL(node)) 
		return;
	printTree(GETADDR(node)->child[LEFT]);
	//std::cout<<GETADDR(node)<<std::endl;
	countPrint++;
	std::cout<<"[VARUN] "<<GETDATA(node)<<"  "<<STATUS(GETADDR(node)->child[RIGHT])<<" "<<STATUS(GETADDR(node)->child[LEFT])<<std::endl;
//	if ((STATUS(GETADDR(node)->child[RIGHT]) ) !=  (STATUS(GETADDR(node)->child[LEFT])))
//		std::cout<<"ERROR"<<std::endl;
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
	for (int i = 0; i < numThreads; i++)
		arr[i] = rand();
	for (int i = 0; i < numThreads; i++)
		addT[i] = std::thread(insertTree, root, arr[i]);
	for (int i = 0; i < numThreads; i++)
		addT[i].join();	
//	printTree(root->child[LEFT]);
	std::cout<<"Removing Elements..."<<std::endl;
	for (int i = 0; i < numThreads; i++)
		removeT[i] = std::thread(removeTree, root, arr[i]);
	for (int i = 0; i < numThreads; i++)
		removeT[i].join();	
	std::cout<<"Printing Removed Elements..."<<std::endl;
	printTree(root->child[LEFT]);
//	if ((countPrint) != count.load())
//		std::cout<<"[VARUN] This is an error"<<std::endl;
//	std::cout<<count.load()<<" "<<(countPrint )<<std::endl;
	
}

int main(void) {
	//testbenchSequential();
	testbenchParallel();
	return 0;
}
