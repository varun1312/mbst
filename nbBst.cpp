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
			//return false for now;
			return false;
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
	for (int i = 0; i < numThreads; i++) 
		addT[i] = std::thread(insert,root, rand());
	for (int i = 0; i < numThreads; i++) 
		addT[i].join();
	print(root->child[LEFT]);
}

int main(void) {
	testbenchParallel();
	//testbenchSequential();
	return 0;
}
