#include <iostream>
#include <climits>
#include <vector>
#include <thread>
#include <sys/time.h>
#include <signal.h>
#include <getopt.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <cstdio>
#include<gsl/gsl_rng.h>
#include<gsl/gsl_randist.h>
#include <ctime>
#include <mutex>

#define GETADDR(n) ((Node *)((uintptr_t)n & ~0x03))
#define STATUS(n) ((uintptr_t)n & 0x03)
#define ISNULL(n) ((GETADDR(n) == NULL) || (STATUS(n) == UNQNULL))
#define GETDATA(n) (*(int *)((uintptr_t)(GETADDR(n)->dp) & ~0x03))
#define MARKNODE(node, status) (Node *)(((uintptr_t)node & ~0x03) | status)

#define CAS(ptr, s, ss, t, ts) \
	__sync_bool_compare_and_swap(ptr, MARKNODE(s, ss), MARKNODE(t, ts))
#define ISMARKED(n) ((STATUS(n->ch[R]) == MARKED ) || (STATUS(n->ch[L]) == MARKED) || (STATUS(n->dp) == MARKED))
#define DEBUG_MSG(n) std::cout<<__LINE__<<" : "<<(STATUS(n->ch[R]))<<" "<<(STATUS(n->ch[L]))<<" "<<(STATUS(n->dp))<<" "

#define INV_MSG(data) std::cout<<"Line : "<<__LINE__<<" VARUN "<<data<<std::endl;
std::mutex myMutex;
const int NORMAL = 0, MARKED = 1, PROMOTE = 2, UNQNULL = 3;
const int L = 0, R = 1;


/* This code belongs to Synchrobench testbench */
#define DEFAULT_DURATION 2
#define DEFAULT_DATA_SIZE 256
#define DEFAULT_THREADS 1
#define DEFAULT_RANGE 0x7FFFFFFF
#define DEFAULT_SEED 0
#define DEFAULT_INSERT 20
#define DEFAULT_REMOVE 10
#define DEFAULT_SEARCH 70

/* This code belongs to Synchrobench testbench */
bool start = false, stop = false, steadyState = false;
/* This code belongs to Synchrobench testbench */
typedef struct barrier {
	pthread_cond_t complete;
	pthread_mutex_t mutex;
	int count;
	int crossing;
} barrier_t;

	int duration = DEFAULT_DURATION;
	int dataSize = DEFAULT_DATA_SIZE;
	int numThreads = DEFAULT_THREADS;
	int range = DEFAULT_RANGE;
	unsigned int seed = DEFAULT_SEED;
	int insertPer = DEFAULT_INSERT;
	int removePer = DEFAULT_REMOVE;
	int searchPer = DEFAULT_SEARCH;
	int initialSize = ((dataSize)/2);

/* This code belongs to Synchrobench testbench */
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

/* This code belongs to Synchrobench testbench */
void barrier_init(barrier_t *b, int n) {
	pthread_cond_init(&b->complete, NULL);
	pthread_mutex_init(&b->mutex, NULL);
	b->count = n;
	b->crossing = 0;
}

/* This code belongs to Synchrobench testbench */
void barrier_cross(barrier_t *b) {
	pthread_mutex_lock(&b->mutex);
	b->crossing++;
	if (b->crossing < b->count) {
		pthread_cond_wait(&b->complete, &b->mutex);
	}
	else {
		pthread_cond_wait(&b->complete, &b->mutex);
		b->crossing = 0;
	}
	pthread_mutex_unlock(&b->mutex);
}

/* This code belongs to Synchrobench testbench */
/* rand function without seed */
inline long rand_range(long r) {
	int m = RAND_MAX;
	int d, v = 0;
	do {
		d = (m > r ? r : m);
		v += 1 + (int)(d * ((double)rand()/((double)(m)+1.0)));
		r -= m;
	} while (r > 0);
	return v;
}

inline long rand_range_re(unsigned int *seed, long r) {
	int m = RAND_MAX;
	int d, v = 0;
	do {
		d = (m > r ? r : m);
		v += 1 + (int)(d * ((double)rand_r(seed)/((double)(m)+1.0)));
		r -= m;
	} while (r > 0);
	return v;
}

enum mS_t {
	R_0C = 3,
	R_1C = 5,
	R_2C = 7,
	R_M = 11,
	R_AN = 13,
	L_M = 17,
	H_R = 19,
	A_R = 23,
	RETRY = 29
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
		this->bl = this;
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

sN* sT(Node *startNode, Node *ancNode, int data) {
	Node *c = (startNode);
	Node *p = GETADDR(c)->bl;
	Node *aN = ancNode;
	int aNd = GETDATA(aN);
	bool lr = false;
	while(true) {
		if (ISNULL(c))
			break;
		int cD = GETDATA(c);
		if (data > cD) {
			p = GETADDR(c);
			c = GETADDR(c)->ch[R];
			if (ISNULL(c) || (STATUS(c) == PROMOTE))
				break;
		}
		else if (data < cD) {
			aN = p;
			aNd = GETDATA(aN);	
			p = GETADDR(c);
			c = GETADDR(c)->ch[L];
			if (ISNULL(c))
				break;
		}
		else if (data == cD)
			break;
	}
	sN *n = new sN(aN, p, c, aNd);
	return n;
}

mS_t markLeft(Node *n) {
	Node *lp = n->ch[L];
	while(true) {
		if (STATUS(lp) == MARKED) {
			return L_M;
		}
		else if (STATUS(lp) == UNQNULL) {
			if (CAS(&(n->ch[L]), lp, UNQNULL, NULL, MARKED)) {
				return L_M;
			}
		}
		else if (STATUS(lp) == NORMAL) {
			if (CAS(&(n->ch[L]), lp, NORMAL, lp, MARKED)) {
				return L_M;
			}
		}
		else if (STATUS(lp) == PROMOTE) {
			INV_MSG(GETDATA(n))
			exit(1);
		}
		lp = n->ch[L];
	}
}

mS_t markRight(Node *n) {
	Node *rp = n->ch[R];
	while(true) {
		if (STATUS(rp) == MARKED)
			return R_M;
		else if (STATUS(rp) == PROMOTE) {
			return R_M;
		}
		else if (STATUS(rp) == UNQNULL) {
			if (CAS(&(n->ch[R]), rp, UNQNULL, NULL, MARKED)) {
				return R_M;
			}
		}
		else if (STATUS(rp) == NORMAL) {
			if (CAS(&(n->ch[R]), rp, NORMAL, rp, MARKED)) {
				return R_M;
			}
		}
		rp = n->ch[R];
	}
}

mS_t markLeftPromote(Node *n) {
	Node *lp = n->ch[L];
	while(true) {
		if (STATUS(lp) == PROMOTE)
			return L_M;
		else if (STATUS(lp) == MARKED) {
			return R_0C;
		}
		else if (STATUS(lp) == NORMAL) {
			if (CAS(&(n->ch[L]), lp, NORMAL, lp, PROMOTE)) {
				return L_M;
			}
		}
		else if (STATUS(lp) == UNQNULL) {
			if (CAS(&(n->ch[L]), lp, UNQNULL, NULL, PROMOTE)) {
				return L_M;
			}
		}
		lp = n->ch[L];
	}
}

mS_t mTN(Node *c, Node *rp, Node *lp, int *cdp, int data) {
	if ((STATUS(rp) == NORMAL) && (STATUS(lp) == NORMAL)) {
		if (CAS(&(c->dp), cdp, NORMAL, cdp, MARKED)) {
			return R_2C;
		}
		else {
			cdp = c->dp;
			int cd = *(int *)((uintptr_t)cdp & ~0x03);
			if ((STATUS(cdp) == MARKED) && (cd == data))
				return R_2C;
			else 
				return A_R;
		}
	}
	if (ISNULL(rp)) {
		if (GETADDR(rp) == root) {
			if ((STATUS(cdp) == MARKED) || (GETADDR(c->dp) == GETADDR(cdp))) {
				if (!ISNULL(lp)) {
					return R_2C;
				}
			}
			else {
				if (CAS(&(c->ch[R]), rp, UNQNULL, NULL, MARKED)) {
					markLeft(c);
					lp = c->ch[L];
					if (GETADDR(lp) == NULL)
						return R_0C;
					return R_1C;
				}
				else
					return RETRY;
			}
		}
		else {
			if (STATUS(rp) == MARKED) {
				markLeft(c);
				lp = c->ch[L];
				if (GETADDR(lp) == NULL)
					return R_0C;
				return R_1C;
			}
			else if (STATUS(rp) == UNQNULL) {
				if (CAS(&(c->ch[R]), rp, UNQNULL, NULL, MARKED)) {
					markLeft(c);
					lp = c->ch[L];
					if (GETADDR(lp) == NULL)
						return R_0C;
					return R_1C;
				}	
				else
					return RETRY;
			}
			else if (STATUS(rp) == PROMOTE) {
				INV_MSG(data)
				exit(1);
			}
		}
	}
	if (ISNULL(lp)) {
		if (STATUS(lp) == MARKED) {
			markRight(c);
			rp = c->ch[R];
			if ((GETADDR(rp) == NULL) || (STATUS(rp) == PROMOTE))
				return R_0C;
			return R_1C;
		}
		else if (STATUS(lp) == PROMOTE) {
			// lp will be promote only if rp is promote.
			if (STATUS(rp) != PROMOTE) {
				INV_MSG(data)
				exit(1);
			}
			return R_2C;
		}
		else if (STATUS(lp) == UNQNULL) {
			if (CAS(&(c->ch[L]), lp, UNQNULL, NULL, MARKED)) {
				markRight(c);
				rp = c->ch[R];
				if ((GETADDR(rp) == NULL) || (STATUS(rp) == PROMOTE))
					return R_0C;
				return R_1C;
			}
			else 
				return RETRY;
		}
	}
	if ((STATUS(rp) == PROMOTE) && (STATUS(lp) == PROMOTE))
		return R_2C;
	else if ((STATUS(rp) == PROMOTE) && (STATUS(lp) == MARKED)){
		return R_1C;
	}
	else {
		return RETRY;
	}
}


bool rTN(Node *p, Node *c, int data) {
	Node *rp = c->ch[R];
	Node *lp = c->ch[L];
	if (((STATUS(rp) == MARKED) && (STATUS(lp) == MARKED)) || ((STATUS(rp) == PROMOTE) && (STATUS(lp) == PROMOTE)) || ((STATUS(rp) == PROMOTE) && (STATUS(lp) == MARKED))) {
		Node *ptr;
		int status;
		
		if ((STATUS(rp) == MARKED) && (STATUS(lp) == MARKED)) {
			if (GETADDR(lp) == NULL) {
				if (GETADDR(rp) == NULL) {
					if (STATUS(p->dp) == MARKED)
						ptr = root, status = UNQNULL;
					else
						ptr = GETADDR(c), status = UNQNULL;
				}
				else {
					ptr = GETADDR(rp), status = NORMAL;
				}
			}
			else if (GETADDR(rp) == NULL) {
				if (GETADDR(lp) == NULL) {
					if (STATUS(p->dp) == MARKED)
						ptr = root, status = UNQNULL;
					else
						ptr = GETADDR(c), status = UNQNULL;
				}
				else {
					ptr = GETADDR(lp), status = NORMAL;
				}
			}
		}
		else if ((STATUS(rp) == PROMOTE) && (STATUS(lp) == MARKED)) {
			if (GETADDR(lp) == NULL) {
				ptr = GETADDR(c), status = UNQNULL;
			}
			else {
				ptr = GETADDR(lp), status = NORMAL;
			}			
		}
		else if ((STATUS(rp) == PROMOTE) && (STATUS(lp) == PROMOTE)) {
			if (GETADDR(lp) == NULL) {
				ptr = GETADDR(c), status = UNQNULL;
			}
			else {
				ptr = GETADDR(lp), status = NORMAL;
			}			
		}
		
		if (GETADDR(p->ch[R]) == c) {
			if (CAS(&(p->ch[R]), c, NORMAL, ptr, status)) {
				if (ptr != root && ptr != NULL)
					CAS(&(ptr->bl), c, NORMAL, p, NORMAL);
				return true;
			}
			else {
				Node *pR = p->ch[R];
				if (STATUS(pR) == UNQNULL) {
					return false;
				}
				else if (STATUS(pR) == MARKED) {
					rTN(p->bl, p, GETDATA(p));
					return rTN(c->bl, c, data);
				}
				else if (STATUS(pR) == NORMAL) {
					if (GETADDR(pR) != c) {
						if (ptr != root && ptr != NULL)
							CAS(&(ptr->bl), c, NORMAL, p, NORMAL);
						return false;
					}
					else {
						INV_MSG(data);
						exit(1);
					}
				}
				else if (STATUS(pR) == PROMOTE) {
					INV_MSG(data)
				}
			}
		}
		else if (GETADDR(p->ch[L]) == c){
			if (CAS(&(p->ch[L]), c, NORMAL, ptr, status)) {
				if (ptr != root && ptr != NULL) {
					CAS(&(ptr->bl), c, NORMAL, p, NORMAL);
				}
				return true;
			}
			else {
				Node *pL = p->ch[L];
				if (STATUS(pL) == UNQNULL) {
					return false;
				}
				else if (STATUS(pL) == MARKED) {
					rTN(p->bl, p, GETDATA(p));
					return rTN(c->bl, c, data);
				}
				else if (STATUS(pL) == NORMAL) {
					if (GETADDR(pL) != c) {
						if (ptr != root && ptr != NULL)
							CAS(&(ptr->bl), c, NORMAL, p, NORMAL);
						return false;
					}
					else {
						INV_MSG(data)
						exit(1);
					}
				}
				else if (STATUS(pL) == PROMOTE) {
					rTN(p->bl, p, GETDATA(p));
					return rTN(c->bl, c, data);
				}
			}
		}
		else
			return false;
	}
	else {
		INV_MSG(data)
		exit(1);
	}
}

bool rTNTC(Node *p, Node *c, int *cdp, int data) {
	// Coming here means that data pointer should be marked 
	// and left, right should be NORMAL
	if (STATUS(cdp) == NORMAL) {
		return rT(root, data);	
	}
	Node *rp = c->ch[R];
	Node *lp = c->ch[L];
	if (((STATUS(rp) == NORMAL) || (GETADDR(rp) == root)) && (STATUS(lp) == NORMAL)) {
		if ((GETADDR(rp->ch[R]) == NULL) && (GETADDR(rp->ch[L]) == NULL) && (STATUS(rp->ch[R]) == MARKED) && (STATUS(rp->ch[L]) == MARKED)) {
			if (CAS(&(c->ch[R]), rp, NORMAL, root, UNQNULL)) {
				return rTNTC(p, c, cdp, data);
			}
			return rT(root, data);
		}	
		
		Node *sc = c;
		Node *sr = lp;
		while(true) {
			if (ISNULL(sr) || (STATUS(sr) == PROMOTE)) {
				break;
			}
			sc = GETADDR(sr);
			sr = GETADDR(sr)->ch[R];
		}
		int *sdp = sc->dp;
		int sd = *(int *)((uintptr_t)sdp & ~0x03);
		if (data != GETDATA(c))
			return false;
		if (STATUS(sr) == MARKED) {	
			markLeft(sc);
			rTN(sc->bl, sc, sd);
			return rT(root, data);
		}
		else if (STATUS(sr) == NORMAL) {
			INV_MSG(data)
			exit(1);
		}
		else if (STATUS(sr) == UNQNULL) {
			if (CAS(&(sc->ch[R]), sr, UNQNULL, c, PROMOTE)) {
				mS_t ls = markLeftPromote(sc);
				if (ls == L_M) {
					// Both Left and Right are marked promote
					CAS(&(c->dp), cdp, MARKED, sdp, NORMAL);
					return rTN(sc->bl, sc, sd);
				}
				else {
					rTN(sc->bl, sc, sd);
					return rT(root, data);
				}
			}
			else {
				return rT(root, data);
			}
		}
		else if (STATUS(sr) == PROMOTE) {
			mS_t ls = markLeftPromote(sc);
			if (ls == L_M) {
				// Both Left and Right are marked promote
				CAS(&(c->dp), cdp, MARKED, sdp, NORMAL);
				return rTN(sc->bl, sc, sd);
			}
			else {
				rTN(sc->bl, sc, sd);
				return rT(root, data);
			}
		}
		else {
			INV_MSG(data)
			exit(1);
		}
	}
	else {
		return rT(root, data);
	}
}

bool rT(Node *startNode, int data) {
	sN *rS = sT(root, root, data);
	Node *p = rS->p;
	int pD = GETDATA(p);
	Node *c = rS->c;
	Node *a = rS->aN;
	int aNDP = rS->aND;
	int *adp = a->dp;
	int aNDC = *(int *)((uintptr_t)adp & ~0x03);
	if (aNDP != aNDC) {
		return rT(root, data);
	}
	else if (ISNULL(c)) {
//		Node *ca = GETADDR(c);
//		std::cout<<p->ch[R]<<" "<<p->ch[L]<<std::endl;
//		std::cout<<c<<std::endl;
//		std::cout<<data<<std::endl;
//		std::cout<<GETDATA(p)<<std::endl;
//		//std::cout<<STATUS(ca->ch[R])<<" "<<STATUS(ca->ch[L])<<std::endl;
//		//std::cout<<(ca->ch[R])<<" "<<(ca->ch[L])<<std::endl;
		return rT(root, data);
		std::cout<<data<<" "<<__LINE__<<std::endl;
		return false;
	}
	else if ((STATUS(c) == PROMOTE) && (p->ch[R] == c)) {
		std::cout<<data<<" "<<__LINE__<<std::endl;
		return false;
	}
	c = GETADDR(c);
	int *cdp = c->dp;
	int cd = *(int *)((uintptr_t)cdp & ~0x03);
	Node *rp = c->ch[R];
	Node *lp = c->ch[L];
	if (data == cd) {
		mS_t stat = mTN(c, rp, lp, cdp, data);
		//std::cout<<__LINE__<<" "<<data<<" : "<<stat<<" "<<STATUS(c->ch[R])<<" "<<STATUS(c->ch[L])<<std::endl;
		//std::cout<<rp<<" "<<lp<<" "<<root<<std::endl;
		cdp = c->dp;
		if (stat == RETRY)
			return rT(root, data);
		else if ((stat == R_0C) || (stat == R_1C)) {
			return rTN(p, c, data);
		}
		else if (stat == R_2C) {
			return rTNTC(p, c, cdp, data);
		}
		else if (stat == R_AN) {
			// This means both left and right childs are marked promote.
			if ((STATUS(c->ch[R]) == PROMOTE) && (STATUS(c->ch[L]) == PROMOTE)) {
					CAS(&(a->dp), adp, MARKED, cdp, MARKED);
					rTN(p, c, data);
					return rT(root, data);
			}
			else {
				//std::cout<<data<<" "<<__LINE__<<" "<<STATUS(c->ch[R])<<" "<<STATUS(c->ch[L])<<std::endl;
				return rT(root, data);
			}
		}
		else if (stat == A_R)
			INV_MSG(data)
		INV_MSG(data)
		return false;
	}
	else {
		//INV_MSG(data)
		return false;
	}
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


bool hSD(int *pdp, Node *an, int *andp) {
	return CAS(&(an->dp), andp, MARKED, pdp, STATUS(pdp));
}

bool iT(Node *startNode, int data) {
	sN* insSeek = sT(startNode, root, data);
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
			if (!ISNULL(c))
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
			return iT(root, data);
		}
		// returning true for now
		return false;
	}
	return true;
}

bool searchT(Node *startNode, int data) {
	sN* serSeek = sT(startNode, root, data);
	Node *aN = serSeek->aN;
	Node *p = serSeek->p;
	Node *c = serSeek->c;
	int aNDP = serSeek->aND;
	int aNDC = GETDATA(aN);
	if (aNDP != aNDC)
		return searchT(aN->bl, data);
	int pD = GETDATA(p);
	if (ISNULL(c) || ((STATUS(c) == PROMOTE)  && (data > pD))) {
		return false;
	}
	int cD = GETDATA(c); 
	if (data == cD) {
		if (STATUS(GETADDR(c)->ch[R]) == PROMOTE) {
			if ((STATUS(GETADDR(c)->ch[L]) == UNQNULL)) 
				return true;
			else if ((STATUS(GETADDR(c)->ch[L]) == MARKED) || (STATUS(GETADDR(c)->ch[L]) == PROMOTE))  {
				if (data == GETDATA(aN) && (STATUS(aN->dp) != MARKED))
					return true;
				return false;
			}
		}
		else if (ISMARKED(c)) {
			return false;
		}
		return true;
	}
	else {
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

void printTreeRemove(Node *pred, Node *node) {
	if (ISNULL(node) || (STATUS(node) == PROMOTE))
		return;
	printTreeRemove(node, GETADDR(node)->ch[L]);
	//if ((STATUS(GETADDR(node)->ch[L]) != MARKED) && (STATUS(GETADDR(node)->ch[R]) != MARKED)) {
	//	if (STATUS(GETADDR(node)->dp) == NORMAL ) {
			std::cout<<"[VARUN] : "<<GETDATA(node)<<" ";
			std::cout<<(STATUS(GETADDR(node)->ch[L]) )<<" " <<(STATUS(GETADDR(node)->ch[R]) )<<" "<<(STATUS(GETADDR(node)->dp) )<<std::endl;
			std::cout<<(STATUS(GETADDR((GETADDR(node)->ch[R]))->ch[R]))<<" " <<(STATUS(GETADDR((GETADDR(node)->ch[R]))->ch[L]))<<" "<<((STATUS(GETADDR((GETADDR(node)->ch[R]))->dp)))<<std::endl;
			std::cout<<pred<<" "<<pred->ch[R]<<" "<<pred->ch[L]<<std::endl;
			std::cout<<node<<" "<<node->ch[R]<<" "<<node->ch[L]<<std::endl;
	//	}
//		std::cout<<" "<<GETDATA(node)<<" ";
//		std::cout<<(STATUS(GETADDR(node)->ch[L]) )<<" " <<(STATUS(GETADDR(node)->ch[R]) )<<" "<<(STATUS(GETADDR(node)->dp) )<<std::endl;
	//}
	printTreeRemove(node, GETADDR(node)->ch[R]);
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
	//printTree(root->ch[L]);
	
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
			searchT(root, key);
		}
		else if ((chooseOperation < insertPer)) {
			iT(root, key);
		}
		else if (chooseOperation < removePer) {
			rT(root, key);
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
			searchT(root, key);
		}
		else if ((chooseOperation < insertPer)) {
			tData->insertCount++;
			iT(root, key);
		}
		else if (chooseOperation < removePer) {
			tData->deleteCount++;
			rT(root, key);
		}
	}
	return NULL;
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

int main(int argc, char **argv) {
	/* This code is for synchrobench. Here it starts *
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
	pthread_attr_t attr;
	barrier_t barrier;
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
		if (iT(root, val)) {
		}
			i++;
	}
	
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
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

    pthread_attr_destroy(&attr);
	struct timespec transientTime;	
	transientTime.tv_sec=0;
	transientTime.tv_nsec=2000000;
	std::cout<<"STARTING..."<<std::endl;
	__atomic_store_n(&start, true, 0);
	//nanosleep(&transientTime, NULL);
	__atomic_store_n(&steadyState, true, 0);
	//nanosleep(&timeout, NULL);
	sleep(duration);
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
	/* This code is for synchrobench. Here it ends*/
	//isValidTree(root);
	//double MOPS = (totalOperations/(duration*1000000.0));
	//std::cout<<"Through Put is : "<<MOPS<<std::endl;
	testbenchParallel();
	return 0;
}
