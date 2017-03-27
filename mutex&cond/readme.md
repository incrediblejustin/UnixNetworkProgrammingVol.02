[TOC]

# Chapter 7. 互斥锁和条件变量

## 7.1 概述

#### 7.1.1 互斥的要求

1. 必须强制实施互斥：在具有关于相同资源或者共享对象的临界区的所有进程中，一次只允许一个进程进入临界区
2. 一个在非临界区的进程必须不干涉其他进程
3. 据不允许出现一个需要访问临界区的进程被无限期延迟，即不会死锁或者饥饿
4. 当没有进程在临界区中时，任何需要进入临界区的进程都不许能够立即进入
5. 对相关进程的速度和处理器的数目不能做出任何假设
6. 一个进程在临界区的时间必须是有限的

#### 7.1.2 互斥锁的硬件实现 

1. 中断禁用
	
	- *单处理器机器中，并发进程不能重叠，只能交替。进程将一直运行，直到**调用一个系统调用**或者**被中断***
	- 想要保证互斥，秩序保证一个进程在执行时不被中断就行了，此方法代价高，但保证互斥，例如：
	```cpp
	while(true)
	{
		//禁用中断
		//临界区
		//启用中断
		//其他部分
	}
	```
	- 当处理器多于一个时，就有可能并行执行一个程序，在这种情况下，禁用中断不能保证互斥（处理器间共享一个公共主存，其行为对等关系，处理器之间没有支持互斥的中断机制）

2. 专用机器指令

	- 在硬件级别上，对存储器单元的访问排斥对相同单元的其他访问。基于这一点，有两个机器指令被提出，
		- **testset 指令**
		- **exchange 指令**

	- **testset 指令**
		- testset 指令是一个原子操作，他的执行过程不会被中断
		- bolt是一个共享变量，`while(!testset(bolt)) {}`，如果指令以true返回,那么可以进入临界区，否则进入忙等状态，来持续检查指令结果，**在退出临界区时应将共享变量置 0**
		
	- **exchange 指令**
		- exchange 指令是一个原子操作，他执行的过程不会被中断
		- bolt是一个共享变量，key值为 1 ，exchange指令交换bolt 与 key的值，`while(key != 0){\*临界区*\}`，否则进入忙等待状态，来持续检查指令结果，**在退出临界区时应再次交换两个变量的值，使得bolt置 0**

	- 机器指令的特点：
		- **有点：** 适用于多处理器， 简单易于证明， 支持多个临界区
		- **缺点：** **使用忙等待，可能饥饿， 可能死锁**
	

## 7.2 互斥锁：上锁和解锁

*保证任何时刻只有一个进程或者线程在执行临界区（访问临界资源）的代码*

#### 7.2.1 互斥锁的初始化
*互斥锁被声明为具有pthread_mutex_t数据类型的变量*

1. 互斥锁变量是静态分配的, 默认初始化
	- `static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;`

2. 互斥锁变量时动态分配的
 	- `int pthread_mutex_init(pthread_mutex_t *restrict mutex,
         const pthread_mutexattr_t *restrict attr);`
         
#### 7.2.2 互斥锁的上锁和解锁

```cpp
	#include <pthread.h>
	int pthread_mutex_lock(pthread_mutex_t *mutex);
	int pthread_mutex_trylock(pthread_mutex_t *mutex);
	int pthread_mutex_unlock(pthread_mutex_t *mutex);

	//成功则返回 0 ， 否则返回正的错误值
```

- `**_lock`函数，当互斥锁已经被另一个线程上锁，当前线程会阻塞直到互斥锁解锁为止
- `**_trylock`函数，是`**_lock`函数的非阻塞版本，当互斥锁已经被另一个线程上锁，当前线程会返回`EBUSY`错误


## 7.3 条件变量：等待与信号发送
*互斥锁用于对临界资源上锁，条件变量则用于等待从而被退出临界资源的线程唤醒*

#### 7.3.1 条件变量的初始化
*条件变量被声明为具有pthread_cond_t数据类型的变量*

1. 条件变量是静态分配的, 默认初始化
	- `static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;`

2. 条件变量时动态分配的
 	- `int pthread_cond_init(pthread_cond_t *cptr, const phtread_condattr_t *attr);`

 	
#### 7.3.2 等待与通知函数

```cpp
#include <pthread.h>
int pthread_cond_wait(pthread_cond_t *restrict cond, pthread_mutex_t *restrict mutex);
int pthread_cond_signal(pthread_cond_t *restrict cond);

//signal 一词指的 不是 unix SIGxxx 的信号
```

- 等 待 函 数
	- 条件变量总是有一个互斥锁与之关联
	- 在调用`pthread_cond_wait`函数时，需要指定其**条件变量的地址**和**所关联的互斥锁的地址**
	- 使用条件变量的等待函数，执行以下几个步骤：
		1. 在进入等待函数之前，用户应给给与之关联的互斥锁**加锁**
		2. 如果条件变量是一个临界资源，需要先给他上锁
		3. 给传入的互斥锁解锁，如果解锁失败，给条件变量解锁并返回错误，否则等待被唤醒的队列个数 + 1，并将该线程push到等待的队列中
		4. 记录 **已经被唤醒的线程个数**、**用来监控是否是被broadcast唤醒该线程的值**
		5. 然后给条件变量的锁解锁，以便于其他线程可以使用该条件变量
		6. 睡眠等待被唤醒
		7. 唤醒后，给条件变量加锁，
			- 检查条件变量内部的broadcast值是否与之前第四步记录的相等，如果是表示是被broadcast唤醒，执行唤醒所有的线程
			- 否则检查是否是被signal唤醒
			- 如果以上都不是继续执行第5步
		8. 执行唤醒操作，对以唤醒的线程个数+1，再对条件变量的锁解锁
		9. 将被唤醒的进程pop出队列，在对互斥锁加锁
	- 等待函数发送信号的伪代码如下：

	
	```cpp
	//-------------用户使用这些代码来等待在条件变量成立后被唤醒--------
	pthread_mutex_lock(mutex) // step 1
	while（条件为假）
		pthread_cond_wait(cond, mutex);
	pthread_mutex_unlock(mutex);
	//------------------pthread_cond_wait函数内部----------------
	mutex = "被传入的锁用来保护队列的操作"；
	condlock  = "保护条件变量的锁";
	lock(condlock); // step 2
	unlock(mutex);  // step 3
	total_seq += 1;
	push(cond, mutex);
	val = seq = wakeup_seq; // step 4
	bc_seq = broadcast_seq; 
	unlock(condlock); // step 5
	wait();           // step 6
	lock(condlock);   // step 7
	bc_seq != broadcast_seq;  goto bc_out;// step 7.1
	val = wakeup_seq;
	val != seq ; // step 7.2
	goto step 5; // step 7.3
	wakeup_seq++;     // step 8
	wake(1);
	unlock(condlock); 
	pop(cond, mutex); // step 9
	lock(mutex);      
	```
- 唤 醒 函 数
	- 一般在唤醒线程前后是要加一个互斥锁的
	- 执行唤醒函数(signal)一般包含以下几个操作：

	
		1. 给条件变量加锁，保证他的修改是正确的
		2. 如果线程在等待被唤醒，即条件变量的所有等待的线程个数 > 已经唤醒的线程个数
		3. 唤醒线程个数+1， 执行唤醒操作
		4. 给条件变量解锁，退出
	- 唤醒函数(signal)发送信号的伪代码如下：

	
	```cpp
	//----------------用户使用这些代码来唤醒一个线程----------------
	//没有解决上锁冲突，有可能在发出唤醒线程后还没有解锁
	pthread_mutex_lock(mutex);// step 1
	//设置条件为真
	pthread_cond_signal(cond);
	pthread_mutex_unlock(mutex);
	//---------------------解决了上锁冲突后-----------------------
	int dosignal;
	pthread_mutex_lock(mutex);
	dosignal = (条件为真）；
	pthread_mutex_unlock(mutex);
	if(dosignal)
		pthread_cond_signal(cond);
	//----------------------signal函数内部----------------------
	condlock = "保护条件变量的锁";
	lock(condlock); // step 1
	if (total_seq > wakeup_seq)  // step 2
	{
		wakeup_seq++;  // step 3
		wake(1);
	}
	unlock(condlock); // step 4
	```

#### 7.3.3 条件变量：定时等待与广播

```cpp
#include <pthread.h>
int pthread_cond_broadcast(pthread_cond_t *cptr);
int pthread_cond_timewait(pthread_cond_t *cptr, pthread_mutex_t *mptr,
								const struct timespec *abstime);
```

- 广播唤醒函数（`pthread_cond_broadcast`）


	- 该函数的主要功能是如果一个线程认定有多个其他线程应该被唤醒，这时调用该函数来唤醒所有阻塞在该条件变量上的线程
	- 该函数主要执行以下几个步骤：

	
		1. 为条件变量加锁，以保护其正确性
		2. 如果有等待在条件变量上的进程，即等待的进程数(`total_seq`) > 已经唤醒的进程数(`wakeup_seq`)
		3. 是唤醒的先传给你数等于所有线程数，wakeup_seq = total_seq
		4. 给记录broadcast的整数值+1，当wait函数返回时首先检查这个值，用来判断是否是被broadcast唤醒
		5. 唤醒所有(`INT_MAX`个)线程
		6. 给条件变量解锁

- 定时等待函数


	- 定时等待函数的时间参数是一个绝对时间，即该函数必须返回时的系统时间（与select等函数不同）
	- 这样做的好处在于：如果函数过早返回，可以在不调整参数的时候再次调用该函数
	- 如果超时，该函数返回`ETIMEOUT`错误


## 7.4 互斥锁与条件变量的属性

- 通常使用`pthread_mutex_init`和`pthread_cond_init`来初始化互斥锁和条件变量
- 用来初始化这两个类型变量的属性的分别是`pthread_mutexattr_t`和`pthread_condattr_t`类型的参数
- 用来初始化这两个类型的参数的函数分别是分别是:


	- `int pthread_mutexattr_init(pthread_mutexattr_t *attr);`
	- `int pthread_condattr_init(pthread_condattr_t *attr);`
- 对属性操作的函数有四个：

	- `int pthread_mutexattr_getshared(const pthread_mutexattr_t *attr, int *value);`
	- `int pthread_mutexattr_setshared(pthread_mutexattr_t *attr, int value);`
	- `int pthread_condattr_getshared(const pthread_condattr_t *attr, int *value);`
	- `int pthread_condattr_setshared(pthread_condattr_t *attr, int value);`
	- *value 的值可以是`PTHREAD_PROCESS_SHARED`或者是`PTHREAD_PROCESS_PRIVATE`, 用来表示该互斥锁或者条件变量是否可以在进程间共享的属性*
- **设置属性举例：**

```cpp
pthread_mutex_t mutex; // 定义一个互斥锁变量
pthread_mutexattr_t mattr; //定义一个互斥锁属性变量
pthread_mutexattr_init(&mattr); //初始化互斥锁属性变量
pthread_mutexattr_setshared(&mattr, 
                  PTHREAD_PROCESS_SHARED); //给属性设置可共享
pthread_mutexattr_init(&mutex, &mattr); //初始化互斥锁变量
```




