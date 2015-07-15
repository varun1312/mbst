#include <iostream>
#include <climits>
#include <vector>
#include <thread>

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
bool insert(Node*, int);

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

seekNode* seek(Node *startNode, int data) {
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

void helpSwapData(Node *pred, Node *ancNode, int* dataPtr) {
//	if (CAS(&ancNode->dataPtr, dataPtr, MARKED, pred->dataPtr, MARKED))
//		return;
	// CAS failed means dataPtr already swapped. Therefore return;
	CAS(&ancNode->dataPtr, dataPtr, MARKED, pred->dataPtr, MARKED);
	return;
}

bool markSuccessor(Node *node) {
	Node *rp = node->child[RIGHT];
	while(STATUS(rp) == UNQNULL) {
		if (CAS(&node->child[RIGHT], rp, UNQNULL, rp, PROMOTE))
			return true;
		else {
			// Failure means some other thread inserted or status changed.
			rp = node->child[RIGHT];
			if (STATUS(rp) == NORMAL) {
			//	return SEEK_SUCC_FROM_NODE;
				return false;
			}
			else if (STATUS(rp) == MARKED) {
				// Node getting deleted.
				// Help remove this node and then mark successor.
			}
			else if (STATUS(rp) == UNQNULL) {
				return true;
			}
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
			helpSwapData(node, ancNode, dataPtr);
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
			seekNode* succSeek = seek(node, INT_MAX);
			// Mark Successor	
			// help swap data.
			// remove successor
			// then return false;
		}
		else if (STATUS(rp) == MARKED) {
			markLeft(node);
			break;
		}
		else if (STATUS(rp) == PROMOTE) {
			helpSwapData(node, ancNode, dataPtr);
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
//	std::cout<<data<<" "<<STATUS(rp)<<" "<<STATUS(lp)<<" "<<STATUS(node->dataPtr)<<std::endl;
	if (((STATUS(rp) == MARKED) || (STATUS(rp) == PROMOTE)) && (STATUS(lp) == MARKED)) {
		if ((GETADDR(rp) == NULL) && (GETADDR(lp) == NULL))
			return REMOVE_0C;
		return REMOVE_1C;
	}
	if (CAS(&(node->dataPtr), dataPtr, NORMAL, dataPtr, MARKED)) {
		return REMOVE_2C;
	}
//	std::cout<<data<<" "<<STATUS(rp)<<" "<<STATUS(lp)<<" "<<STATUS(node->dataPtr)<<std::endl;
	return ABORT_REMOVE;	
}

bool remove(Node *node, int data) {
	seekNode *remSeek = seek(node, data);
	Node *ancNode = remSeek->ancNode;
	Node *pred = remSeek->pred;
	Node *curr = remSeek->curr;
	int *ancNodeDataPtr = (int *)((uintptr_t)(ancNode->dataPtr) & ~0x03) ;
	int *dataPtr =  GETADDR(curr)->dataPtr;
	int ancNodeDataPrev = remSeek->ancNodeData;
	int ancNodeDataCurr = GETDATA(ancNode);
	if (ancNodeDataPrev != ancNodeDataCurr)
		return remove(ancNode, data);
	// Now Marking Starts
	if (ISNULL(curr))
		return false;
	if (data == GETDATA(curr)) {
		markStatus_t stat = markNode(GETADDR(curr), ancNode, dataPtr, data);
		return true;
	}
}

bool insert_data(Node *pred, Node *curr, int status, int data) {
	int predData = GETDATA(pred);
	Node *myNode = new Node(data, GETADDR(pred));
	if (data > predData) {
		if (CAS(&pred->child[RIGHT], curr, status, myNode, NORMAL))
			return true;
		else { 
		//	if (GETADDR(pred->child[RIGHT]) != curr) 
				return insert(pred, data);		
		//	else if (STATUS(pred->child[RIGHT]) != status)
		//		return insert(pred, data);
		}
	}
	else {
		if (CAS(&pred->child[LEFT], curr, status, myNode, NORMAL))
			return true;
		else 
		//	if (GETADDR(pred->child[RIGHT]) != curr) 
		//		return insert(pred, data);		
		//	else if (STATUS(pred->child[RIGHT]) != status)
				return insert(pred, data);
	}
}

bool insert(Node *node, int data) {
	seekNode *insSeek = seek(root, data);
	Node *ancNode = insSeek->ancNode;
	Node *pred = insSeek->pred;
	Node *curr = insSeek->curr;
	int *dataPtr = (int *)((uintptr_t)(ancNode->dataPtr) & ~0x03) ;
	int ancNodeDataPrev = insSeek->ancNodeData;
	int ancNodeDataCurr = GETDATA(ancNode);
	if (ancNodeDataPrev != ancNodeDataCurr)
		return insert(ancNode, data);
	if (ISNULL(curr)) {
		if (STATUS(curr) == UNQNULL) {
			return insert_data(pred, GETADDR(curr), UNQNULL, data);
		}
		else if (STATUS(curr) == MARKED) {
			return insert_data(pred->bl, pred, NORMAL, data);
		}
		else if (STATUS(curr) == PROMOTE) {
			// Help swap data and then return insert.
			helpSwapData(pred, ancNode, dataPtr);
			return insert(ancNode, data);
		}
	}
	else {
		if (GETDATA(curr) == data) {
			/* Here check if node ptr is marked.
			If it is marked and 2C, then help.
			Else, just help remove that node and
			retry insert */
			// returning false for now;
			return false;
		}
	}
}

void print(Node *node) {
	if (ISNULL(node))
		return;
	print(GETADDR(node)->child[LEFT]);
	if (!ISMARKED(node))
		std::cout<<GETDATA(node)<<std::endl;
	print(GETADDR(node)->child[RIGHT]);
}

void testbenchSequential() {
	srand(time(NULL));
	for (int i = 0; i < 10; i++)
		insert(root, rand());
	print(root->child[LEFT]);
}

void testbenchParallel() {
	const int numThreads = 100;
	srand(time(NULL));
	std::vector<std::thread> addT(numThreads);
	int arr[numThreads];
	for (int i = 0; i < numThreads; i++) 
		arr[i] = rand();
	for (int i = 0; i < numThreads; i++) 
		addT[i] = std::thread(insert,root, arr[i]);
	for (int i = 0; i < numThreads; i++) 
		addT[i].join();
	print(root->child[LEFT]);
	std::cout<<"Removing elements..."<<std::endl;
	for (int i = 0; i < numThreads; i++) 
		remove(root, arr[i]);
	std::cout<<"Printing removed elements..."<<std::endl;
	print(root->child[LEFT]);
/*	int removeElement;
	while(removeElement != -1) {
		std::cout<<"Enter an element to remove : ";
		std::cin>>removeElement;
		remove(root, removeElement);
		print(root->child[LEFT]);
	} */
}

int main(void) {
	testbenchParallel();
	//testbenchSequential();
	return 0;
}
