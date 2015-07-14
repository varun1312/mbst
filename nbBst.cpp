#include <iostream>
#include <climits>
#include <vector>
#include <thread>

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

bool insert(Node *node, int data) {
	seekNode *insSeek = seek(root, data);
	Node *ancNode = insSeek->ancNode;
	Node *pred = insSeek->pred;
	Node *curr = insSeek->curr;
	int ancNodeDataPrev = insSeek->ancNodeData;
	int ancNodeDataCurr = GETDATA(ancNode);
	if (ancNodeDataPrev != ancNodeDataCurr)
		return insert(ancNode, data);
	if (GETDATA(curr) == data) {
		/* Here check if node ptr is marked.
		If it is marked and 2C, then help.
		Else, just help remove that node and
		retry insert */
		// returning false for now;
		return false;
	}
	if (STATUS(curr) == UNQNULL) {
		return insert_data(pred, curr, data);
	}
	else if (STATUS(curr) == MARKED) {
		return insert_data(pred->bl, pred, data);
	}
	else if (STATUS(curr) == PROMOTE) {
		// Help swap data and then return insert.
		//return false for now;
		return false;
	}
}

void testbenchSequential() {
	const Node *root = new Node(INT_MAX);
}

int main(void) {
	testbenchSequential();
	return 0;
}
