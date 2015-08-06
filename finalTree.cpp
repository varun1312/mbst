#include <iostream>
#include <cstdio>
#include <climits>
#include <vector>
#include <thread>
#include <gsl/gsl_rng.h>
#include <gsl/gsl_randist.h>
#include <sys/time.h>
#include <signal.h>
#include <getopt.h>
#include <stdint.h>
#include <pthread.h>
#include <ctime>
#include <unistd.h>

const int L = 0, R = 1;
const int NORMAL = 0, MARKED = 1, PROMOTE = 2, UNQNULL = 3;

volatile bool start = false, stop = false, steadyState = false;

/* This code belongs to Synchrobench testbench */
#define DEFAULT_DURATION 2
#define DEFAULT_DATA_SIZE 256
#define DEFAULT_THREADS 1
#define DEFAULT_RANGE 0x7FFFFFFF
#define DEFAULT_SEED 0
#define DEFAULT_INSERT 20
#define DEFAULT_REMOVE 10
#define DEFAULT_SEARCH 70

int duration = DEFAULT_DURATION;
int dataSize = DEFAULT_DATA_SIZE;
int numThreads = DEFAULT_THREADS;
int range = DEFAULT_RANGE;
unsigned int seed = DEFAULT_SEED;
int insertPer = DEFAULT_INSERT;
int removePer = DEFAULT_REMOVE;
int searchPer = DEFAULT_SEARCH;
int initialSize = ((dataSize)/2);

typedef struct thread_data {
	int id;
	unsigned long seed;
	unsigned long readCount;
	unsigned long successfulReads;
	unsigned long unSuccessfulReads;
	unsigned long readRetries;
	unsigned long insertCount;
	unsigned long successfulInserts;
	unsigned long unSuccessfulInserts;
	unsigned long insertRetries;
	unsigned long deleteCount;
	unsigned long successfulDeletes;
	unsigned long unSuccessfulDeletes;
	unsigned long deleteRetries;
	unsigned long seekRetries;

} thread_data_t;

struct tArgs
{
	int tId;
	unsigned long lseed;
	unsigned long readCount;
	unsigned long successfulReads;
	unsigned long unsuccessfulReads;
	unsigned long readRetries;
	unsigned long insertCount;
	unsigned long successfulInserts;
	unsigned long unsuccessfulInserts;
	unsigned long insertRetries;
	unsigned long deleteCount;
	unsigned long successfulDeletes;
	unsigned long unsuccessfulDeletes;
	unsigned long deleteRetries;
	bool isNewNodeAvailable;
	unsigned long seekRetries;
	unsigned long seekLength;
};

enum markStatus_t {
	REMOVE_ZERO_ONE_CHILD,
	REMOVE_TWOCHILD,
	REMOVE_ANCNODE,
	LEFT_MARKED,
	RIGHT_MARKED,
	ABORT_REMOVE,
	RETRY
};

// Here all convenient macros are defined.
#define GETADDR(node) ((Node *)((uintptr_t)node & ~0x03))
#define STATUS(node) ((uintptr_t)node & 0x03)
#define GETDATA(node) (*(int *)((uintptr_t)(GETADDR(node)->dp) & ~0x03))
#define ISNULL(node) ((GETADDR(node) == NULL) || (STATUS(node) == UNQNULL))
#define ISUNQNULL(node) (STATUS(node) == UNQNULL)
#define MARKNODE(node, status) ((Node *)(((uintptr_t)node & ~0x03) | status))
#define CAS(ptr, source, sourceState, target, targetState) \
		__sync_bool_compare_and_swap(ptr, MARKNODE(source, sourceState), MARKNODE(target, targetState))

#define ISMARKED(node) ((STATUS(GETADDR(node)->ch[R]) == MARKED) || (STATUS(GETADDR(node)->ch[L]) == MARKED) || (STATUS(GETADDR(node)->dp) == MARKED))

struct Node {
	Node *bl;
	 int *dp;
	Node *ch[2];
	
	// This constructor is for the root node.
	Node(int data) {
		this->bl =this;
		this->dp = new int(data);
		this->ch[L] = (Node *)(UNQNULL);
		this->ch[R] = (Node *)(UNQNULL);
	}

	// This is for non root nodes.
	Node (int data, Node *bl) {
		this->bl = bl;
		this->dp = new int(data);
		this->ch[L] = (Node *)(UNQNULL);
		this->ch[R] = (Node *)(UNQNULL);
	}

};

// Decleration
bool removeTree(Node *, Node *, int);
bool insertTree(int);
// This is the node that is returned by the seekTree method.
struct seekNode {
	Node *ancNode;
	Node *pred;
	Node *curr;
	int ancNodeData;
	
	seekNode(Node *ancNode, Node *pred, Node *curr, int ancNodeData) {
		this->ancNode = ancNode;
		this->pred = pred;
		this->curr = curr;
		this->ancNodeData = ancNodeData;
	}
};

// This Node is the root of the tree.
Node *root = new Node(INT_MAX);

seekNode *seekTree(Node *startNode, Node *_ancNode, int data) {
	Node *curr = startNode;
	Node *pred = curr;
	Node *ancNode = _ancNode;
	int ancNodeData = GETDATA(_ancNode);
	while(true) {
		if (ISNULL(curr))
			break;
		else if ( (STATUS(curr) == PROMOTE) && (pred->ch[R] == GETADDR(curr)) )
			break;
		int currData = GETDATA(curr);
		if (data > currData) {
			pred = GETADDR(curr);
			curr = GETADDR(curr)->ch[R];
			if (ISNULL(curr) || (STATUS(curr) == PROMOTE)) {
				break;
			}
		}
		else if (data < currData) {
			ancNode = GETADDR(curr);
			ancNodeData = GETDATA(curr);
			pred = GETADDR(curr);
			curr = GETADDR(curr)->ch[L];
			if (ISNULL(curr)) {
				break;
			}
		}
		else if (data == currData) {
			break;
		}
	}
	seekNode *mySeek = new seekNode(ancNode, pred, curr, ancNodeData);
	return mySeek;
};

void printTreeRemove(Node *pred, Node *node) {
	if (ISNULL(node) || ((STATUS(node) == PROMOTE) && (GETADDR(pred->ch[R]) == GETADDR(node))))
		return;
	printTreeRemove(GETADDR(node), GETADDR(node)->ch[L]);
	//if (GETDATA(node) != (INT_MAX -1)) {
		std::cout<<"VARUN ";
		std::cout<<GETDATA(node)<<" "<<STATUS(GETADDR(node)->ch[L])<<" "<<STATUS(GETADDR(node)->ch[R])<<" "<<STATUS(GETADDR(node)->dp)<<std::endl;
		std::cout<<"ROOT : "<<root<<std::endl;
		std::cout<<"Pred : "<<pred<<" Curr : "<<node<<std::endl;
		std::cout<<"Curr Right : "<<GETADDR(node)->ch[R]<<" Curr Left : "<<GETADDR(node)->ch[L]<<std::endl;
		std::cout<<"Pred Right : "<<GETADDR(pred)->ch[R]<<" Pred Left : "<<GETADDR(pred)->ch[L]<<std::endl;
	//}
	printTreeRemove(GETADDR(node), GETADDR(node)->ch[R]);
}

void printTree(Node *pred, Node *node) {
	if (ISNULL(node) || ((STATUS(node) == PROMOTE) && (GETADDR(pred->ch[R]) == GETADDR(node))))
		return;
	printTree(GETADDR(node), GETADDR(node)->ch[L]);
	std::cout<<GETDATA(node)<<std::endl;
	printTree(GETADDR(node), GETADDR(node)->ch[R]);
}

bool insertTreeNode(Node *pred, Node *curr, int stat, int data) {
	int predData = GETDATA(pred);
	Node *myNode = new Node(data, pred);
	if (data > predData) {
		if (CAS(&(pred->ch[R]), curr, stat, myNode, NORMAL))
			return true;
		else {
			// CAS failed means pred child changed or pred 	
			// is marked. Both the scenarions are handled by
			// insert method. We need to restart the insert
			// from pred
			return insertTree(data);
		}
	}
	else { 
		if (CAS(&(pred->ch[L]), curr, stat, myNode, NORMAL))
			return true;
		else {
			// CAS failed means pred child changed or pred 	
			// is marked. Both the scenarions are handled by
			// insert method. We need to restart the insert
			// from pred
			return insertTree(data);
		}
	}
}


bool insertTree(int data) {
	seekNode *insertSeek = seekTree(root, root, data);
	Node *ancNode = insertSeek->ancNode;
	int ancNodeDataSeen = insertSeek->ancNodeData;
	Node *pred = insertSeek->pred;
	Node *curr = insertSeek->curr;
	if (ISNULL(curr)) {
		int ancNodeDataCurr = GETDATA(ancNode);
		if ( (ancNodeDataSeen) != (ancNodeDataCurr)) 
			return insertTree( data);
		else if ((ancNode != root) && GETADDR(ancNode->bl->ch[L]) != ancNode && GETADDR(ancNode->bl->ch[R]) != ancNode)
			return insertTree(data);

		int predData = GETDATA(pred);
		if (STATUS(curr) == PROMOTE)
			return insertTree(data);
		else if (STATUS(curr) == UNQNULL)
			return insertTreeNode(pred, GETADDR(curr), UNQNULL, data);
		else if (STATUS(curr) == MARKED) {
			removeTree(root, root, predData);
			return insertTree(data);
		}
	}
	else if (data == GETDATA(curr)) {
		// Here we need to check if curr is marked or not.
		if (ISMARKED(curr)) {
			removeTree(root, root, data);
			return insertTree(data);
		}
		return false;
	}
}

markStatus_t markLeft(Node *node) {
	Node *lp = node->ch[L];
	while(true) {
		int lpStatus = STATUS(lp);
		switch(lpStatus) {
			case MARKED:
				return LEFT_MARKED;
			case PROMOTE:
				return REMOVE_ANCNODE;
			case NORMAL:
				if (CAS(&(node->ch[L]), lp, NORMAL, lp, MARKED))
					return LEFT_MARKED;
				break;
			case UNQNULL:
				if (CAS(&(node->ch[L]), lp, UNQNULL, NULL, MARKED))
					return LEFT_MARKED;
				break;
		}
		lp = node->ch[L];
	}
}

markStatus_t markLeftPromote(Node *node) {
	Node *lp = node->ch[L];
	while(true) {
		int lpStatus = STATUS(lp);
		switch(lpStatus) {
			case MARKED:
				return REMOVE_ANCNODE;
			case PROMOTE:
				return LEFT_MARKED;
			case NORMAL:
				if (CAS(&(node->ch[L]), lp, NORMAL, lp, PROMOTE))
					return LEFT_MARKED;
				break;
			case UNQNULL:
				if (CAS(&(node->ch[L]), lp, UNQNULL, NULL, PROMOTE))
					return LEFT_MARKED;
				break;
		}
		lp = node->ch[L];
	}
}

markStatus_t markRight(Node *node) {
	Node *rp = node->ch[R];
	while(true) {
		int rpStatus = STATUS(rp);
		switch(rpStatus) {
			case MARKED:
				return RIGHT_MARKED;
			case PROMOTE:
				return REMOVE_ANCNODE;
			case NORMAL:
				if (CAS(&(node->ch[R]), rp, NORMAL, rp, MARKED))
					return RIGHT_MARKED;
				break;
			case UNQNULL:
				if (CAS(&(node->ch[R]), rp, UNQNULL, NULL, MARKED)) 
					return RIGHT_MARKED;
				break;
		}
		rp = node->ch[R];
	}
}


markStatus_t markTreeNode(Node *curr, Node *rp, Node *lp,  int *currDataPtr, int data) {
	//if (STATUS(lp) != NORMAL) {
	while (STATUS(lp) != NORMAL) {
		if (STATUS(lp) == PROMOTE)
			return REMOVE_ANCNODE;
		else if (STATUS(lp) == MARKED) {
			markStatus_t rightStatus = markRight(curr);
			if (rightStatus == REMOVE_ANCNODE)
				return REMOVE_ANCNODE;
			return REMOVE_ZERO_ONE_CHILD;
		}
		else if (STATUS(lp) == UNQNULL) {
			if (CAS(&(curr->ch[L]), lp, UNQNULL, NULL, MARKED)) {
				markStatus_t rightStatus = markRight(curr);
				if (rightStatus == REMOVE_ANCNODE)
					return REMOVE_ANCNODE;
				return REMOVE_ZERO_ONE_CHILD;	
			}
		}
		lp = curr->ch[L];
	}

	// Coming here means that either of the child pointers are not NORMAL.
	//if (ISNULL(rp) || (STATUS(rp) == PROMOTE)) {
	//if (STATUS(rp) != NORMAL) {
	while (STATUS(rp) != NORMAL) {
		if (STATUS(rp) == MARKED) {
			markLeft(curr);
			return REMOVE_ZERO_ONE_CHILD;
		}
		else if (STATUS(rp) == PROMOTE) {
			markStatus_t lpStatus = markLeft(curr);
			return REMOVE_ANCNODE;
		}
		else if (STATUS(rp) == UNQNULL) {
			if ((GETADDR(rp) == root) && (STATUS(curr->dp) == MARKED) && (data == GETDATA(curr))) {
					return REMOVE_TWOCHILD;
			}
			else {
				if (CAS(&(curr->ch[R]), rp, UNQNULL, NULL, MARKED)) {
					markLeft(curr);
					return REMOVE_ZERO_ONE_CHILD;
				}
			}
		}
		rp = curr->ch[R];
	}
	if ((STATUS(rp) == NORMAL) && (STATUS(lp) == NORMAL)) {
		int currData = *(int *)((uintptr_t)currDataPtr & ~0x03);
		if (STATUS(currDataPtr) == NORMAL) {
			if (CAS(&(curr->dp), currDataPtr, NORMAL, currDataPtr, MARKED)) {
				return REMOVE_TWOCHILD;
			}
			else {
				// CAS failed.
				currDataPtr = curr->dp;
				int currData = *(int *)((uintptr_t)currDataPtr & ~0x03);
				if ((STATUS(currDataPtr) == MARKED) && (data == currData))
					return REMOVE_TWOCHILD;
				else
					return ABORT_REMOVE;
			}
		}
		else if ((STATUS(currDataPtr) == MARKED) && (data == currData))
			return REMOVE_TWOCHILD;
		else
			return ABORT_REMOVE;
	}	
	else if ((STATUS(rp) == MARKED) && (STATUS(lp) == MARKED))
		return REMOVE_ZERO_ONE_CHILD;
	else if ((STATUS(rp) == PROMOTE) && (STATUS(lp) == PROMOTE))
		return REMOVE_ANCNODE;
	else if ((STATUS(rp) == PROMOTE) && (STATUS(lp) == MARKED))
		return REMOVE_ZERO_ONE_CHILD;
}

bool removeTreeNodeZeroOneChild(Node *pred, Node *curr, int data) {
	Node *ptr; int status;
	Node *rp, *lp;
	rp = curr->ch[R];
	lp = curr->ch[L];
	int predData = GETDATA(pred);
	if ((STATUS(rp) == MARKED) && (STATUS(lp) == MARKED)) {
		if ((GETADDR(rp) == NULL) && (GETADDR(lp) == NULL))	{
			if (STATUS(pred->dp) == MARKED)
				ptr = root, status = UNQNULL;
			else
				ptr = curr, status = UNQNULL;
		}
		else if (GETADDR(lp) == NULL) {
			ptr = GETADDR(rp), status = NORMAL;
		}
		else if (GETADDR(rp) == NULL) {
			ptr = GETADDR(lp), status = NORMAL;
		}
		else {
			std::cout<<"This is an error "<<__LINE__<<std::endl;
			exit(1);
		}
	}
	else if ((STATUS(rp) == PROMOTE) && (STATUS(lp) == PROMOTE)) {
		if (GETADDR(lp) == NULL)
			ptr = curr, status = UNQNULL;
		else
			ptr = GETADDR(lp), status = NORMAL;
	}
	else if ((STATUS(rp) == PROMOTE) && (STATUS(lp) == MARKED)) {
		if (GETADDR(lp) == NULL)
			ptr = curr, status = UNQNULL;
		else
			ptr = GETADDR(lp), status = NORMAL;
	}
	else {
		std::cout<<"This is an error "<<__LINE__<<std::endl;
		exit(1);
	}

	if (GETADDR(pred->ch[R]) == curr) {
		if (STATUS(pred->ch[R]) == NORMAL) {
			if (CAS(&(pred->ch[R]), curr, NORMAL, ptr, status)) {
				if (ptr != root && ptr != NULL )
					ptr->bl = pred;
				return true;
			}	
		}
		Node *predRight = pred->ch[R];
		if (STATUS(predRight) == UNQNULL) {
			return true;
		}
		else if (STATUS(predRight) == MARKED) {
			int predData = GETDATA(pred);
			if (GETADDR(pred->bl->ch[L]) == pred || GETADDR(pred->bl->ch[R]) == pred)
				removeTreeNodeZeroOneChild(pred->bl, pred, predData);
			else
				curr->bl = pred->bl;
			return removeTreeNodeZeroOneChild(curr->bl, curr, data);
		}
		else if (STATUS(predRight) == NORMAL) {
			if (GETADDR(predRight) == curr) {
				std::cout<<"This is an error "<<__LINE__<<std::endl;
				exit(1);
			}
			return true;		
		}
//		else if (STATUS(predRight) == PROMOTE) {
//			std::cout<<"This is an error "<<__LINE__<<std::endl;
//			exit(1);
//		}
	}
	else if (GETADDR(pred->ch[L]) == curr) {
		if (STATUS(pred->ch[L]) == NORMAL) {
			if (CAS(&(pred->ch[L]), curr, NORMAL, ptr, status)) {
				if (ptr != root && ptr != NULL)
					ptr->bl = pred;
				return true;
			}
		}
		Node *predLeft = pred->ch[L];
		if (STATUS(predLeft) == UNQNULL) {
			return true;
		}
		else if (STATUS(predLeft) == PROMOTE) {
			if (GETADDR(pred->bl->ch[L]) == pred || GETADDR(pred->bl->ch[R]) == pred) 
				removeTreeNodeZeroOneChild(pred->bl, pred, predData);
			else
				curr->bl = pred->bl;
			return removeTreeNodeZeroOneChild(curr->bl, curr, data);
		}
		else if (STATUS(predLeft) == MARKED) {
			int predData = GETDATA(pred);
			if (GETADDR(pred->bl->ch[L]) == pred || GETADDR(pred->bl->ch[R]) == pred)
				removeTreeNodeZeroOneChild(pred->bl, pred, predData);
			else
				curr->bl = pred->bl;
			return removeTreeNodeZeroOneChild(pred->bl, curr, data);
		}
		else if (STATUS(predLeft) == NORMAL) {
			if (GETADDR(predLeft) == curr) {
				std::cout<<"This is an error "<<__LINE__<<std::endl;
				exit(1);
			}
			return true;		
		}
	}
	return false;
}

bool removeTreeNodeTwoChild(Node *pred, Node *curr,  int *currDataPtr, Node *ancNode, int data) {
	if (GETADDR(pred) == GETADDR(curr))
		return false;
	Node *rp = curr->ch[R];
	Node *lp = curr->ch[L];
	if (STATUS(lp) == NORMAL) {
		if ((STATUS(rp) == NORMAL) || ((GETADDR(rp) == root) && (STATUS(currDataPtr) == MARKED))) {
			if ((GETADDR(rp->ch[R]) == NULL) && (STATUS(rp->ch[R]) == MARKED) && (GETADDR(rp->ch[L]) == NULL) && (STATUS(rp->ch[L]) == MARKED)) {
				if (CAS(&(curr->ch[R]), rp, NORMAL, root, UNQNULL)) {
				}
				else {
					return removeTree(root, root, data);
				}
			}
			Node *succPred = pred;
			Node *succ = curr;
			Node *succRight = lp;
			while(true) {
				if ((ISNULL(succRight)) || (STATUS(succRight) == PROMOTE)) { 
					break;
				}
				succPred = succ;
				succ = GETADDR(succRight);
				succRight = GETADDR(succRight)->ch[R];
			}
			int *succDataPtr = succ->dp;
			int	succData = *(int *)((uintptr_t)succDataPtr & ~0x03);
			if (GETADDR(currDataPtr) != GETADDR(curr->dp))
				return false;
			if (STATUS(succRight) == MARKED) {
				markLeft(succ);
				removeTreeNodeZeroOneChild(succPred, succ, succData);
				return removeTreeNodeTwoChild(pred, curr, currDataPtr, ancNode, data);
			}
			else if (STATUS(succRight) == PROMOTE) {
				if (GETADDR(succRight) == curr && data != succData) {
					if (CAS(&(curr->dp), currDataPtr, MARKED, succDataPtr, NORMAL)) {
						// CAS succeeded
						markStatus_t ls = markLeftPromote(succ);
						if (ls == LEFT_MARKED) {
							return removeTreeNodeZeroOneChild(succPred, succ, succData);
						}
						else {
							removeTreeNodeZeroOneChild(succPred, succ, succData);
							return removeTree(root, root , succData);
						}
					}
					else {
						markLeft(succ);
						removeTreeNodeZeroOneChild(succPred, succ, succData);
						return false;
					}
				}	
				else {
					markLeft(succ);
					removeTreeNodeZeroOneChild(succPred, succ, succData);
					return removeTree(root, root, data);
				}
			}
			else if (STATUS(succRight) == UNQNULL) {
				if ((GETADDR(succRight) == root) && (STATUS(succDataPtr) == MARKED)) {
					removeTreeNodeTwoChild(succ->bl, succ, succDataPtr, curr, succData);
					return removeTreeNodeTwoChild(pred, curr, currDataPtr, ancNode, data);
				}
				else if (CAS(&(succ->ch[R]), succRight, UNQNULL, curr, PROMOTE)) {
					if (CAS(&(curr->dp), currDataPtr, MARKED, succDataPtr, NORMAL)) {
						markStatus_t ls = markLeftPromote(succ);
						if (ls == LEFT_MARKED) {
							return removeTreeNodeZeroOneChild(succPred, succ, succData);
						}
						else {
							removeTreeNodeZeroOneChild(succPred, succ, succData);
							return removeTree(root, root, succData);
						}
					}
				}	
			}
			return removeTreeNodeTwoChild(pred, curr, currDataPtr, ancNode, data);
		}
		else
			return removeTree(root, root, data);
	}
	else 
		return removeTree(root, root, data);
}

bool removeTree(Node *startNode, Node* _ancNode, int data) {
	//std::thread::id tid = std::this_thread::get_id();
	//std::cout<<"Removing : "<<data<<" Thread ID : "<<tid<<std::endl;
	seekNode *removeSeek = seekTree(startNode, startNode->bl, data);
	Node *pred = removeSeek->pred;
	Node *curr = removeSeek->curr;
	Node *ancNode = removeSeek->ancNode;
	int ancNodeDataSeen = removeSeek->ancNodeData;
	int *ancNodeDataPtr = ancNode->dp;
	int ancNodeDataCurr = *(int *)((uintptr_t)ancNodeDataPtr & ~0x03);
	if (ISNULL(curr) || ((STATUS(curr) == PROMOTE) && (GETADDR(pred->ch[R]) == GETADDR(curr)))) {
		if ( (ancNodeDataSeen) != (ancNodeDataCurr)) 
			return removeTree(root, root, data);
		else if (ancNode != root && GETADDR(ancNode->bl->ch[L]) != ancNode && GETADDR(ancNode->bl->ch[R]) != ancNode)
			return removeTree(root, root, data);
		return false;
	}
	curr = GETADDR(curr);
	 int *currDataPtr = curr->dp;
	int currData = *(int *)((uintptr_t)currDataPtr & ~0x03);
	Node *rp = curr->ch[R];
	Node *lp = curr->ch[L];
	if (data == currData) {
		markStatus_t markStat = markTreeNode(curr, rp, lp, currDataPtr, data);
		//std::cout<<data<<" "<<markStat<<" "<<__LINE__<<std::endl;
		rp = curr->ch[R];
		lp = curr->ch[L];
		currDataPtr = curr->dp;
		if (markStat == REMOVE_ZERO_ONE_CHILD) {
			return removeTreeNodeZeroOneChild(pred, curr, data);
		}
		else if (markStat == REMOVE_TWOCHILD) {
			return removeTreeNodeTwoChild(pred, curr, currDataPtr, ancNode, data);
		}
		else if (markStat == REMOVE_ANCNODE) {
			if (GETADDR(curr->ch[R]) == ancNode && STATUS(ancNodeDataPtr) == MARKED && STATUS(ancNode->ch[L]) == NORMAL) {
				//std::cout<<data<<" "<<__LINE__<<std::endl;
				CAS(&(ancNode->dp), ancNodeDataPtr, MARKED, currDataPtr, MARKED);
				removeTreeNodeZeroOneChild(pred, curr, data);
				return removeTree(root, root, data);
			}
			else {
				return removeTree(root, root, data);
			}
		}
		return true;
	}
	else
		return removeTree(root, root, data);
}


bool searchTree(int data) {
	seekNode *searchSeek = seekTree(root, root, data);
	Node *pred = searchSeek->pred;
	Node *curr = searchSeek->curr;
	Node *ancNode = searchSeek->ancNode;
	int ancNodeDataSeen = searchSeek->ancNodeData;
	int *ancNodeDataPtr = ancNode->dp;
	int ancNodeDataCurr = *(int *)((uintptr_t)ancNodeDataPtr & ~0x03);
	if (ISNULL(curr) || ((STATUS(curr) == PROMOTE) && (GETADDR(pred->ch[R]) == GETADDR(curr)))) {
		if ( (ancNodeDataSeen) != (ancNodeDataCurr)) 
			return searchTree(data);
		else if (ancNode != root && GETADDR(ancNode->bl->ch[L]) != ancNode && GETADDR(ancNode->bl->ch[R]) != ancNode)
			return searchTree(data);
		return false;
	}
	else if (data == GETDATA(curr) && !ISMARKED(curr)) {
		return true;
	}
	return false;
}

int isValidTree(Node *node) {
	int k = GETDATA(node);
	Node *lp = GETADDR(node)->ch[L];
	Node *rp = GETADDR(node)->ch[R];
	if (!ISNULL(lp)) {
		int lK = isValidTree(GETADDR(lp));
		if (lK >= k) {
			std::cout<<"Error : Sanity Check failed : "<<__LINE__<<std::endl;
			exit(1);
		}
	}
	if (!ISNULL(rp)) {
		int rK = isValidTree(GETADDR(rp));
		if (rK < k) {
			std::cout<<"Error : Sanity Check failed : "<<__LINE__<<std::endl;
			exit(1);
		}
	}
	return k;
}

void testbenchParallelRemove() {
	const int numThreads = 1000;
	int arr[numThreads];
	srand(time(NULL));
	std::vector<std::thread> insertThreads(numThreads);
	std::vector<std::thread> removeThreads(numThreads);
	for (int i = 0; i < numThreads; i++) {
		do {
			arr[i] = rand();
		} while(arr[i] == INT_MAX );
	}
//	for (int i = 0; i < numThreads; i++) {
//	//	std::cout<<"Array is : "<<arr[i]<<std::endl;
//		insertTree(arr[i]);
//	}
	for (int i = 0; i < numThreads; i++) {
		insertThreads[i] = std::thread(&insertTree, arr[i]);
	}
	for (int i = 0; i < numThreads; i++) {
		insertThreads[i].join();
	}
	for (int i = 0; i < numThreads; i++) {
		if (!searchTree(arr[i]))
			std::cout<<"ERROR"<<std::endl;
	}
	//printTree(root, root->ch[L]);
	std::cout<<"Removing Elements..."<<std::endl;
	for (int i = 0; i < numThreads; i++) {
		removeThreads[i] = std::thread(&removeTree, root, root, arr[i]);
	}
	for (int i = 0; i < numThreads; i++) {
		removeThreads[i].join();
	}
	std::cout<<"Printing after removing elements"<<std::endl;
	printTreeRemove(root, root->ch[L]);
	
}



void *test(void *_data) {
	thread_data_t *tData = (thread_data_t *)_data;
	int lseed;
	int chooseOperation, key;
	const gsl_rng_type* T;
	gsl_rng* r;
	gsl_rng_env_setup();
	T = gsl_rng_default;
	r = gsl_rng_alloc(T);
	lseed = tData->seed;
	gsl_rng_set(r,lseed);
	while(!start){}
	while(!steadyState) {
		chooseOperation = gsl_rng_uniform(r)*100;
		key = gsl_rng_uniform_int(r, range) ;
		if (chooseOperation < searchPer) {
			searchTree( key);
		}
		else if ((chooseOperation < insertPer)) {
			insertTree(key);
		}
		else if (chooseOperation < removePer) {
			removeTree(root, root, key);
		}
	}
	tData->readCount = 0;
	tData->insertCount = 0;
	tData->deleteCount = 0;
	std::cout<<"Starting : "<<stop<<std::endl;
	while(!stop) {
    	chooseOperation = gsl_rng_uniform(r)*100;
		key = gsl_rng_uniform_int(r, range) ;
		if (chooseOperation < searchPer) {
			tData->readCount++;
			searchTree(key);
		}
		else if ((chooseOperation < insertPer)) {
			tData->insertCount++;
			insertTree(key);
		}
		else if (chooseOperation < removePer) {
			tData->deleteCount++;
			removeTree(root, root, key);
		}
	}
	return NULL;
} 

void *operateOnTree(void* tArgs)
{
  int chooseOperation;
  unsigned long lseed;
	unsigned long key;
  struct tArgs* tData = (struct tArgs*) tArgs;
  const gsl_rng_type* T;
  gsl_rng* r;
  gsl_rng_env_setup();
  T = gsl_rng_default;
  r = gsl_rng_alloc(T);
	lseed = tData->lseed;
  gsl_rng_set(r,lseed);

  tData->isNewNodeAvailable=false;
	tData->readCount=0;
  tData->successfulReads=0;
  tData->unsuccessfulReads=0;
  tData->readRetries=0;
  tData->insertCount=0;
  tData->successfulInserts=0;
  tData->unsuccessfulInserts=0;
  tData->insertRetries=0;
  tData->deleteCount=0;
  tData->successfulDeletes=0;
  tData->unsuccessfulDeletes=0;
  tData->deleteRetries=0;
	tData->seekRetries=0;
	tData->seekLength=0;

  while(!start)
  {
  }
	while(!steadyState)
	{
	  chooseOperation = gsl_rng_uniform(r)*100;
		key = gsl_rng_uniform_int(r,range) + 2;
    if(chooseOperation < searchPer)
    {
      searchTree(key);
    }
    else if (chooseOperation < insertPer)
    {
      insertTree(key);
    }
    else
    {
      removeTree(root, root, key);
    }
	}
	
  tData->readCount=0;
  tData->successfulReads=0;
  tData->unsuccessfulReads=0;
  tData->readRetries=0;
  tData->insertCount=0;
  tData->successfulInserts=0;
  tData->unsuccessfulInserts=0;
  tData->insertRetries=0;
  tData->deleteCount=0;
  tData->successfulDeletes=0;
  tData->unsuccessfulDeletes=0;
  tData->deleteRetries=0;
	tData->seekRetries=0;
	tData->seekLength=0;
	
	while(!stop)
  {
    chooseOperation = gsl_rng_uniform(r)*100;
		key = gsl_rng_uniform_int(r,range) + 2;

    if(chooseOperation < searchPer)
    {
			tData->readCount++;
      searchTree(key);
    }
    else if (chooseOperation < insertPer)
    {
			tData->insertCount++;
      insertTree( key);
    }
    else
    {
			tData->deleteCount++;
      removeTree(root, root, key);
    }
  }
  return NULL;
}
struct tArgs** tArgs;

int main(int argc, char *argv[])
{
	struct timespec runTime,transientTime;
	unsigned long lseed;
	//get run configuration from command line
  numThreads = atoi(argv[1]);
  searchPer = atoi(argv[2]);
  insertPer = searchPer + atoi(argv[3]);
  removePer = insertPer + atoi(argv[4]);

	runTime.tv_sec = atoi(argv[5]);
	runTime.tv_nsec =0;
	transientTime.tv_sec=0;
	transientTime.tv_nsec=2000000;

  range = (unsigned long) atol(argv[6])-1;
	lseed = (unsigned long) atol(argv[7]);

  tArgs = (struct tArgs**) malloc(numThreads * sizeof(struct tArgs*)); 

  const gsl_rng_type* T;
  gsl_rng* r;
  gsl_rng_env_setup();
  T = gsl_rng_default;
  r = gsl_rng_alloc(T);
  gsl_rng_set(r,lseed);
	
  
  struct tArgs* initialInsertArgs = (struct tArgs*) malloc(sizeof(struct tArgs));
  initialInsertArgs->successfulInserts=0;
  initialInsertArgs->isNewNodeAvailable=false;
	int i = 0;	
  while(i < range/2) //populate the tree with 50% of keys
  {
    if (insertTree(gsl_rng_uniform_int(r,range) ))
		i++;
  }
  pthread_t threadArray[numThreads];
  for(int i=0;i<numThreads;i++)
  {
    tArgs[i] = (struct tArgs*) malloc(sizeof(struct tArgs));
    tArgs[i]->tId = i;
    tArgs[i]->lseed = gsl_rng_get(r);
  }

	for(int i=0;i<numThreads;i++)
	{
		pthread_create(&threadArray[i], NULL, operateOnTree, (void*) tArgs[i] );
	}
	
	start=true; 										//start operations
	//nanosleep(&transientTime,NULL); //warmup
	sleep(.01);
	steadyState=true;
	nanosleep(&runTime,NULL);
	stop=true;											//stop operations
	
	for(int i=0;i<numThreads;i++)
	{
		pthread_join(threadArray[i], NULL);
	}	

  unsigned long totalReadCount=0;
  unsigned long totalSuccessfulReads=0;
  unsigned long totalUnsuccessfulReads=0;
  unsigned long totalReadRetries=0;
  unsigned long totalInsertCount=0;
  unsigned long totalSuccessfulInserts=0;
  unsigned long totalUnsuccessfulInserts=0;
  unsigned long totalInsertRetries=0;
  unsigned long totalDeleteCount=0;
  unsigned long totalSuccessfulDeletes=0;
  unsigned long totalUnsuccessfulDeletes=0;
  unsigned long totalDeleteRetries=0;
	unsigned long totalSeekRetries=0;
	unsigned long totalSeekLength=0;
 
  for(int i=0;i<numThreads;i++)
  {
    totalReadCount += tArgs[i]->readCount;
    totalSuccessfulReads += tArgs[i]->successfulReads;
    totalUnsuccessfulReads += tArgs[i]->unsuccessfulReads;
    totalReadRetries += tArgs[i]->readRetries;

    totalInsertCount += tArgs[i]->insertCount;
    totalSuccessfulInserts += tArgs[i]->successfulInserts;
    totalUnsuccessfulInserts += tArgs[i]->unsuccessfulInserts;
    totalInsertRetries += tArgs[i]->insertRetries;
    totalDeleteCount += tArgs[i]->deleteCount;
    totalSuccessfulDeletes += tArgs[i]->successfulDeletes;
    totalUnsuccessfulDeletes += tArgs[i]->unsuccessfulDeletes;
    totalDeleteRetries += tArgs[i]->deleteRetries;
		totalSeekRetries += tArgs[i]->seekRetries;
		totalSeekLength += tArgs[i]->seekLength;
  }
	unsigned long totalOperations = totalReadCount + totalInsertCount + totalDeleteCount;
	double MOPS = totalOperations/(runTime.tv_sec*1000000.0);
	printf("k%d;%d-%d-%d;%d;%ld;%ld;%ld;%ld;%ld;%ld;%ld;%.2f;%.2f\n",atoi(argv[6]),searchPer,(insertPer-searchPer),(removePer-insertPer),numThreads,totalReadCount,totalInsertCount,totalDeleteCount,totalReadRetries,totalSeekRetries,totalInsertRetries,totalDeleteRetries,totalSeekLength*1.0/totalOperations,MOPS);
	(isValidTree(root));
	pthread_exit(NULL);
}

/*
int main(int argc, char **argv) {
	struct option long_options[] = {
		{"help", 				no_argument, 		NULL, 'h'},
		{"duration",			required_argument,	NULL, 'D'},
		{"Data Size",			required_argument,	NULL, 'I'},
		{"thread-num",			required_argument,	NULL, 'T'},
		{"range",				required_argument,	NULL, 'R'},
		{"Seed",				required_argument,	NULL, 'S'},
		{"Insert-Percentage",	required_argument,	NULL, 'i'},
		{"Remove-Percentage",	required_argument,	NULL, 'r'},
		{"Search-Percentage",	required_argument,	NULL, 's'}
	};

	thread_data_t *data;
	pthread_t *threads;
	struct timespec timeout;
 

	while(1) {
		int i = 0, c;
		c = getopt_long(argc, argv, "hD:I:T:R:S:i:r:s:", long_options, &i);
		if (c == -1)
			break;
		if (c ==0 && long_options[i].flag == 0)
			c = long_options[i].val;

		switch(c) {
			case 0 :
				break;
			case 'h':
				printf("Help Messge\n");
				exit(0);
			case 'D':
				duration = atoi(optarg);
				break;
			case 'I':
				dataSize = atoi(optarg);
				initialSize = dataSize/2;
				break;
			case 'T':
				numThreads = atoi(optarg);
				break;
			case 'R':
				range = atoi(optarg);
				break;
			case 'S':
				seed = atoi(optarg);
				break;
			case 'i':
			 	insertPer = atoi(optarg);
				break;
			case 'r':
				removePer = atoi(optarg);
				break;
			case 's':
				searchPer = atoi(optarg);
				break;
			default:
				std::cout<<"Use -h or --help for help"<<std::endl;
				exit(1);
		}
	}
	insertPer = searchPer + insertPer;
	removePer = insertPer + removePer;
	std::cout<<"Tree Type : Non-Blocking BST"<<std::endl;
	std::cout<<"Duration : "<<duration<<std::endl;
	std::cout<<"Data Size : "<<initialSize<<std::endl;
	std::cout<<"Number of Threads : "<<numThreads<<std::endl;
	std::cout<<"Range : "<<range<<std::endl;
	std::cout<<"Seed : "<<seed<<std::endl;
	std::cout<<"Insert percentage : "<<insertPer<<std::endl;
	std::cout<<"Remove Percentage : "<<removePer<<std::endl;
	std::cout<<"Search Percentage : "<<searchPer<<std::endl;

    timeout.tv_sec = duration;
	data = (thread_data_t *)malloc(numThreads * sizeof(thread_data_t));
	threads = (pthread_t *)malloc(numThreads * sizeof(pthread_t));

	if (seed == 0)
		srand((int)time(0));
	else
		srand(seed);

	std::cout<<"Pre Populating the tree with initial size of : "<<initialSize<<std::endl;
	int i = 0;
	const gsl_rng_type* T;
  gsl_rng* r;
  gsl_rng_env_setup();
  T = gsl_rng_default;
  r = gsl_rng_alloc(T);
  gsl_rng_set(r,seed);
	while(i < initialSize) {
		int val = gsl_rng_uniform_int(r, range); 
		if (insertTree(val)) {
		}
			i++;
	}
	
	for (int i = 0; i <numThreads; i++) {
		data[i].id = i;
		data[i].seed = seed;
		data[i].readCount = 0;
		data[i].successfulReads = 0;
		data[i].unSuccessfulReads = 0;
		data[i].readRetries = 0;
		data[i].insertCount = 0;
		data[i].successfulInserts = 0;
		data[i].unSuccessfulInserts = 0;
		data[i].insertRetries = 0;
		data[i].deleteCount = 0;
		data[i].successfulDeletes = 0;
		data[i].unSuccessfulDeletes = 0;
		data[i].deleteRetries = 0;
		data[i].seekRetries = 0;
		// Here the if condition of creating the pthread will come
		pthread_create(&threads[i], NULL, test, (void *)(&data[i]));
	}

	struct timespec transientTime;	
	transientTime.tv_sec=0;
	transientTime.tv_nsec=2000000;
	std::cout<<"STARTING..."<<std::endl;
	__atomic_store_n(&start, true, 0);
	nanosleep(&transientTime, NULL);
	__atomic_store_n(&steadyState, true, 0);
	nanosleep(&timeout, NULL);
	stop = true;
	std::cout<<"STOPPING..."<<std::endl;
	for (int i = 0; i < numThreads ; i++) {
		if(pthread_join(threads[i], NULL) != 0) {
			std::cout<<"Error waiting for thread joinig"<<std::endl;
			exit(0);
		}
	}

  unsigned long totalReadCount=0;
  unsigned long totalSuccessfulReads=0;
  unsigned long totalUnsuccessfulReads=0;
  unsigned long totalReadRetries=0;
  unsigned long totalInsertCount=0;
  unsigned long totalSuccessfulInserts=0;
  unsigned long totalUnsuccessfulInserts=0;
  unsigned long totalInsertRetries=0;
  unsigned long totalDeleteCount=0;
  unsigned long totalSuccessfulDeletes=0;
  unsigned long totalUnsuccessfulDeletes=0;
  unsigned long totalDeleteRetries=0;
	unsigned long totalSeekRetries=0;
	unsigned long totalSeekLength=0;
 
  for(int i=0;i<numThreads;i++)
  {
    totalReadCount += data[i].readCount;
    totalSuccessfulReads += data[i].successfulReads;
    totalUnsuccessfulReads += data[i].unSuccessfulReads;
    totalReadRetries += data[i].readRetries;

    totalInsertCount += data[i].insertCount;
    totalSuccessfulInserts += data[i].successfulInserts;
    totalUnsuccessfulInserts += data[i].unSuccessfulInserts;
    totalInsertRetries += data[i].insertRetries;
    totalDeleteCount += data[i].deleteCount;
    totalSuccessfulDeletes += data[i].successfulDeletes;
    totalUnsuccessfulDeletes += data[i].unSuccessfulDeletes;
    totalDeleteRetries += data[i].deleteRetries;
	totalSeekRetries += data[i].seekRetries;
  }
	unsigned long totalOperations = totalReadCount + totalInsertCount + totalDeleteCount;
	// Here we need to add all the results and also update the methods
	// to contain data[i], insertCounts and retries etc.
	// This code is for synchrobench. Here it ends
	isValidTree(root);
	double MOPS = (totalOperations/(duration*1000000.0));
	std::cout<<"Through Put is : "<<MOPS<<std::endl; 
	//testbenchParallelRemove();
	return 0;
}
 */

































