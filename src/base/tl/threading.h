#ifndef BASE_TL_THREADING_H
#define BASE_TL_THREADING_H

#include "../system.h"

class CSemaphore
{
	SEMAPHORE m_Sem;

public:
	CSemaphore() { sphore_init(&m_Sem); }
	~CSemaphore() { sphore_destroy(&m_Sem); }
	CSemaphore(const CSemaphore &) = delete;
	void Wait() { sphore_wait(&m_Sem); }
	void Signal() { sphore_signal(&m_Sem); }
};

#endif // BASE_TL_THREADING_H
