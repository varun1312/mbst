#include <iostream>
#include <cstdio>
#include <climits>
#include <vector>
#include <thread>

const int L = 0, R = 1;
const int NORMAL = 0, MARKED = 1, PROMOTE = 2, UNQNULL = 3;

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

// Decleration
bool removeTree(int);

struct Node {
	Node *bl;
	volatile int *dp;
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

// This is the node that is returned by the seekTree method.
struct seekNode {
	Node *ancNode;
	Node *pred;
	Node *curr;
	volatile int* ancNodeDataPtr;
	
	seekNode(Node *ancNode, Node *pred, Node *curr, volatile int* ancNodeDataPtr) {
		this->ancNode = ancNode;
		this->pred = pred;
		this->curr = curr;
		this->ancNodeDataPtr = ancNodeDataPtr;
	}
};

// This Node is the root of the tree.
Node *root = new Node(INT_MAX);

seekNode *seekTree(Node *startNode, Node *_ancNode, int data) {
	Node *curr = startNode;
	Node *pred = curr;
	Node *ancNode = _ancNode;
	volatile int *ancNodeDataPtr = _ancNode->dp;
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
			ancNodeDataPtr = GETADDR(curr)->dp;
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
	seekNode *mySeek = new seekNode(ancNode, pred, curr, ancNodeDataPtr);
	return mySeek;
};

void printTree(Node *pred, Node *node) {
	if (ISNULL(node) || ((STATUS(node) == PROMOTE) && (GETADDR(pred->ch[R]) == GETADDR(node))))
		return;
	printTree(GETADDR(node), GETADDR(node)->ch[L]);
	if (GETDATA(node) != (INT_MAX -1)) {
		std::cout<<"VARUN ";
		std::cout<<GETDATA(node)<<" "<<STATUS(GETADDR(node)->ch[L])<<" "<<STATUS(GETADDR(node)->ch[R])<<" "<<STATUS(GETADDR(node)->dp)<<std::endl;
		std::cout<<"ROOT : "<<root<<std::endl;
		std::cout<<"Pred : "<<pred<<" Curr : "<<node<<std::endl;
		std::cout<<"Curr Right : "<<GETADDR(node)->ch[R]<<" Curr Left : "<<GETADDR(node)->ch[L]<<std::endl;
		std::cout<<"Pred Right : "<<GETADDR(pred)->ch[R]<<" Pred Left : "<<GETADDR(pred)->ch[L]<<std::endl;
	}
	printTree(GETADDR(node), GETADDR(node)->ch[R]);
}

bool insertTree(int data) {
	seekNode *insertSeek = seekTree(root, root, data);
	Node *pred = insertSeek->pred;
	Node *curr = insertSeek->curr;
	if (ISUNQNULL(curr)) {
		int predData = GETDATA(pred);
		Node *myNode = new Node(data, pred);
		if (data > predData)
			pred->ch[R] = myNode;
		else if (data < predData)
			pred->ch[L] = myNode;
		return true;
	}
	else if (data == GETDATA(curr)) {
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

void markRight(Node *node) {
	Node *rp = node->ch[R];
	while(true) {
		int rpStatus = STATUS(rp);
		switch(rpStatus) {
			case MARKED:
				return;
			case PROMOTE:
				return;
			case NORMAL:
				if (CAS(&(node->ch[R]), rp, NORMAL, rp, MARKED))
					return;
				break;
			case UNQNULL:
				if (CAS(&(node->ch[R]), rp, UNQNULL, NULL, MARKED)) 
					return;
				break;
		}
		rp = node->ch[R];
	}
}


markStatus_t markTreeNode(Node *curr, Node *rp, Node *lp, volatile int *currDataPtr, int data) {
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
	if (STATUS(lp) != NORMAL) {
		if (STATUS(lp) == PROMOTE)
			return REMOVE_ANCNODE;
		else if (STATUS(lp) == MARKED) {
			markRight(curr);
			return REMOVE_ZERO_ONE_CHILD;
		}
		else if (STATUS(lp) == UNQNULL) {
			if (CAS(&(curr->ch[L]), lp, UNQNULL, NULL, MARKED)) {
				markRight(curr);
				return REMOVE_ZERO_ONE_CHILD;	
			}
			else
				return RETRY;
		}
	}

	// Coming here means that either of the child pointers are not NORMAL.
	//if (ISNULL(rp) || (STATUS(rp) == PROMOTE)) {
	if (STATUS(rp) != NORMAL) {
		if (STATUS(rp) == MARKED) {
			markLeft(curr);
			return REMOVE_ZERO_ONE_CHILD;
		}
		else if (STATUS(rp) == PROMOTE) {
			markStatus_t lpStatus = markLeft(curr);
			if (lpStatus == REMOVE_ANCNODE)
				return REMOVE_ANCNODE;
			return REMOVE_ZERO_ONE_CHILD;
		}
		else if (STATUS(rp) == UNQNULL) {
			if ((GETADDR(rp) == root) && (STATUS(curr->dp) == MARKED) && (GETADDR(curr->dp) == GETADDR(currDataPtr))) {
				if (!ISNULL(lp)) {
					return REMOVE_TWOCHILD;
				}
			}
			else {
				if (CAS(&(curr->ch[R]), rp, UNQNULL, NULL, MARKED)) {
					markLeft(curr);
					return REMOVE_ZERO_ONE_CHILD;
				}
				else {
					return RETRY;
				}
			}
		}
	}
		
	rp = curr->ch[R];
	lp = curr->ch[L];
	if ((STATUS(rp) == MARKED) && (STATUS(lp) == MARKED))
		return REMOVE_ZERO_ONE_CHILD;
	else if ((STATUS(rp) == PROMOTE) && (STATUS(lp) == PROMOTE))
		return REMOVE_ANCNODE;
	else if ((STATUS(rp) == PROMOTE) && (STATUS(lp) == MARKED))
		return REMOVE_ZERO_ONE_CHILD;
}

bool removeTreeNodeZeroOneChild(Node *pred, Node *curr, Node *ancNode, int data) {
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
	//	std::cout<<data<<" HELLO  "<<__LINE__<<std::endl;
	//	std::cout<<pred<<" "<<curr<<" "<<data<<" "<<GETDATA(pred)<<" "<<__LINE__<<std::endl;
	//	std::cout<<pred->bl<<" "<<curr->bl<<" "<<data<<" "<<GETDATA(pred)<<" "<<__LINE__<<std::endl;
	//	std::cout<<(pred->ch[L])<<" "<<(pred->ch[R])<<" "<<GETDATA(pred)<<" "<<__LINE__<<std::endl;
	//	std::cout<<(pred->bl->ch[L])<<" "<<(pred->bl->ch[R])<<" "<<GETDATA(pred)<<" "<<__LINE__<<std::endl;
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
					CAS(&(ptr->bl), curr, NORMAL, pred, NORMAL);
			//	rp = curr->ch[R];
			//	rp = curr->ch[L];
			//	std::cout<<"Deleted : "<<data<<" "<<curr<<" "<<STATUS(rp)<<" "<<STATUS(lp)<<std::endl;
			//	std::cout<<pred<<" "<<pred->ch[R]<<" "<<pred->ch[L]<<std::endl;
				//std::cout<<STATUS(rp)<<" "<<STATUS(lp)<<std::endl;
				return true;
			}	
		}
		Node *predRight = pred->ch[R];
		if (STATUS(predRight) == UNQNULL) {
			return true;
		}
		else if (STATUS(predRight) == MARKED) {
			int predData = GETDATA(pred);
		//	std::cout<<pred->bl<<" "<<pred<<" "<<__LINE__<<std::endl;
		//	std::cout<<(pred->bl)->ch[R]<<" "<<(pred->bl)->ch[L]<<" "<<__LINE__<<std::endl;
			if (GETADDR(pred->bl->ch[L]) == pred || GETADDR(pred->bl->ch[R]) == pred)
				removeTreeNodeZeroOneChild(pred->bl, pred, ancNode, predData);
			else
				CAS(&(curr->bl), pred, NORMAL, pred->bl, NORMAL);
		//	std::cout<<curr->bl<<" "<<curr<<" "<<__LINE__<<std::endl;
		//	std::cout<<(curr->bl)->ch[R]<<" "<<(curr->bl)->ch[L]<<" "<<__LINE__<<std::endl;
			return removeTreeNodeZeroOneChild(curr->bl, curr, ancNode, data);
		}
		else if (STATUS(predRight) == NORMAL) {
		//	std::cout<<predRight<<" "<<data<<" "<<__LINE__<<std::endl;
			if (GETADDR(predRight) == curr) {
				std::cout<<"This is an error "<<__LINE__<<std::endl;
				exit(1);
			}
			return true;		
		}
		else if (STATUS(predRight) == PROMOTE) {
			std::cout<<"This is an error "<<__LINE__<<std::endl;
			exit(1);
		}
	}
	else if (GETADDR(pred->ch[L]) == curr) {
		if (STATUS(pred->ch[L]) == NORMAL) {
			if (CAS(&(pred->ch[L]), curr, NORMAL, ptr, status)) {
				if (ptr != root && ptr != NULL)
					CAS(&(ptr->bl), curr, NORMAL, pred, NORMAL);
		//		rp = curr->ch[R];
		//		rp = curr->ch[L];
			//	std::cout<<"Deleted : "<<data<<" "<<STATUS(rp)<<" "<<STATUS(lp)<<std::endl;
			//	std::cout<<pred<<" "<<pred->ch[R]<<" "<<pred->ch[L]<<std::endl;
				return true;
			}
		}
		Node *predLeft = pred->ch[L];
		if (STATUS(predLeft) == UNQNULL) {
			return true;
		}
		else if (STATUS(predLeft) == PROMOTE) {
			int predData = GETDATA(pred);
		//	std::cout<<"HERE..."<<std::endl;
			Node *predRight = GETADDR(pred->ch[R]);	
			volatile int *predRightDataPtr = predRight->dp;
			int predAncData = *(int *)((uintptr_t)predRightDataPtr & ~0x03);
			if (GETADDR(pred->bl->ch[L]) == pred || GETADDR(pred->bl->ch[R]) == pred) {
				if ((STATUS(predRightDataPtr) == MARKED) && (predAncData != predData)) {
					CAS(&(predRight->dp), predRightDataPtr, MARKED, pred->dp, NORMAL);
				}
				removeTreeNodeZeroOneChild(pred->bl, pred, ancNode, predData);
			}
			else
				CAS(&(curr->bl), pred, NORMAL, pred->bl, NORMAL);
			return removeTreeNodeZeroOneChild(curr->bl, curr, ancNode, data);
		}
		else if (STATUS(predLeft) == MARKED) {
			int predData = GETDATA(pred);
			if (GETADDR(pred->bl->ch[L]) == pred || GETADDR(pred->bl->ch[R]) == pred)
				removeTreeNodeZeroOneChild(pred->bl, pred, ancNode, predData);
			else
				curr->bl = pred->bl;
			return removeTreeNodeZeroOneChild(pred->bl, curr, ancNode, data);
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

bool removeTreeNodeTwoChild(Node *pred, Node *curr, volatile int *currDataPtr, int data) {
	if (GETADDR(pred) == GETADDR(curr))
		return false;
	Node *rp = curr->ch[R];
	Node *lp = curr->ch[L];
	if (STATUS(lp) == NORMAL) {
		if ((STATUS(rp) == NORMAL) || ((GETADDR(rp) == root) && (STATUS(currDataPtr) == MARKED))) {
			if ((GETADDR(rp->ch[R]) == NULL) && (STATUS(rp->ch[R]) == MARKED) && (GETADDR(rp->ch[L]) == NULL) && (STATUS(rp->ch[L]) == MARKED)) {
				if (CAS(&(curr->ch[R]), rp, NORMAL, root, UNQNULL)) {
					return removeTree(data);
				}
				return removeTree(data);
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
			volatile int *succDataPtr = succ->dp;
			int	succData = *(int *)((uintptr_t)succDataPtr & ~0x03);
			if (GETADDR(currDataPtr) != GETADDR(curr->dp))
				return false;
			if (STATUS(succRight) == MARKED) {
				markLeft(succ);
				removeTreeNodeZeroOneChild(succPred, succ, curr, succData);
				return removeTreeNodeTwoChild(pred, curr, currDataPtr, data);
			}
			else if (STATUS(succRight) == PROMOTE) {
					markStatus_t ls = markLeftPromote(succ);
					if (ls == LEFT_MARKED && (GETADDR(succRight) == curr) && (data != succData)) {
					//	std::cout<<"Succ Data is : "<<succData<<" curr Data is : "<<data<<std::endl;
						CAS(&(curr->dp), currDataPtr, MARKED, succ->dp, NORMAL);
					//	std::cout<<"Succ Data is : "<<succData<<" curr Data is : "<<GETDATA(curr)<<std::endl;
						return removeTreeNodeZeroOneChild(succPred, succ, curr, GETDATA(succ));
					}
					else {
						removeTreeNodeZeroOneChild(succPred, succ, curr, succData);
						return removeTreeNodeTwoChild(pred, curr, currDataPtr, data);
					}
			}
			if (STATUS(succRight) == UNQNULL) {
				if ((GETADDR(succRight) == root) && (STATUS(succDataPtr) == MARKED)) {
					removeTreeNodeTwoChild(succ->bl, succ, succ->dp, succData);
					return removeTreeNodeTwoChild(pred, curr, currDataPtr, data);
				}
				if (CAS(&(succ->ch[R]), succRight, UNQNULL, curr, PROMOTE)) {
					markStatus_t ls = markLeftPromote(succ);
					int succData = GETDATA(succ);
					if (data == succData) {
						//std::cout<<data<<" "<<__LINE__<<std::endl;
						removeTreeNodeZeroOneChild(succPred, succ, curr, succData);
						return removeTreeNodeTwoChild(pred, curr, currDataPtr, data);
					}
					else if (ls == LEFT_MARKED) {
					//	std::cout<<"Succ Data is : "<<succData<<" curr Data is : "<<data<<std::endl;
						CAS(&(curr->dp), currDataPtr, MARKED, succ->dp, NORMAL);
					//	std::cout<<"Succ Data is : "<<succData<<" curr Data is : "<<GETDATA(curr)<<std::endl;
					//	std::cout<<data<<" "<<curr->bl<<" "<<curr<<" "<<" "<<succ->bl<<" "<<succ<<" "<<__LINE__<<std::endl;
						return removeTreeNodeZeroOneChild(succPred, succ, curr, GETDATA(succ));
					}
					else {
						removeTreeNodeZeroOneChild(succPred, succ, curr, succData);
						return removeTreeNodeTwoChild(pred, curr, currDataPtr, data);
					}
				}	
			}
			return removeTreeNodeTwoChild(pred, curr, currDataPtr, data);
		}
		else
			return removeTree(data);
	}
	else 
		return removeTree(data);
}

bool removeTree(int data) {
	std::thread::id tid = std::this_thread::get_id();
	//std::cout<<"Removing : "<<data<<" Thread ID : "<<tid<<std::endl;
	seekNode *removeSeek = seekTree(root, root, data);
	Node *pred = removeSeek->pred;
	Node *curr = removeSeek->curr;
	Node *ancNode = removeSeek->ancNode;
	volatile int *ancNodeDataSeen = removeSeek->ancNodeDataPtr;
	int ancNodeDataSeenD = *(int *)((uintptr_t)ancNodeDataSeen & ~0x03);
	volatile int *ancNodeDataPtr = ancNode->dp;
	int ancNodeDataCurr = *(int *)((uintptr_t)ancNodeDataPtr & ~0x03); 
	if (GETADDR(ancNodeDataSeen) != GETADDR(ancNodeDataPtr))
		return removeTree(data);
	if (ISNULL(curr)) {
		if (GETADDR(curr) != NULL && (STATUS(curr->ch[R]) == PROMOTE) && (STATUS(curr->ch[L]) == PROMOTE)) {
			if (GETDATA(curr->ch[R]) == data)
				return removeTree(data);
		}
		std::cout<<"Pred : "<<pred<<" Curr : "<<curr<<std::endl;
		std::cout<<"Curr Right : "<<GETADDR(curr)->ch[R]<<" Curr Left : "<<GETADDR(curr)->ch[L]<<std::endl;
	//	std::cout<<"Pred Right : "<<GETADDR(pred)->ch[R]<<" Pred Left : "<<GETADDR(pred)->ch[L]<<std::endl;
		std::cout<<data<<" "<<__LINE__<<" "<<" pred data : "<<GETDATA(pred)<<" "<<tid<<std::endl;
		std::cout<<ancNodeDataSeen<<" "<<ancNodeDataSeenD<<" "<<ancNodeDataCurr<<" "<<data<<std::endl;
		return false;
	}
	else if ((STATUS(curr) == PROMOTE) && (GETADDR(pred->ch[R]) == GETADDR(curr))) {
		std::cout<<data<<" "<<__LINE__<<std::endl;
		return false;
	}
	curr = GETADDR(curr);
	volatile int *currDataPtr = curr->dp;
	int currData = *(int *)((uintptr_t)currDataPtr & ~0x03);
	Node *rp = curr->ch[R];
	Node *lp = curr->ch[L];
	if (data == currData) {
		markStatus_t markStat = markTreeNode(curr, rp, lp, currDataPtr, data);
		//std::cout<<data<<" "<<markStat<<" "<<__LINE__<<std::endl;
		rp = curr->ch[R];
		lp = curr->ch[L];
		if ((STATUS(rp) ==  UNQNULL) && (STATUS(lp) == UNQNULL))
			return removeTree(data);
		currDataPtr = curr->dp;
		if (markStat == RETRY) {
			return removeTree(data);
		}
		else if (markStat == REMOVE_ZERO_ONE_CHILD) {
			return removeTreeNodeZeroOneChild(pred, curr, ancNode, data);
		}
		else if (markStat == REMOVE_TWOCHILD) {
			return removeTreeNodeTwoChild(pred, curr, currDataPtr, data);
		}
		else if (markStat == REMOVE_ANCNODE) {
			if (GETADDR(curr->ch[R]) == ancNode) {
				//std::cout<<data<<" "<<__LINE__<<std::endl;
				CAS(&(ancNode->dp), ancNodeDataPtr, MARKED, currDataPtr, MARKED);
				removeTreeNodeZeroOneChild(pred, curr, ancNode, data);
				return removeTree(data);
			}
			else {
				return removeTree(data);
			}
		}
		return true;
	}
	else
		return removeTree(data);
}



void testbenchParallelRemove() {
	const int numThreads = 100;
	int arr[numThreads];
	srand(time(NULL));
	std::vector<std::thread> removeThreads(numThreads);
	for (int i = 0; i < numThreads; i++) {
		do {
			arr[i] = rand();
		} while(arr[i] == INT_MAX || (arr[i] == (INT_MAX - 1)));
	}
	for (int i = 0; i < numThreads; i++) {
	//	std::cout<<"Array is : "<<arr[i]<<std::endl;
		insertTree(arr[i]);
	}
	std::cout<<"Removing Elements..."<<std::endl;
	for (int i = 0; i < numThreads; i++) {
		removeThreads[i] = std::thread(&removeTree, arr[i]);
	}
	for (int i = 0; i < numThreads; i++) {
		removeThreads[i].join();
	}
	std::cout<<"Printing after removing elements"<<std::endl;
	printTree(root, root->ch[L]);
	
}

int main(void) {
	testbenchParallelRemove();
	return 0;
}


































