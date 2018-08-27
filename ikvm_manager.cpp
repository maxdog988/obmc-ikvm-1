#include <thread>
#include <time.h>
#include "ikvm_manager.hpp"
#include <iostream>
#define PROFILE_SAMPLES		512ULL

struct profile {
	profile(const char *n) : name(n) {}

	const char *name;
	bool rolled_over;
	unsigned int idx;
	unsigned long long times[PROFILE_SAMPLES];
};

unsigned long long _avg(struct profile *p)
{
	unsigned int i;
	unsigned int limit = p->rolled_over ? PROFILE_SAMPLES : p->idx + 1;
	unsigned long long rc = 0;

	for (i = 0; i < limit; ++i)
		rc += (p->times[i] / limit);

	return rc;
}

void _prof(struct timespec *diff, struct profile *p)
{
	unsigned long long usec = diff->tv_nsec / 1000ULL;

	if (diff->tv_sec)
		usec += 1000000ULL;

	p->times[p->idx++] = usec;
	if (p->idx >= PROFILE_SAMPLES) {
		p->rolled_over = true;
		p->idx = 0;
std::cout << p->name << " " << _avg(p) << std::endl;
	}
}

static int timespec_subtract(struct timespec *result, struct timespec *x,
			     struct timespec *y)
{
	/* Perform the carry for the later subtraction by updating y. */
	if (x->tv_nsec < y->tv_nsec) {
		long long int nsec =
			((y->tv_nsec - x->tv_nsec) / 1000000000ULL) + 1;

		y->tv_nsec -= 1000000000ULL * nsec;
		y->tv_sec += nsec;
	}

	if (x->tv_nsec - y->tv_nsec > 1000000000LL) {
		long long int nsec = (x->tv_nsec - y->tv_nsec) / 1000000000ULL;

		y->tv_nsec += 1000000000ULL * nsec;
		y->tv_sec -= nsec;
	}

	/*
	 * Compute the time remaining to wait.
	 * tv_nsec is certainly positive.
	 */
	result->tv_sec = x->tv_sec - y->tv_sec;
	result->tv_nsec = x->tv_nsec - y->tv_nsec;

	/* Return 1 if result is negative. */
	return x->tv_sec < y->tv_sec;
}

struct profile _frame("frame");
struct profile _frame_wait("frame wait");
struct profile _server("server");
struct profile _server_wait("server wait");

namespace ikvm
{

Manager::Manager(const Args& args) :
    continueExecuting(true),
    serverDone(false),
    videoDone(true),
    input(args.getInputPath()),
    video(args.getVideoPath(), input, args.getFrameRate()),
    server(args, input, video)
{}

void Manager::run()
{
    struct timespec start;
    struct timespec end;
    struct timespec diff;
    std::thread run(serverThread, this);

    while (continueExecuting)
    {
        clock_gettime(CLOCK_MONOTONIC, &start);
        if (server.wantsFrame())
        {
            bool needsResize(false);

            video.getFrame(needsResize);
            if (needsResize)
            {
                videoDone = false;
                waitServer();
                video.resize();
                server.resize();
                setVideoDone();
                goto done;
            }

            server.sendFrame();
            clock_gettime(CLOCK_MONOTONIC, &end);
            timespec_subtract(&diff, &end, &start);
            _prof(&diff, &_frame);
        }

        setVideoDone();
        waitServer();
done:
        clock_gettime(CLOCK_MONOTONIC, &start);
        timespec_subtract(&diff, &start, &end);
        _prof(&diff, &_frame_wait);
    }

    run.join();
}

void Manager::serverThread(Manager* manager)
{
    struct timespec start;
    struct timespec end;
    struct timespec diff;

    while (manager->continueExecuting)
    {
        clock_gettime(CLOCK_MONOTONIC, &start);
        manager->server.run();

        clock_gettime(CLOCK_MONOTONIC, &end);
        timespec_subtract(&diff, &end, &start);
        _prof(&diff, &_server);

        manager->setServerDone();
        manager->waitVideo();

        clock_gettime(CLOCK_MONOTONIC, &start);
        timespec_subtract(&diff, &start, &end);
        _prof(&diff, &_server_wait);
    }
}

void Manager::setServerDone()
{
    std::unique_lock<std::mutex> ulock(lock);

    serverDone = true;
    sync.notify_all();
}

void Manager::setVideoDone()
{
    std::unique_lock<std::mutex> ulock(lock);

    videoDone = true;
    sync.notify_all();
}

void Manager::waitServer()
{
    std::unique_lock<std::mutex> ulock(lock);

    while (!serverDone)
    {
        sync.wait(ulock);
    }

    serverDone = false;
}

void Manager::waitVideo()
{
    std::unique_lock<std::mutex> ulock(lock);

    while (!videoDone)
    {
        sync.wait(ulock);
    }
}

} // namespace ikvm
