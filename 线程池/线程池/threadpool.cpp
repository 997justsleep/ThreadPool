#include "threadpool.h"
#include<functional>
#include<iostream>
#include<thread>

const int TASK_MAX_THRESHOLD = INT32_MAX;
const int THREAD_MAX_THRESHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 10; //��λ����

//�̳߳ع���
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

//�̵߳�����
ThreadPool::~ThreadPool()
{
	isPoolRunning_ = false;

	//�ȴ��̳߳��������̷߳���
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty_.notify_all();
	exitCond_.wait(lock, [&]()->bool
		{
			return threads_.size() == 0;
		});
}


//�����̳߳صĹ���ģʽ
void ThreadPool::setMode(PoolMode mode)
{
	if (isPoolRunning_)
	{
		return;
	}
	poolMode_ = mode;
}

//����task�������������ֵ
void ThreadPool::setTaskQueThreshold(int threshold)
{
	if (isPoolRunning_)
	{
		return;
	}
	taskQueThreshold_ = threshold;
}

//����cachedģʽ�� �߳�����������ֵ
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

//���̳߳��ύ����
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	//��ȡ��
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	//�߳�ͨ�� �ȴ���������п���  wait    wait_for    wait_until
	//�û��ύ�����������������һ�� �����ж��ύ����ʧ�ܣ�����
	if (!notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]() -> bool 
		{
			return taskQue_.size() < taskQueThreshold_;
		}))
	{
		//��ʾnotFull�ȴ�1s��������Ȼû������
		std::cerr << "task queue is full, submit task fail" << std::endl;
		
		return Result(sp, false);//Task Result
	}

	//����п��࣬���̷߳������������
	taskQue_.emplace(sp);
	taskSize_++;

	//��notEmpty��֪ͨ
	notEmpty_.notify_all();

	//cachedģʽ�£��߳�����
	if (poolMode_         == PoolMode::MODE_CACHED
		&& taskSize_      >  idleThreadSize_
		&& curThreadSize_ <  threadSizeThreshold_)
	{
		std::cout << ">>>> create new thread <<<<" << std::endl;
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));
		//unique_ptr ����ֵ���ÿ������챻ɾ���������ṩ����ֵ���õĿ�������
		int threadId = ptr->getID();
		threads_.emplace(threadId, std::move(ptr));
		//threads_.emplace_back(std::move(ptr));
		threads_[threadId]->start();//�����߳�

		//�޸��߳���������ر���
		curThreadSize_++;
		idleThreadSize_++;
	}

	//���������result����
	//return task->getResult(); //���ַ������У�task����ִ����ϻᱻ����
	return  Result(sp);
}


//�����̳߳�
void ThreadPool::start(int initThreadSize)
{
	isPoolRunning_ = true;

	//��¼�̳߳�ʼ����
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;

	//�����̶߳���
	for (int i = 0; i < initThreadSize_; i++)
	{
		//����thread�̶߳����ʱ�򣬰��̺߳�������thread����
		//make_unqie ��C++14��׼
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc,this,std::placeholders::_1));
		//unique_ptr ����ֵ���ÿ������챻ɾ���������ṩ����ֵ���õĿ�������
		int threadId = ptr->getID();
		threads_.emplace(threadId, std::move(ptr));
		//threads_.emplace_back(std::move(ptr));
	}

	//���������߳�
	for (int i = 0; i < initThreadSize_; i++)
	{
		threads_[i]->start(); //ִ���̺߳���
		idleThreadSize_++; //�̸߳տ�ʼִ�У������߳�++
	}
}


//�����̺߳��� �̳߳ص����������������������
void ThreadPool::threadFunc(int threadid)
{
	auto lastTime = std::chrono::high_resolution_clock().now();

	//�������������ɣ��̳߳ز��ܻ���������Դ
	for(;;)
	{
		std::shared_ptr<Task> task;
		{
			//�Ȼ�ȡ��
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid: " << std::this_thread::get_id()
				<< "���Ի�ȡ����" << std::endl;

			//cached ģʽ�»ᴴ������̣߳������̳߳��� THREAD_MAX_IDLE_TIME ���л���
			//���� initThreadSize_ ���̶߳�Ҫ����
			
			//ÿ�뷵��һ��
			//�� + ˫���ж�
			while (taskQue_.size() == 0)
			{
				//�߳̽����������߳���Դ
				if (!isPoolRunning_)
				{
					threads_.erase(threadid);
					std::cout << "thread id:" << std::this_thread::get_id() << " exit" << std::endl;
					exitCond_.notify_all();
					return;//�̺߳����������߳̽���
				}

				if (poolMode_ == PoolMode::MODE_CACHED)
				{
					//�������� ��ʱ����
					if (std::cv_status::timeout ==
						notEmpty_.wait_for(lock, std::chrono::seconds(1)))
					{
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() > THREAD_MAX_IDLE_TIME
							&& curThreadSize_ > initThreadSize_)
						{
							//��ʼ�����߳�
							//��¼�߳���������ر���ֵ���޸�
							curThreadSize_--;
							idleThreadSize_--;
							//���̶߳���������б���ɾ��
							threads_.erase(threadid);
							//threadid =�� thread ���� =�� ɾ��

							std::cout << "thread id:" << std::this_thread::get_id() << " exit" << std::endl;
							return;
						}
					}
				}
				else
				{
					//�ȴ�notEmpty����
					notEmpty_.wait(lock);
				}

				
				//if (!isPoolRunning_)
				//{
				//	//���̶߳���������б���ɾ��
				//	threads_.erase(threadid);
				//	//threadid =�� thread ���� =�� ɾ��

				//	std::cout << "thread id:" << std::this_thread::get_id() << " exit" << std::endl;
				//	exitCond_.notify_all();
				//	return;
				//}
			}

			//�̻߳�ȡ������
			idleThreadSize_--;

			std::cout << "tid: " << std::this_thread::get_id()
				<< "��ȡ����ɹ�" << std::endl;

			//ȡһ���������
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			//�����Ȼ��ʣ������֪ͨ�����߳�ִ������
			if (taskQue_.size() > 0)
			{
				notEmpty_.notify_all();
			}

			//ȡ��һ������֪ͨ������������
			notFull_.notify_all();

		}//�ͷ���

		//��ǰ�̸߳���ִ������
		if (task != nullptr)
		{
			task->exec();
			//ִ����һ���������֪ͨ
		}
		
		//����ִ����ϣ������߳�����++
		idleThreadSize_++;
		lastTime = std::chrono::high_resolution_clock().now();//�����߳�ִ��������ʱ��
	}
	
}


//����̳߳��Ƿ���
bool ThreadPool::checkPoolRunning() const
{
	return isPoolRunning_;
}

//////////////// �̷߳���ʵ��

int Thread::generateID_ = 0;

//�̹߳���
Thread::Thread(ThreadFunc func)
	:func_(func)
	,threadId_(generateID_++)
{
}
// �߳�����
Thread::~Thread()
{

}

//�����߳�
void Thread::start()
{
	//����һ���߳� ִ��һ���̺߳���
	std::thread t(func_,threadId_); // C++11 �̶߳���t ���̶߳�����func
	t.detach(); //���÷����߳�  pthread_detach  pthread_t���óɷ����߳�
}

//��ȡ�߳�ID
int Thread::getID()
{
	return threadId_;
}

////////////////Task����ʵ��
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

/////////////////Result ������ʵ��
Result::Result(std::shared_ptr<Task> task, bool isValid)
	:task_(task)
	,isValid_(isValid)
{
	task_->setResult(this);
}


//setVal��������ȡ����ķ���ֵ
void Result::setVal(Any any) {
	any_ = std::move(any);//��ֵ��ֵ���������
	sem_.post();//�����ź�
}

//getVal�������û�������������õ�task����ֵ
Any Result::get()
{
	//�ȼ���Ƿ���Ч
	if (!isValid_)
	{
		return "";
	}
	//�ȴ��ź���
	sem_.wait();
	return std::move(any_);//ֻ����ֵ��������
}