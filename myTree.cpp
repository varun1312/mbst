#include <iostream>
#include <climits>
#include <vector>
#include <thread>

#define GETADDR(n) ((Node *)((uintptr_t)n & ~0x03))
#define STATUS(n) ((uintptr_t)n & 0x03)
#define ISNULL(n) ((GETADDR(n) == NULL) || (STATUS(n) == UNQNULL))
#define GETDATA(n) (*(int *)((uintptr_t)(GETADDR(n)->dp) & ~0x03))
#define MARKNODE(node, status) (Node *)(((uintptr_t)node & ~0x03) | status)

#define CAS(ptr, s, ss, t, ts) \
	__sync_bool_compare_and_swap(ptr, MARKNODE(s, ss), MARKNODE(t, ts))
#define ISMARKED(n) ((STATUS(n->ch[R]) == MARKED ) || (STATUS(n->ch[L]) == MARKED) || (STATUS(n->dp) == MARKED))
#define DEBUG_MSG(n) std::cout<<__LINE__<<" : "<<(STATUS(n->ch[R]))<<" "<<(STATUS(n->ch[L]))<<" "<<(STATUS(n->dp))<<" "


const int NORMAL = 0, MARKED = 1, PROMOTE = 2, UNQNULL = 3;
const int L = 0, R = 1;

enum mS_t {
	R_0C = 3,
	R_1C = 5,
	R_2C = 7,
	R_M = 11,
	R_AN = 13,
	L_M = 17,
	H_R = 19,
	A_R = 23
};

struct Node {
	Node *bl;
	int *dp;
	Node *ch[2];
	
	Node (int data, Node *_bl) {
		dp = new int(data);
		ch[L] = (Node *)(UNQNULL);
		ch[R] = (Node *)(UNQNULL);
		this->bl = _bl;
	}

	Node (int data) {
		dp = new int(data);
		ch[L] = (Node *)(UNQNULL);
		ch[R] = (Node *)(UNQNULL);
		bl = this;
	}
};
bool rT(Node *, int);
bool iT(Node *, int);

Node *root = new Node(INT_MAX);

struct sN {
	Node *aN;
	Node *p;
	Node *c;
	int aND;
	
	sN(Node *_aN, Node *_p, Node *_c, int _data) {
		this->aN = _aN;
		this->p = _p;
		this->c = _c;
		this->aND = _data;
	}
};

sN* sT(Node *startNode, int data) {
	Node *c = GETADDR(startNode);
	Node *p = c->bl;
	Node *aN = p;
	int aNd = GETDATA(aN);
	bool lr = false;
	while(true) {
		if (ISNULL(c) || ((STATUS(c) == PROMOTE) && (lr == true))) {
			break;
		}
		int cD = GETDATA(c);
		if (data > cD) {
			p = GETADDR(c);
			c = GETADDR(c)->ch[R];
			lr = true;
		}
		else if (data < cD) {
			aN = p;
			aNd = GETDATA(aN);	
			p = GETADDR(c);
			c = GETADDR(c)->ch[L];
			lr = false;
		}
		else if (data == cD)
			break;
	}
	sN *n = new sN(aN, p, c, aNd);
	return n;
}

mS_t markLeftPromote(Node *n) {
	Node *lp = n->ch[L];
	while(true) {
		if (STATUS(lp) == NORMAL) {
			if (CAS(&(n->ch[L]), lp, NORMAL, lp, PROMOTE)) {
				return L_M;
			}
		}
		else if (STATUS(lp) == MARKED) {
			return R_AN;
		}
		else if (STATUS(lp) == UNQNULL) {
			if (CAS(&(n->ch[L]), lp, UNQNULL, NULL, PROMOTE)) {
				return L_M;
			}
		}
		else if (STATUS(lp) == PROMOTE) {
			return L_M;
		}
		lp = n->ch[L];
	}
}

void markLeft(Node *n) {
	Node *lp = n->ch[L];
	while(true) {
		if (STATUS(lp) == NORMAL) {
			if (CAS(&(n->ch[L]), lp, NORMAL, lp, MARKED)) {
				return;
			}
		}
		else if (STATUS(lp) == MARKED) {
			return;
		}
		else if (STATUS(lp) == UNQNULL) {
			if (CAS(&(n->ch[L]), lp, UNQNULL, NULL, MARKED)) {
				return;
			}
		}
		else if (STATUS(lp) == PROMOTE)
			return;
		lp = n->ch[L];
	}
}

mS_t markRight(Node *n) {
	Node *rp = n->ch[R];
	while(true) {
		if (STATUS(rp) == NORMAL) {
			if (CAS(&(n->ch[R]), rp, NORMAL, rp, MARKED)) {
				return R_M;
			}
		}
		else if (STATUS(rp) == MARKED) {
			return R_M;
		}
		else if (STATUS(rp) == UNQNULL) {
			if (CAS(&(n->ch[R]), rp, UNQNULL, NULL, MARKED)) {
				return R_M;
			}
		}
		else if (STATUS(rp) == PROMOTE) {
			return R_AN;
		}
		rp = n->ch[R];
	}
}

mS_t mTN(Node *n, int *nP) {
	Node *rp = n->ch[R];
	while(true) {
		if (STATUS(rp) == NORMAL) {
			break;
		}
		else if (STATUS(rp) == MARKED) {
			markLeft(n);
			break;
		}
		else if (STATUS(rp) == PROMOTE) {
			mS_t ls = markLeftPromote(n);
			return R_AN;
		}
		else if ((STATUS(rp) == UNQNULL) && (GETADDR(rp) != root)) {
			if (CAS(&(n->ch[R]), rp, UNQNULL, NULL, MARKED)) {
				markLeft(n);
				break;
			}
		}
		else if ((STATUS(rp) == UNQNULL) && (GETADDR(rp) == root)) {
			return H_R;
		}
		rp = n->ch[R];
	}

	Node *lp = n->ch[L];
	while(true) {
		if (STATUS(lp) == NORMAL) {
			break;
		}
		else if (STATUS(lp) == MARKED) {
			mS_t rs = markRight(n);
			if (rs == R_AN)
				return R_AN;
			break;
		}
		else if (STATUS(lp) == UNQNULL) {
			if (CAS(&(n->ch[L]), lp, UNQNULL, NULL, MARKED)) {
				mS_t rs = markRight(n);
				if (rs == R_AN)
					return R_AN;
				break;
			}
		}
		else if (STATUS(lp) == PROMOTE) {
			return R_AN;
		}
		lp = n->ch[L];
	}
	rp = n->ch[R];
	lp = n->ch[L];
	if (ISNULL(rp) && ISNULL(lp)) {
		return R_0C;
    }
	else if (ISNULL(rp) || ISNULL(lp)) {
		return R_1C;
    }
	
	if (CAS(&(n->dp), nP, NORMAL, nP, MARKED)) {
		return R_2C;
	}
	else if (STATUS(n->dp) == MARKED) {
		return R_2C;
	}
	return A_R;
}

bool hSD(int *sDp, Node *aN, int *aNDP) {
	return CAS(&(aN->dp), aNDP, MARKED, sDp, STATUS(sDp));
}

bool rTN(Node *p, Node *c, int data) {
	int status;
	Node *ptr;
	Node *rp, *lp;
	rp = c->ch[R];
	lp = c->ch[L];
	if (ISNULL(lp) && (ISNULL(rp) || (STATUS(rp) == PROMOTE))) {
		if (STATUS(p->dp) == MARKED) 
			ptr = root, status = UNQNULL;
		ptr = c, status = UNQNULL;
	}
	else if (ISNULL(lp))
		ptr = GETADDR(rp), status = NORMAL;
	else
		ptr = GETADDR(lp), status = NORMAL;

	int pD = GETDATA(p);
	if (data > pD) {
		if (CAS(&(p->ch[R]), c, NORMAL, ptr, status)) {
			if ((ptr != root) && (ptr != NULL) && (status != UNQNULL))
				ptr->bl = p;
			return true;
		}
		else {
			Node *pR = p->ch[R];
			if ((GETADDR(pR) == c) && (((STATUS(pR) != UNQNULL))) ) {
				Node *pBl = p->bl;	
				if ((GETADDR(pBl->ch[L]) == p) || (GETADDR(pBl->ch[R]) == p)) {
					markLeft(p);
					if (STATUS(pR) == PROMOTE) 
						hSD(p->dp, GETADDR(pR),GETADDR(pR)->dp);
					rTN(pBl, p, pD);
					if (STATUS(p->ch[R]) == PROMOTE) {
						rT(GETADDR(pR)->bl, pD);
					}
				}
				return rTN(c->bl, c, data);
			}
			if (ptr != root && ptr != NULL && ptr->bl == c)
				ptr->bl = p;
			return false;
		}
	}
	else {
		if (CAS(&(p->ch[L]), c, NORMAL, ptr, status)) {
			if ((ptr != root) && (ptr != NULL) && (status != UNQNULL))
				ptr->bl = p;
			return true;
		}
		else {
			Node *pL = p->ch[L];
			if ((GETADDR(pL) == c) && (((STATUS(pL) != UNQNULL))) ) {
				Node *pBl = p->bl;
				if ((GETADDR(pBl->ch[L]) == p) || (GETADDR(pBl->ch[R]) == p)) {
					mS_t stat = markRight(p);
					if (stat == R_AN)
						hSD(p->dp, GETADDR(pL),GETADDR(pL)->dp);
					rTN(pBl, p, pD);
					if (stat == R_AN) {
						rT(GETADDR(pL)->bl, pD);
					}
				}
				return rTN(c->bl, c, data);
			}
			if (ptr != root && ptr != NULL && ptr->bl == c)
				ptr->bl = p;
			return false;
		}
	}
}

bool rTNTC(Node *p, Node *c, int *dp, int data) {
	Node *rp = c->ch[R];
	if (STATUS(rp) == NORMAL) {
		Node *rPtr = GETADDR(rp);
		if ((GETADDR(rPtr->ch[L]) == NULL) && (GETADDR(rPtr->ch[R]) == NULL) && (STATUS(rPtr->ch[L]) == MARKED) && (STATUS(rPtr->ch[R]) == MARKED)) {
			if (CAS(&(c->ch[R]), rPtr, NORMAL, NULL, MARKED)) {
				markLeft(c);
				return rTN(p, c, data);
			}
			return rTNTC(p, c, dp, data);
		}
	}
	else if (STATUS(rp) == MARKED) {
		markLeft(c);
		return rTN(p, c ,data);
	}
	else if ((STATUS(rp) == UNQNULL) && (GETADDR(rp) != root)) {
		return rT(p, data);
	}
	else if (STATUS(rp) == PROMOTE) {
		markLeft(c);
		hSD(dp, GETADDR(rp),GETADDR(rp)->dp);
		rTN(p, c, data);
		return rT(GETADDR(rp)->bl, data);
	}
	
	Node *lp = c->ch[L];
	if (STATUS(lp) != NORMAL)
		return rT(p, data);

	Node *sc = c;
	Node *sr = GETADDR(lp);
	while(true) {
		if (ISNULL(sr) || (STATUS(sr) == PROMOTE)) {
			break;
		}
		sc = GETADDR(sc);
		sr = GETADDR(sc)->ch[R];
	}
	int *sdp = sc->dp;
	int sd = *(int *)((uintptr_t)sdp & ~0x03);
	if (data != GETDATA(c))
		return false;
	if ((GETADDR(sr) == root) && (STATUS(sc->dp) == MARKED)) {
		rTNTC(sc->bl, sc, sdp, sd);
		return rTNTC(p, c, dp, data);
	}
	else if (CAS(&(sc->ch[R]), sr, UNQNULL, c, PROMOTE)) {
		// CASE of Promote.
		hSD(sdp, c, dp);
		mS_t stat = markLeftPromote(sc);
		rTN(sc->bl, sc, sd);
		if (stat == R_AN) {
			return rT(p, sd);
		}
		return true;
	}
	else if (STATUS(sr) == PROMOTE) {
		hSD(sdp, c, dp);
		mS_t stat = markLeftPromote(sc);
		rTN(sc->bl, sc, sd);
		if (stat == R_AN) {
			return rT(p, sd);
		}
		return true;
	}
	else if (STATUS(sr) == MARKED) {
		markLeft(sc);
		rTN(sc->bl, sc, sd);
		return rTNTC(p, c, dp, data);
	}
	return rTNTC(p, c, dp, data);
}

bool rT(Node *startNode, int data) {
	//std::cout<<"Removing  : "<<data<<std::endl;
	sN* remSeek = sT(startNode, data);
	Node *aN = remSeek->aN;
	Node *p = remSeek->p;
	Node *c = remSeek->c;
	int aNDP = remSeek->aND;
	int aNDC = GETDATA(aN);
	if (aNDP != aNDC)
		return rT(aN->bl, data);
	int pD = GETDATA(p);
	if (ISNULL(c) || ((STATUS(c) == PROMOTE)  && (data > pD))) {
		return false;
	}
	int *cDP = GETADDR(c)->dp;
	int cD = *(int *)((uintptr_t)cDP & ~0x03);
	if (data == cD) {
		c = GETADDR(c);
		mS_t stat = mTN(c, cDP);
	//	std::cout<<data<<" : "<<stat<<std::endl;
		if (stat == R_0C) {
			return rTN(p, c, data);
		}
		else if (stat == R_1C) {
			return rTN(p, c, data);
		}
		else if (stat == R_2C) {
			return rTNTC(p, c, cDP, data);
		}
		else if (stat == R_AN) {
			hSD(cDP, GETADDR(c->ch[R]), GETADDR(c->ch[R])->dp);
			rTN(p, c, data);
			return rT(root, data);
		}
		DEBUG_MSG(c);
		return false;
	}
	else 
		return rT(root, data);
}

bool iTN(Node *p, Node *c, int stat, int data) {
	int pD = GETDATA(p);
	Node *mN = new Node(data, p);
	if (data > pD) {
		if (CAS(&(p->ch[R]), c, stat, mN, NORMAL))
			return true;
		else {
			// CAS failed means pred child changed or pred 	
			// is marked. Both the scenarions are handled by
			// insert method. We need to restart the insert
			// from pred
			return iT(p, data);
		}
	}
	else { 
		if (CAS(&(p->ch[L]), c, stat, mN, NORMAL))
			return true;
		else {
			// CAS failed means pred child changed or pred 	
			// is marked. Both the scenarions are handled by
			// insert method. We need to restart the insert
			// from pred
			return iT(p, data);
		}
	}
}


bool iT(Node *startNode, int data) {
	sN* insSeek = sT(startNode, data);
	Node *aN = insSeek->aN;
	Node *p = insSeek->p;
	Node *c = insSeek->c;
	int aNDP = insSeek->aND;
	int aNDC = GETDATA(aN);
	if (aNDP != aNDC)
		return iT(aN->bl, data);
	int pD = GETDATA(p);
	if (ISNULL(c) || ((STATUS(c) == PROMOTE) && (data > pD))) {
		if (STATUS(c) == UNQNULL) {
			return iTN(p, GETADDR(c), UNQNULL, data);
		}
		else if (STATUS(c) == PROMOTE) {
			hSD(p->dp, GETADDR(c), GETADDR(c)->dp);
			return iT(aN->bl, data);
		}
		else if (STATUS(c) == MARKED) {
			// This means pred is getting removed.
			// we must try remove in out style.
			// retry for now.
			if (data > pD) {
				// This means that right is marked.
				markLeft(p);
				return iTN(p->bl, p , NORMAL, data);
			}
			else {
				mS_t rs = markRight(p);
				if (rs == R_AN) {
					hSD(p->dp, GETADDR(c), GETADDR(c)->dp);
					return iT(aN->bl, data);
				}
				else {
					return iTN(p->bl, p , NORMAL, data);
				}
			}
		}
	}
	else if (data == GETDATA(c)) {
		// Here we need to check if the node is marked or not. If it is, then remove it
		// in insert style 
		if (ISMARKED(c)) {
			rT(GETADDR(c)->bl, data);
			return iT(p, data);
		}
		// returning true for now
		return true;
	}
	return true;
}

bool searchT(Node *startNode, int data) {
	sN* serSeek = sT(startNode, data);
	Node *aN = serSeek->aN;
	Node *p = serSeek->p;
	Node *c = serSeek->c;
	int aNDP = serSeek->aND;
	int aNDC = GETDATA(aN);
	if (aNDP != aNDC)
		return searchT(aN->bl, data);
	int pD = GETDATA(p);
	if (ISNULL(c) || ((STATUS(c) == PROMOTE)  && (data > pD))) {
		std::cout<<"ERROR : "<<__LINE__<<std::endl;
		return false;
	}
	int cD = GETDATA(c); 
	if (data == cD) {
		if (ISMARKED(c)) {
			std::cout<<"ERROR : "<<__LINE__<<std::endl;
			return false;
		}
		return true;
	}
	else {
		std::cout<<"ERROR : "<<__LINE__<<std::endl;
		return false;
	}
}

void printTree(Node *node) {
	if (ISNULL(node))
		return;
	printTree(GETADDR(node)->ch[L]);
	//DEBUG_MSG(GETADDR(node));
	std::cout<<GETDATA(node)<<std::endl;
	printTree(GETADDR(node)->ch[R]);
}

void printTreeRemove(Node *node) {
	if (ISNULL(node))
		return;
	printTreeRemove(GETADDR(node)->ch[L]);
	std::cout<<"[VARUN] : ";
	DEBUG_MSG(GETADDR(node));
	std::cout<<" "<<GETDATA(node)<<std::endl;
	printTreeRemove(GETADDR(node)->ch[R]);
}

void testbenchParallel() {
	const int n = 1000;
	srand(time(NULL));
	int arr[n];
	std::vector<std::thread> addT(n);
	std::vector<std::thread> remT(n);
	std::vector<std::thread> serT(n);
	for (int i = 0; i < n ; i++) {
		do {
			arr[i] = rand();
		} while(arr[i] == INT_MAX);
	}
	for (int i = 0; i < n ; i++)
		addT[i] = std::thread(&iT, root, arr[i]);
	for (int i = 0; i < n ; i++)
		addT[i].join();
	printTree(root->ch[L]);
	
	for (int i = 0; i < n ; i++)
		serT[i] = std::thread(&searchT, root, arr[i]);
	for (int i = 0; i < n ; i++)
		serT[i].join();
	std::cout<<"Removing elements"<<std::endl;
	for (int i = 0; i < n ; i++)
		remT[i] = std::thread(&rT, root, arr[i]);
	for (int i = 0; i < n ; i++)
		remT[i].join();
	std::cout<<"Printing after removing elements"<<std::endl;
	printTreeRemove(root->ch[L]);
}

int main(void) {
	testbenchParallel();
	return 0;
}
