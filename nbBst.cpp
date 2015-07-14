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

const Node *root = new Node(INT_MAX);
int main(void) {
	return 0;
}
