#define QUIVER_GCRY_THREAD_OPTION_PTHREAD_IMPL					      \
	static int gcry_pthread_mutex_init (void **priv)			      \
{									      \
	  int err = 0;								      \
	  pthread_mutex_t *lock = (pthread_mutex_t*) malloc (sizeof (pthread_mutex_t));		      \
										      \
	  if (!lock)								      \
	    err = ENOMEM;							      \
	  if (!err)								      \
	    {									      \
		          err = pthread_mutex_init (lock, NULL);				      \
		          if (err)								      \
		    	free (lock);							      \
		          else								      \
		    	*priv = lock;							      \
		        }									      \
	  return err;								      \
}									      \
static int gcry_pthread_mutex_destroy (void **lock)			      \
  { int err = pthread_mutex_destroy ((pthread_mutex_t*) *lock);  free (*lock); return err; }     \
static int gcry_pthread_mutex_lock (void **lock)			      \
  { return pthread_mutex_lock ((pthread_mutex_t*) *lock); }				      \
static int gcry_pthread_mutex_unlock (void **lock)			      \
  { return pthread_mutex_unlock ((pthread_mutex_t*) *lock); }				      \
									      \
static struct gcry_thread_cbs gcry_threads_pthread =			      \
{ GCRY_THREAD_OPTION_PTHREAD, NULL,					      \
	  gcry_pthread_mutex_init, gcry_pthread_mutex_destroy,			      \
	  gcry_pthread_mutex_lock, gcry_pthread_mutex_unlock }

