#ifndef THREADPOOL_H
#define THREAFPOOL_H

#include<vector>
#include<queue>
#include<memory>
#include<atomic>
#include<thread>
#include<mutex>
#include<condition_variable>
#include<functional>
#include<unordered_map>


//Any类型：可以接收任意类型的数据
class Any
{
public:
	Any() = default;
	~Any() = default;

	//unique_ptr删除了左值引用的拷贝构造和赋值运算符重载
	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;

	//unique_ptr有右值引用的拷贝构造和赋值运算符重载
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	//这个构造函数可以让Any类型接收任意的其他数据
	template<typename T>
	Any(T data): base_(std::make_unique<Derive<T>>(data))
	{}

	//这个方法把Any对象中的data数据提取出来
	template<typename T>
	T cast_()
	{
		//从base_找到他所指向的Derive对象类型，从它里面取出data的值
		//基类指针转为派生类指针 dynamic_cast 具有RTTI识别
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
		if (pd == nullptr)
		{
			throw "type is unmatch";
		}
		return pd->data_;
	}
private:
	// 基类类型
	class Base
	{
	public:
		virtual ~Base() = default;
	};

	//派生类类型
	template<typename T>
	class Derive :public Base
	{
	public:
		Derive(T data) :data_(data)
		{}
		T data_;//保存了任意的其他类型
	};

private:
	//定义一个基类的指针
	std::unique_ptr<Base> base_;
};

//实现信号量类
class Semaphore
{
public:
	Semaphore(int limit = 0) 
		:resLimit_(limit)
	{}
	~Semaphore() = default;

	//获取一个信号量
	void wait()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		//等待信号量有资源，没有资源的话，阻塞当前线程
		cond_.wait(lock, [&]()->bool
			{
				return resLimit_ > 0;
			});
		resLimit_--;
	}

	//增加一个信号量
	void post()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		resLimit_++;
		cond_.notify_all();
	}

private:
	int resLimit_;
	std::mutex mtx_;
	std::condition_variable cond_;
};


class Task;//task前置声明

//实现接收提交到线程池的task任务执行完毕后的返回值类型 Result
class Result
{
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);
	~Result() = default;

	//setVal方法，获取任务的返回值
	void setVal(Any any);
	//getVal方法，用户调用这个方法拿到task返回值
	Any get();
private:
	Any any_; //存储任务的返回值
	Semaphore sem_; //线程通信信号量
	std::shared_ptr<Task> task_; // 指向对应获取返回值的任务对象
	std::atomic_bool isValid_; //返回对象是否有效
};

//任务抽象基类
class Task
{
public:
	Task(Result* result = nullptr);
	~Task() = default;
	void setResult(Result* res);
	void exec();
	//用户可以自定义任意类型，从Task继承，重写run方法，实现自定义任务处理
	virtual Any run() = 0;

private:
	//此处不可使用shared_ptr，否则会循环引用导致内存泄漏，用普通指针即可
	//因为Result的声明周期肯定比task长，result需要等线程执行完任务之后才可以拿到结果
	//而task对象在向线程池提交任务完成之后就会释放
	Result* result_;
};



//线程池的两种模式
enum class PoolMode
{
	MODE_FIXED, //固定数量的线程
	MODE_CACHED, //线程数量可动态增长
};

//线程类型
class Thread
{
public:
	using ThreadFunc = std::function<void(int)>;

	//线程构造
	Thread(ThreadFunc func);
	// 线程析构
	~Thread();
	//启动线程
	void start();

	//获取线程ID
	int getID();
private:
	ThreadFunc func_;
	static int generateID_;
	int threadId_;
};
/*

example：

ThreadPool pool;
pool.start(4);

class MyTask : public Task
{
public:
	void run() override
	{
		//代码
	}
};

pool.submit(std::make_shared<MyTask>());

*/



//线程池类型          线程池工作之后不允许修改配置
class ThreadPool
{
public:
	//构造
	ThreadPool();
	//析构
	~ThreadPool();

	//设置工作模式
	void setMode(PoolMode poolMode);

	//设置task任务队列上限阈值
	void setTaskQueThreshold(int threshHold);
	
	//设置cached模式下 线程数量上限阈值
	void setThreadSizeThreshold(int threshHold);

	//给线程池提交任务
	Result submitTask(std::shared_ptr<Task> sp);

	//开启线程池
	void start(int initThreadSize = 4);

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	//定义线程函数
	void threadFunc(int threadid);

	//检测线程池是否工作
	bool checkPoolRunning() const;
	 
private:
	//std::vector<std::unique_ptr<Thread>> threads_; 
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;//线程列表
	int initThreadSize_; //初始的线程数量
	std::atomic_int curThreadSize_; //记录当前线程池的线程数量
	int threadSizeThreshold_;//最大线程数量
	std::atomic_int idleThreadSize_; //记录当前空闲线程数量

	std::queue<std::shared_ptr<Task>> taskQue_; //任务队列，此处使用智能指针为了防止任务对象被析构，导致任务无法执行
	std::atomic_int taskSize_; //任务的数量
	size_t taskQueThreshold_; //任务队列数量上限阈值

	std::mutex taskQueMtx_; //保证任务队列的线程安全
	std::condition_variable notFull_; //表示任务队列不满
	std::condition_variable notEmpty_; //表示任务队列不空
	std::condition_variable exitCond_; //等待资源全部回收


	PoolMode poolMode_; //当前线程池的工作模式
	std::atomic_bool isPoolRunning_;//当前线程池是否开始工作
	
};

#endif