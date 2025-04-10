#include "threadpool.h"
#include<functional>
#include<iostream>
#include<thread>

const int TASK_MAX_THRESHOLD = INT32_MAX;
const int THREAD_MAX_THRESHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 10; //单位：秒

//线程池构造
ThreadPool::ThreadPool()
	: initThreadSize_(0)
	, idleThreadSize_(0)
	, threadSizeThreshold_(THREAD_MAX_THRESHOLD)
	, curThreadSize_(0)
	, taskSize_(0)
	, taskQueThreshold_(TASK_MAX_THRESHOLD)
	, poolMode_(PoolMode::MODE_FIXED)
	, isPoolRunning_(false)
{}

//线程的析构
ThreadPool::~ThreadPool()
{
	isPoolRunning_ = false;

	//等待线程池中所有线程返回
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty_.notify_all();
	exitCond_.wait(lock, [&]()->bool
		{
			return threads_.size() == 0;
		});
}


//设置线程池的工作模式
void ThreadPool::setMode(PoolMode mode)
{
	if (isPoolRunning_)
	{
		return;
	}
	poolMode_ = mode;
}

//设置task任务队列上限阈值
void ThreadPool::setTaskQueThreshold(int threshold)
{
	if (isPoolRunning_)
	{
		return;
	}
	taskQueThreshold_ = threshold;
}

//设置cached模式下 线程数量上限阈值
void ThreadPool::setThreadSizeThreshold(int threshold)
{
	if (isPoolRunning_)
	{
		return;
	}
	if (poolMode_ == PoolMode::MODE_CACHED)
	{
		threadSizeThreshold_ = threshold;
	}
}

//给线程池提交任务
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	//获取锁
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	//线程通信 等待任务队列有空余  wait    wait_for    wait_until
	//用户提交任务最长不能阻塞超过一秒 否则判断提交任务失败，返回
	if (!notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]() -> bool 
		{
			return taskQue_.size() < taskQueThreshold_;
		}))
	{
		//表示notFull等待1s，条件依然没有满足
		std::cerr << "task queue is full, submit task fail" << std::endl;
		
		return Result(sp, false);//Task Result
	}

	//如果有空余，把线程放入任务队列中
	taskQue_.emplace(sp);
	taskSize_++;

	//在notEmpty上通知
	notEmpty_.notify_all();

	//cached模式下，线程扩充
	if (poolMode_         == PoolMode::MODE_CACHED
		&& taskSize_      >  idleThreadSize_
		&& curThreadSize_ <  threadSizeThreshold_)
	{
		std::cout << ">>>> create new thread <<<<" << std::endl;
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));
		//unique_ptr 的左值引用拷贝构造被删除，但是提供了右值引用的拷贝构造
		int threadId = ptr->getID();
		threads_.emplace(threadId, std::move(ptr));
		//threads_.emplace_back(std::move(ptr));
		threads_[threadId]->start();//启动线程

		//修改线程数量的相关变量
		curThreadSize_++;
		idleThreadSize_++;
	}

	//返回任务的result对象
	//return task->getResult(); //这种方法不行，task对象执行完毕会被析构
	return  Result(sp);
}


//开启线程池
void ThreadPool::start(int initThreadSize)
{
	isPoolRunning_ = true;

	//记录线程初始数量
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;

	//创建线程对象
	for (int i = 0; i < initThreadSize_; i++)
	{
		//创建thread线程对象的时候，把线程函数给到thread对象
		//make_unqie 是C++14标准
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc,this,std::placeholders::_1));
		//unique_ptr 的左值引用拷贝构造被删除，但是提供了右值引用的拷贝构造
		int threadId = ptr->getID();
		threads_.emplace(threadId, std::move(ptr));
		//threads_.emplace_back(std::move(ptr));
	}

	//启动所有线程
	for (int i = 0; i < initThreadSize_; i++)
	{
		threads_[i]->start(); //执行线程函数
		idleThreadSize_++; //线程刚开始执行，空闲线程++
	}
}


//定义线程函数 线程池的所有任务队列中消费任务
void ThreadPool::threadFunc(int threadid)
{
	auto lastTime = std::chrono::high_resolution_clock().now();

	//所有任务必须完成，线程池才能回收所有资源
	for(;;)
	{
		std::shared_ptr<Task> task;
		{
			//先获取锁
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid: " << std::this_thread::get_id()
				<< "尝试获取任务" << std::endl;

			//cached 模式下会创建许多线程，空闲线程超过 THREAD_MAX_IDLE_TIME 进行回收
			//超过 initThreadSize_ 的线程都要回收
			
			//每秒返回一次
			//锁 + 双重判断
			while (taskQue_.size() == 0)
			{
				//线程结束，回收线程资源
				if (!isPoolRunning_)
				{
					threads_.erase(threadid);
					std::cout << "thread id:" << std::this_thread::get_id() << " exit" << std::endl;
					exitCond_.notify_all();
					return;//线程函数结束，线程结束
				}

				if (poolMode_ == PoolMode::MODE_CACHED)
				{
					//条件变量 超时返回
					if (std::cv_status::timeout ==
						notEmpty_.wait_for(lock, std::chrono::seconds(1)))
					{
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() > THREAD_MAX_IDLE_TIME
							&& curThreadSize_ > initThreadSize_)
						{
							//开始回收线程
							//记录线程数量的相关变量值的修改
							curThreadSize_--;
							idleThreadSize_--;
							//把线程对象从容器列表中删除
							threads_.erase(threadid);
							//threadid =》 thread 对象 =》 删除

							std::cout << "thread id:" << std::this_thread::get_id() << " exit" << std::endl;
							return;
						}
					}
				}
				else
				{
					//等待notEmpty条件
					notEmpty_.wait(lock);
				}

				
				//if (!isPoolRunning_)
				//{
				//	//把线程对象从容器列表中删除
				//	threads_.erase(threadid);
				//	//threadid =》 thread 对象 =》 删除

				//	std::cout << "thread id:" << std::this_thread::get_id() << " exit" << std::endl;
				//	exitCond_.notify_all();
				//	return;
				//}
			}

			//线程获取到任务
			idleThreadSize_--;

			std::cout << "tid: " << std::this_thread::get_id()
				<< "获取任务成功" << std::endl;

			//取一个任务出来
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			//如果依然有剩余任务，通知其他线程执行任务
			if (taskQue_.size() > 0)
			{
				notEmpty_.notify_all();
			}

			//取出一个任务，通知继续生产任务
			notFull_.notify_all();

		}//释放锁

		//当前线程负责执行任务
		if (task != nullptr)
		{
			task->exec();
			//执行完一个任务进行通知
		}
		
		//任务执行完毕，空闲线程数量++
		idleThreadSize_++;
		lastTime = std::chrono::high_resolution_clock().now();//更新线程执行完任务时间
	}
	
}


//检测线程池是否工作
bool ThreadPool::checkPoolRunning() const
{
	return isPoolRunning_;
}

//////////////// 线程方法实现

int Thread::generateID_ = 0;

//线程构造
Thread::Thread(ThreadFunc func)
	:func_(func)
	,threadId_(generateID_++)
{
}
// 线程析构
Thread::~Thread()
{

}

//启动线程
void Thread::start()
{
	//创建一个线程 执行一个线程函数
	std::thread t(func_,threadId_); // C++11 线程对象t 和线程对象函数func
	t.detach(); //设置分离线程  pthread_detach  pthread_t设置成分离线程
}

//获取线程ID
int Thread::getID()
{
	return threadId_;
}

////////////////Task方法实现
Task::Task(Result* result)
	:result_(result)
{}

void Task::setResult(Result* res)
{
	result_ = res;
}

void Task::exec()
{
	if (result_ != nullptr)
	{
		result_->setVal(run());
	}
	
}

/////////////////Result 方法的实现
Result::Result(std::shared_ptr<Task> task, bool isValid)
	:task_(task)
	,isValid_(isValid)
{
	task_->setResult(this);
}


//setVal方法，获取任务的返回值
void Result::setVal(Any any) {
	any_ = std::move(any);//右值赋值运算符重载
	sem_.post();//发送信号
}

//getVal方法，用户调用这个方法拿到task返回值
Any Result::get()
{
	//先检查是否有效
	if (!isValid_)
	{
		return "";
	}
	//等待信号量
	sem_.wait();
	return std::move(any_);//只有右值拷贝构造
}