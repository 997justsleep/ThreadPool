#include<iostream>
#include<chrono>
#include<thread>
#include"threadpool.h"


class MyTask :public Task
{
public:
	MyTask(int begin,int end)
		:begin_(begin),end_(end)
	{}

	Any run() override
	{
		std::cout << "tid:" << std::this_thread::get_id() << "begin!" << std::endl;
		int sum = 0;
		for (int i = begin_; i <= end_; ++i)
		{
			sum += i;
		}
		std::this_thread::sleep_for(std::chrono::seconds(3));
		std::cout << "tid:" << std::this_thread::get_id() << "end!" << std::endl; 
		return sum;
	}

private:
	int begin_;
	int end_;
};

int main()
{
	{

		ThreadPool pool;

		//pool.setMode(PoolMode::MODE_CACHED);

		pool.start(4);

		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000));
		Result res2 = pool.submitTask(std::make_shared<MyTask>(100001, 200000));
		Result res3 = pool.submitTask(std::make_shared<MyTask>(200001, 300000));
		Result res4 = pool.submitTask(std::make_shared<MyTask>(200001, 300000));
		Result res5 = pool.submitTask(std::make_shared<MyTask>(200001, 300000));
		Result res6 = pool.submitTask(std::make_shared<MyTask>(200001, 300000));

		//随着task执行完毕，task对象被释放，而结果对象的生命周期需要在取完之后再结束
		/*int sum = res1.get().cast_<int>() + res2.get().cast_<int>() + res3.get().cast_<int>()
			+ res4.get().cast_<int>() + res5.get().cast_<int>() + res6.get().cast_<int>();*/
	}
	

	/*int sum1 = 0;
	for (int i = 1; i <= 300000; i++)
	{
		sum1 += i;
	}*/
	//std::cout <<"sum: "<< sum << std::endl <<"sum1: "<< sum1 << std::endl;

	std::cout << "main thread end" << std::endl;
	getchar();
	return 0;
}