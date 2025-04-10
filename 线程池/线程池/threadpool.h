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


//Any���ͣ����Խ����������͵�����
class Any
{
public:
	Any() = default;
	~Any() = default;

	//unique_ptrɾ������ֵ���õĿ�������͸�ֵ���������
	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;

	//unique_ptr����ֵ���õĿ�������͸�ֵ���������
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	//������캯��������Any���ͽ����������������
	template<typename T>
	Any(T data): base_(std::make_unique<Derive<T>>(data))
	{}

	//���������Any�����е�data������ȡ����
	template<typename T>
	T cast_()
	{
		//��base_�ҵ�����ָ���Derive�������ͣ���������ȡ��data��ֵ
		//����ָ��תΪ������ָ�� dynamic_cast ����RTTIʶ��
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
		if (pd == nullptr)
		{
			throw "type is unmatch";
		}
		return pd->data_;
	}
private:
	// ��������
	class Base
	{
	public:
		virtual ~Base() = default;
	};

	//����������
	template<typename T>
	class Derive :public Base
	{
	public:
		Derive(T data) :data_(data)
		{}
		T data_;//�������������������
	};

private:
	//����һ�������ָ��
	std::unique_ptr<Base> base_;
};

//ʵ���ź�����
class Semaphore
{
public:
	Semaphore(int limit = 0) 
		:resLimit_(limit)
	{}
	~Semaphore() = default;

	//��ȡһ���ź���
	void wait()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		//�ȴ��ź�������Դ��û����Դ�Ļ���������ǰ�߳�
		cond_.wait(lock, [&]()->bool
			{
				return resLimit_ > 0;
			});
		resLimit_--;
	}

	//����һ���ź���
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


class Task;//taskǰ������

//ʵ�ֽ����ύ���̳߳ص�task����ִ����Ϻ�ķ���ֵ���� Result
class Result
{
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);
	~Result() = default;

	//setVal��������ȡ����ķ���ֵ
	void setVal(Any any);
	//getVal�������û�������������õ�task����ֵ
	Any get();
private:
	Any any_; //�洢����ķ���ֵ
	Semaphore sem_; //�߳�ͨ���ź���
	std::shared_ptr<Task> task_; // ָ���Ӧ��ȡ����ֵ���������
	std::atomic_bool isValid_; //���ض����Ƿ���Ч
};

//����������
class Task
{
public:
	Task(Result* result = nullptr);
	~Task() = default;
	void setResult(Result* res);
	void exec();
	//�û������Զ����������ͣ���Task�̳У���дrun������ʵ���Զ���������
	virtual Any run() = 0;

private:
	//�˴�����ʹ��shared_ptr�������ѭ�����õ����ڴ�й©������ָͨ�뼴��
	//��ΪResult���������ڿ϶���task����result��Ҫ���߳�ִ��������֮��ſ����õ����
	//��task���������̳߳��ύ�������֮��ͻ��ͷ�
	Result* result_;
};



//�̳߳ص�����ģʽ
enum class PoolMode
{
	MODE_FIXED, //�̶��������߳�
	MODE_CACHED, //�߳������ɶ�̬����
};

//�߳�����
class Thread
{
public:
	using ThreadFunc = std::function<void(int)>;

	//�̹߳���
	Thread(ThreadFunc func);
	// �߳�����
	~Thread();
	//�����߳�
	void start();

	//��ȡ�߳�ID
	int getID();
private:
	ThreadFunc func_;
	static int generateID_;
	int threadId_;
};
/*

example��

ThreadPool pool;
pool.start(4);

class MyTask : public Task
{
public:
	void run() override
	{
		//����
	}
};

pool.submit(std::make_shared<MyTask>());

*/



//�̳߳�����          �̳߳ع���֮�������޸�����
class ThreadPool
{
public:
	//����
	ThreadPool();
	//����
	~ThreadPool();

	//���ù���ģʽ
	void setMode(PoolMode poolMode);

	//����task�������������ֵ
	void setTaskQueThreshold(int threshHold);
	
	//����cachedģʽ�� �߳�����������ֵ
	void setThreadSizeThreshold(int threshHold);

	//���̳߳��ύ����
	Result submitTask(std::shared_ptr<Task> sp);

	//�����̳߳�
	void start(int initThreadSize = 4);

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	//�����̺߳���
	void threadFunc(int threadid);

	//����̳߳��Ƿ���
	bool checkPoolRunning() const;
	 
private:
	//std::vector<std::unique_ptr<Thread>> threads_; 
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;//�߳��б�
	int initThreadSize_; //��ʼ���߳�����
	std::atomic_int curThreadSize_; //��¼��ǰ�̳߳ص��߳�����
	int threadSizeThreshold_;//����߳�����
	std::atomic_int idleThreadSize_; //��¼��ǰ�����߳�����

	std::queue<std::shared_ptr<Task>> taskQue_; //������У��˴�ʹ������ָ��Ϊ�˷�ֹ����������������������޷�ִ��
	std::atomic_int taskSize_; //���������
	size_t taskQueThreshold_; //�����������������ֵ

	std::mutex taskQueMtx_; //��֤������е��̰߳�ȫ
	std::condition_variable notFull_; //��ʾ������в���
	std::condition_variable notEmpty_; //��ʾ������в���
	std::condition_variable exitCond_; //�ȴ���Դȫ������


	PoolMode poolMode_; //��ǰ�̳߳صĹ���ģʽ
	std::atomic_bool isPoolRunning_;//��ǰ�̳߳��Ƿ�ʼ����
	
};

#endif