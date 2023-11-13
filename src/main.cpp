#include <iostream>
#include <random>
#include <algorithm>
#include <omp.h>
#include <thread>
#include <mutex>
#include <queue>
#include <vector>
#include <functional>
#include <memory>
#include <condition_variable>
#include <atomic>
#include <iterator>
#include <chrono>
using namespace std;

static random_device r_device;
static uniform_real_distribution<double> rnd{0,1};

enum JobState{
	Waiting,
	Active,
	Completed
};

typedef function<void()> tasktype;

struct JobRecipt{
	tasktype task;
	JobState status=Waiting;
};

struct WorkerSharedDataBlock{
	mutex job_lock;
	condition_variable job_var;
	queue<shared_ptr<JobRecipt>> jobs;
	atomic<int> thread_debug_counter{0};
};

void workerPoolJobRunner(shared_ptr<WorkerSharedDataBlock> data,shared_ptr<atomic<bool>> exit_signal){
	
	++(data->thread_debug_counter);
	
	while(true){
		unique_lock<mutex> lock(data->job_lock);
		data->job_var.wait(lock,[&]{return *exit_signal || !data->jobs.empty();});
		
		if(*exit_signal){
			printf("exiting thread with counter currently at %i\n",(int)(data->thread_debug_counter));
			break;
		}
		
		auto job=data->jobs.front();
		data->jobs.pop();
		
		lock.unlock();
		data->job_var.notify_all();
		
		job->status=Active;
		job->task();
		job->status=Completed;
	}
	
	--(data->thread_debug_counter);
}

class WorkerPool{
private:
	vector<thread> threads;
	shared_ptr<WorkerSharedDataBlock> thread_data;
	
	shared_ptr<atomic<bool>> exiting;
	
	void sendExitSignal(){
		*exiting=true;
		thread_data->job_var.notify_all();
		
		//we need a new exiting value at a new address, the old one's gone stale
		exiting=make_shared<atomic<bool>>(false);
	}
		
public:
	WorkerPool(const size_t thread_count=thread::hardware_concurrency()){
		exiting=make_shared<atomic<bool>>(false);
		thread_data=make_shared<WorkerSharedDataBlock>();
		
		setPoolSize(thread_count);
	}
	
	~WorkerPool(){
		printf("deleting pool with %i worker threads\n",(int)(thread_data->thread_debug_counter));
		
		//flush remaining threads
		setPoolSize(0);
		printf("pool scaled down\n");
	}
	
	void setPoolSize(const size_t thread_count=thread::hardware_concurrency()){
		unique_lock<mutex> lock(thread_data->job_lock);
		
		if(thread_count==threads.size()){
			return;
		}
		
		size_t start=0;
		
		if(thread_count<threads.size()){
			//signal to the existing threads that they should exit
			sendExitSignal();
			
			//flush the queue, note that this is somewhat unsafe, since these threads (seemingly) continue running in the bg uncaptured
			for(auto& thread:threads){
				thread.detach();
			}
			threads.clear();
		}else{
			start=threads.size();
		}
		
		const size_t new_elements=thread_count-start;
		for(size_t i=0;i<new_elements;i++){
			threads.push_back(thread(&workerPoolJobRunner,thread_data,exiting));
		}
	}
	
	shared_ptr<JobRecipt> commitJob(tasktype task){
		unique_lock<mutex> lock(thread_data->job_lock);
		
		auto newjob=make_shared<JobRecipt>(JobRecipt{task});
		
		thread_data->jobs.push(newjob);
		lock.unlock();
		
		thread_data->job_var.notify_one();
		
		return newjob;
	}
	
	template<typename Iterator>
	void map(Iterator begin, Iterator end, function<void(Iterator)> task){
		//TODO: should be possible to do this with minimal auxilary data storage.  for all we know, these iterators could be infinitely long
		queue<shared_ptr<JobRecipt>> recipts;
		
		for(; begin != end; ++begin){
			recipts.push(commitJob(bind(task,begin)));
		}
		
		while(recipts.size()){
			if(recipts.front()->status==Completed){
				recipts.pop();
			}
		}
	}
};

mutex global_lock;

void dothing(){
	unique_lock thing_lock(global_lock);
	printf("hehe funny\n");
}

void waitAndPrint(int* i){
	using namespace std::chrono_literals;
	
	this_thread::sleep_for(1s);
	
	printf("lazily printing %i\n",*i);
}

int main(int argc,char** argv){
	
	cout<<"aaa"<<endl;
	
	unique_lock main_lock(global_lock);
	thread thing_thread(dothing);
	
#pragma omp parallel for
	for(int i=0;i<10;i++){
		printf("omp thread: %i\n",i);
	}
	cout<<endl;
	
	main_lock.unlock();
	
	int sum=0;
	const int sum_count=10;
#pragma omp parallel for reduction(+ : sum)
	for(int i=0;i<sum_count;i++){
		sum+=i;
	}
	cout<<"parallel sum: "<<sum<<endl;
	cout<<"sum should be: "<<(sum_count*(sum_count-1)/2)<<endl;
	
	thing_thread.join();
	
	
	WorkerPool pool;
	
	const int n=10;
	vector<int> v(n);
	iota(v.begin(),v.end(),0);
	
	auto start=&v[0];
	function<void(int*)> task=waitAndPrint;
	pool.map(start+0,start+n,task);
	
	return 0;
}
