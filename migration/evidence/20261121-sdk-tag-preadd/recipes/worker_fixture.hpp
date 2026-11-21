// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "sdk_tag_rate_group.hpp"
#include <libtorrent/session.hpp>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
namespace worker_fixture {
namespace e=biglybtc::engine;namespace lt=libtorrent;
using namespace std::chrono_literals;
struct State {std::unique_ptr<lt::session> sdk;std::unique_ptr<e::SdkTagRateGroup> group;};
class Remote {
 std::mutex mutex;std::condition_variable changed;std::deque<std::function<void(State&)>> jobs;bool stopped=false;
 std::jthread thread{[this]{State state;for(;;){std::function<void(State&)> job;{std::unique_lock lock(mutex);changed.wait(lock,[&]{return stopped||!jobs.empty();});if(jobs.empty()&&stopped){break;}job=std::move(jobs.front());jobs.pop_front();}job(state);}}};
public:
 template<class F>auto call(F operation){using T=std::invoke_result_t<F,State&>;auto task=std::make_shared<std::packaged_task<T(State&)>>(std::move(operation));auto future=task->get_future();{std::lock_guard lock(mutex);jobs.emplace_back([task](State& state){(*task)(state);});}changed.notify_one();if(future.wait_for(20s)!=std::future_status::ready){throw std::runtime_error("Generated SDK control worker deadline");}return future.get();}
 ~Remote(){{std::lock_guard lock(mutex);stopped=true;}changed.notify_all();thread.join();}
};
}
