/* File handle stuff. */

#ifndef _FILEHANDLE_H_
#define _FILEHANDLE_H_

struct fh {
	struct vnode* node; /* The vnode this is handling. */
	struct semaphore* mutex; /* overall mutex for this file */
	struct semaphore* rwLock; /* readers-writers lock (for efficiency) */
};

#endif /* _FILEHANDLE_H_ */
