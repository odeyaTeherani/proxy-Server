#include "threadpool.h"
#include <stdlib.h>
#include <stdio.h>
void error(char *err);
void destroyAndNullify(void *x);
void destroy_error(char *err, threadpool *pool);
void insert_work(threadpool *pool, work_t *work);
work_t *dequeue(threadpool *pool);

// explenation on what pthread_cond_wait does "behind the scene"
// https://stackoverflow.com/questions/14924469/does-pthread-cond-waitcond-t-mutex-unlock-and-then-lock-the-mutex
// https://docs.oracle.com/cd/E19683-01/806-6867/sync-20196/index.html
// https://linux.die.net/man/3/pthread_cond_wait
///////////////////////////////  Helper functions /////////////////////////////////////

/* Error- print an error meesage to stderr and exit with EXIT_FAILURE code  */
void error(char *err)
{
	perror(err);
	exit(EXIT_FAILURE);
}

/* Destroy And Nullify- free memory and set pointer to Null for safety and robustness */
void destroyAndNullify(void *x)
{
	free(x);
	x = NULL;
}

/*  Destroy Error - clean memory of threadpool and send error message */
// err - a string describing the system call which failed
void destroy_error(char *err, threadpool *pool)
{

	if (pool != NULL)
	{

		//destroy all the dynamic pthread attributes objects (cond and mutex)
		pthread_cond_destroy(&pool->q_not_empty);
		pthread_cond_destroy(&pool->q_empty);
		pthread_mutex_destroy(&pool->qlock);

		if (pool->threads != NULL)
			destroyAndNullify(pool->threads); // TODO: Shall we bother freeing each of the work_t ourself?
																				//	or does it automatically handled by pthread_join we already did?

		//free the outer threadpool (AbstractDataType) object
		destroyAndNullify(pool);
	}

	error(err);
}

//////////////////////////////////////////////////////////////////////////////////

///////////////////////////////  Main original functions //////////////////////////////////

//////////// "private functions" for inner usage //////////////////

/* Do Work - The work function of the thread */
void *do_work(void *p)
{

	if (p == NULL)
		return NULL;

	// every "work"/"task" function in pthreads should get and return void*
	// so we need to use casting
	threadpool *pool = (threadpool *)p;

	work_t *work;

	//infinite loop, only an error/shutdown signal will finish it
	while (1)
	{

		// 1. lock the mutex (which protects the whole pool structure)
		if ((pthread_mutex_lock(&pool->qlock)) != 0)	 //try lock (blocking call, has timeout)
			destroy_error("pthread_mutex_lock\n", pool); //if failed

		// 	If the queue becomes empty and destruction process wait to begin, send a "green light"
		if (pool->qsize == 0 && pool->dont_accept == 1)
			/* Wake up one (or at least one) thread waiting for condition variable */
			if ((pthread_cond_signal(&pool->q_empty)) != 0) //if signaling failed
				destroy_error("pthread_cond_signal\n", pool);

		// 	If destruction process has begun, exit thread
		if (pool->shutdown == 1)
		{

			if ((pthread_mutex_unlock(&pool->qlock)) != 0)	 // try unlock
				destroy_error("pthread_mutex_unlock\n", pool); //failure

			return NULL;
		}

		// 2. if the queue is empty, wait ((no job to do)
		if (pool->qsize == 0) // q_empty
			// Wait for condition variable q_not_empty to be signaled or broadcast.
			// MUTEX qlock is assumed to be locked before.
			if ((pthread_cond_wait(&(pool->q_not_empty), &(pool->qlock))) != 0)
				destroy_error("pthread_cond_wait\n", pool);

		// queue isn't empty now, Check again destruction flag.
		if (pool->shutdown == 1)
		{

			if ((pthread_mutex_unlock(&pool->qlock)) != 0) // try unlock
				destroy_error("pthread_mutex_unlock\n", pool);

			return NULL;
		}

		// 3. take the first element from the queue (*work_t)
		// get the head (*work_t)
		work = pool->qhead;
		if (work != NULL)
		{

			// remove the element from the queue and update the pool structure
			work = dequeue(pool);

			// 4. unlock mutex (done updating the pool)
			if ((pthread_mutex_unlock(&pool->qlock)) != 0) // try unlock
				destroy_error("pthread_mutex_unlock\n", pool);

			// 5. call the thread routine
			work->routine(work->arg);

			destroyAndNullify(work);
		}
		else
		{
			//no (valid) work to be done, try to release the pool lock
			if ((pthread_mutex_unlock(&pool->qlock)) != 0)
				destroy_error("pthread_mutex_unlock\n", pool); //failure
		}
	}
}

/* Dequeue - dequeue from job queue */
work_t *dequeue(threadpool *pool)
{

	if (pool->qhead == NULL)
		return NULL;

	work_t *first = pool->qhead;
	pool->qhead = pool->qhead->next;
	pool->qsize--;

	if (pool->qhead == NULL)
		pool->qtail = pool->qhead;

	return first;
}

/*  Insert Work- insert a job to queue  */
void insert_work(threadpool *pool, work_t *work)
{

	// we are adding at the end of queue
	work->next = NULL; // this (work) will be the last element

	if (pool->qsize == 0)
		pool->qhead = work; // head and tail will be the same
	else
		pool->qtail->next = work; // chain it to the end of queue

	pool->qtail = work; // update the pointer,
	pool->qsize++;
}

/////////////////////////////////////////////////////////////

//////////// "public functions" for outer usage (global) //////////////////

/////////////
/*Create TreadPool- creates a fixed-sized threads pool */
threadpool *create_threadpool(int num_threads_in_pool)
{

	//1. Check the legacy of the parameter.

	if (num_threads_in_pool < 1 || num_threads_in_pool > MAXT_IN_POOL)
		return NULL; // invalid input, not a syscall error => no need to call perr

	//2. Create threadpool structure and initialize it:
	threadpool *pool = (threadpool *)malloc(sizeof(threadpool));
	if (pool == NULL)
		error("malloc\n");

	pool->num_threads = num_threads_in_pool;
	pool->qsize = 0;

	pool->threads = (pthread_t *)malloc(num_threads_in_pool * sizeof(pthread_t));
	if (pool->threads == NULL)
	{
		destroyAndNullify(pool); //restore back to before threadpool was allocated (free)
		error("malloc\n");
	}

	pool->qhead = pool->qtail = NULL;

	//		e. Init lock and condition variables.

	// second parameter - __mutexattr is for extra functionality which we don't need, set to NULL
	//  If successful, the pthread_mutex_init() and pthread_mutex_destroy() functions return 0.
	// Otherwise, an error number is returned to indicate the error.
	if ((pthread_mutex_init(&(pool->qlock), NULL)) != 0)
		destroy_error("pthread_mutex_init\n", pool);

	if ((pthread_cond_init(&(pool->q_not_empty), NULL)) != 0)
		destroy_error("pthread_cond_init\n", pool);

	if ((pthread_cond_init(&(pool->q_empty), NULL)) != 0)
		destroy_error("pthread_cond_init\n", pool);

	pool->shutdown = 0;
	pool->dont_accept = 0;

	// g. Create the threads with do_work as execution function and the pool as an argument.
	/* Create a new thread, starting with execution of do_work getting passed the threadpool as an argument.
		The new pthread	handle is stored in the pool's pthread array (we send the address of each cell in the array
		pool-> threads+i  is equal to &(pool-> threads[i]) 
		NOTE: do_work is a pointer to a global function (could be static if we could change the header file) 
               3rd parameter : void *(*__start_routine) (void *)		*/
	for (int i = 0; i < num_threads_in_pool; i++)
		if ((pthread_create(pool->threads + i, NULL, do_work, pool)) != 0)
			destroy_error("pthread_create\n", pool); //failure

	return pool;
}

/* Dispatch- dispatch enter a "job" of type work_t into the queue */
void dispatch(threadpool *from_me, dispatch_fn dispatch_to_here, void *arg)
{

	if (from_me == NULL || dispatch_to_here == NULL)
		return;

	// If destroy function has begun, don't accept new item to the queue
	if (from_me->dont_accept == 1)
		return; //not allowed to dispatch a job at this time.

	// Create work_t structure and init it with the routine and argument.
	work_t *work = (work_t *)malloc(sizeof(work_t));
	if (work == NULL)
		destroy_error("malloc\n", from_me);

	work->arg = arg;									// argument to the routine
	work->routine = dispatch_to_here; //copy the function pointer

	if ((pthread_mutex_lock(&from_me->qlock)) != 0)		//try to lock
		destroy_error("pthread_mutex_lock\n", from_me); //failure

	// Add item to the queue
	insert_work(from_me, work);

	// When there will be an available thread,
	// it will takes a job from the queue and run the negotiation function.
	if ((pthread_cond_signal(&from_me->q_not_empty)) != 0) // work queue is not empty now, wake up a waiting thread
		destroy_error("pthread_cond_signal\n", from_me);

	if ((pthread_mutex_unlock(&from_me->qlock)) != 0) //try unlock
		destroy_error("pthread_mutex_unlock\n", from_me);
}

/* Destroy ThreadPool - kills the threadpool */
void destroy_threadpool(threadpool *destroyme)
{

	if (destroyme == NULL)
		return;

	destroyme->dont_accept = 1;

	if ((pthread_mutex_lock(&destroyme->qlock)) != 0) //try lock
		destroy_error("pthread_mutex_lock\n", destroyme);

	//Wait for queue to become empty (if not empty)
	if (destroyme->qsize != 0)
		if ((pthread_cond_wait(&destroyme->q_empty, &destroyme->qlock)) != 0)
			destroy_error("pthread_cond_wait\n", destroyme);

	destroyme->shutdown = 1;

	// Signal (all) threads that wait on empty queue, so they can wake up, see shutdown flag and exit.
	if ((pthread_cond_broadcast(&destroyme->q_not_empty)) != 0)
		destroy_error("pthread_cond_broadcast\n", destroyme);

	if ((pthread_mutex_unlock(&destroyme->qlock)) != 0) //unlock
		destroy_error("pthread_mutex_unlock\n", destroyme);

	// Join all threads (wait for them to finish and get to this line)
	for (int i = 0; i < destroyme->num_threads; i++)
		if ((pthread_join(destroyme->threads[i], NULL)) != 0)
			destroy_error("pthread_join\n", destroyme);

	// Free whatever you have to free.
	// TODO: can do code re-use for this (destroy_error)
	pthread_cond_destroy(&destroyme->q_not_empty);
	pthread_cond_destroy(&destroyme->q_empty);
	pthread_mutex_destroy(&destroyme->qlock);
	destroyAndNullify(destroyme->threads);
	destroyAndNullify(destroyme);
}

////////////////////////////////////////////////////////////////
