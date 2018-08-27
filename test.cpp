#include <algorithm>
#include <chrono>
#include <exception>
#include <iostream>
#include <vector>

#include <boost/context/continuation.hpp>
#include <boost/fiber/all.hpp>

#include "coro.h"
#include <pth.h>

size_t num_fibers_tests[] = {1, 2, 10, 100, 1000, 10000, 100000};
const size_t STACK_SIZE = 16 * 1024;

boost::context::continuation coro_warmup(boost::context::continuation&& cont)
{
    cont = cont.resume();
    return std::move(cont);
}

static void warmup()
{
    std::cout << "------------------------------------------" << std::endl;

    boost::context::continuation ctx;

    auto before = std::chrono::high_resolution_clock::now();
    auto after = before;

    ctx = boost::context::callcc(coro_warmup);
    ctx = ctx.resume();

    after = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(after - before);
    std::cout << "Warmup time: " << duration.count() << "us" << std::endl;

}


static void inc_yield(size_t& v, bool &run)
{
    while (run)
    {
        v++;
        boost::this_fiber::yield();
    }
}

static void test_yield()
{
    std::cout << "------------------------------------------" << std::endl;
    for (auto num_fibers : num_fibers_tests)
    {
        std::cout << "------------------------------------------" << std::endl;
        std::cout << "Testing yield for " << num_fibers << " boost fibers" << std::endl;

        std::vector<boost::fibers::fiber> fibers(num_fibers);
        std::vector<size_t> values(num_fibers, 0);
        bool run = true;

        for (size_t i = 0; i < num_fibers; i++)
            fibers[i] = boost::fibers::fiber(inc_yield, std::ref(values[i]), std::ref(run));

        auto before = std::chrono::high_resolution_clock::now();
        boost::this_fiber::sleep_for(std::chrono::seconds(1));
        run = false;

        for (size_t i = 0; i < num_fibers; i++)
            fibers[i].join();
        auto after = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(after - before);

        size_t total = std::accumulate(values.begin(), values.end(), 0);
        size_t min_value = *std::min_element(values.begin(), values.end());
        size_t max_value = *std::max_element(values.begin(), values.end());

        std::cout << "Total ops per second: " << total / duration.count() * 1000000
                  << " (" << (total / duration.count()) << " Mrps)" << std::endl;
        std::cout << "Ops per fiber: min: " << min_value
                  << " max: " << max_value << std::endl;
    }
}

static void inc(size_t& v)
{
    v++;
}

static void create_inc_yield(size_t& v, bool &run)
{
    while (run)
    {
        boost::fibers::fiber f(inc, std::ref(v));
        f.join();
        boost::this_fiber::yield();
    }
}

static void test_create()
{
    std::cout << "------------------------------------------" << std::endl;
    for (auto num_fibers : num_fibers_tests)
    {
        std::cout << "------------------------------------------" << std::endl;
        std::cout << "Testing create+yield for " << num_fibers << " boost fibers" << std::endl;

        std::vector<boost::fibers::fiber> fibers(num_fibers);
        std::vector<size_t> values(num_fibers, 0);
        bool run = true;

        auto before = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < num_fibers; i++)
            fibers[i] = boost::fibers::fiber(create_inc_yield, std::ref(values[i]), std::ref(run));

        boost::this_fiber::sleep_for(std::chrono::seconds(1));
        run = false;

        for (size_t i = 0; i < num_fibers; i++)
            fibers[i].join();
        auto after = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(after - before);

        size_t total = std::accumulate(values.begin(), values.end(), 0);
        size_t min_value = *std::min_element(values.begin(), values.end());
        size_t max_value = *std::max_element(values.begin(), values.end());

        std::cout << "Total ops per second: " << total / duration.count() * 1000000
                  << " (" << (total / duration.count() * 1000) << " krps)" << std::endl;
        std::cout << "Ops per fiber: min: " << min_value
                  << " max: " << max_value << std::endl;
    }
}

struct TestExc : public std::exception
{
    TestExc() { std::cout << "TestExc ctor" << std::endl; }
    ~TestExc() { std::cout << "TestExc dtor" << std::endl; }
    TestExc(const TestExc&) { std::cout << "TestExc copy ctor" << std::endl; }
    TestExc(TestExc&&) { std::cout << "TestExc move ctor" << std::endl; }
    void operator=(const TestExc&) { std::cout << "TestExc assign" << std::endl; }
    void operator=(TestExc&&) { std::cout << "TestExc move assign" << std::endl; }
};

static void throwing(std::exception_ptr &ptr)
{
    try {
        throw TestExc();
    }
    catch(...)
    {
        ptr = std::current_exception();
    }
}

static void test_exception()
{
    std::cout << "------------------------------------------" << std::endl;
    std::cout << "------------------------------------------" << std::endl;
    std::cout << "Test exceptions" << std::endl;
    std::exception_ptr eptr;
    boost::fibers::fiber f(throwing, std::ref(eptr));
    f.join();
    if (eptr)
    {
        try {
            rethrow_exception(eptr);
        } catch (TestExc&)
        {
            std::cout << "Exception catched" << std::endl;
        }
    }
}

static void suspended_fiber(boost::fibers::promise<void> &promise_wait,
                            boost::fibers::promise<void> &promise_wakeup,
                            size_t &v, bool &run)
{
    while (run)
    {
        promise_wait.get_future().wait();
        boost::fibers::promise<void>().swap(promise_wait);
        v++;
        promise_wakeup.set_value();
    }
}

static void test_suspend()
{
    std::cout << "------------------------------------------" << std::endl;
    for (auto num_fibers : num_fibers_tests)
    {
        if (num_fibers == 1)
            continue;
        std::cout << "------------------------------------------" << std::endl;
        std::cout << "Testing suspend for " << num_fibers << " boost fibers" << std::endl;

        std::vector<boost::fibers::fiber> fibers(num_fibers);
        std::vector<boost::fibers::promise<void>> promises(num_fibers);
        size_t total = 0;
        bool run = true;

        for (size_t i = 0; i < num_fibers; i++)
            fibers[i] = boost::fibers::fiber(suspended_fiber,
                                             std::ref(promises[i]),
                                             std::ref(promises[(i + 1) % num_fibers]),
                                             std::ref(total), std::ref(run));

        auto before = std::chrono::high_resolution_clock::now();
        promises[0].set_value();
        boost::this_fiber::sleep_for(std::chrono::seconds(1));
        run = false;

        for (size_t i = 0; i < num_fibers; i++)
            fibers[i].join();
        auto after = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(after - before);

        std::cout << "Total ops per second: " << total / duration.count() * 1000000
                  << " (" << (total / duration.count()) << " Mrps)" << std::endl;
    }
}

static void call_inc(size_t &v) __attribute__ ((noinline));

static void call_inc(size_t &v)
{
    v++;
}

static void test_call()
{
    std::cout << "------------------------------------------" << std::endl;
    std::cout << "Testing usual function call" << std::endl;
    size_t total = 0;
    auto before = std::chrono::high_resolution_clock::now();
    auto after = before;
    while (true)
    {
        for (size_t i = 0; i < 10000; i++)
            call_inc(total);
        after = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(after - before);
        if (duration.count() >= 1000000)
            break;
    }
    auto duration = std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(after - before);
    std::cout << "Total ops per second: " << total / duration.count()
              << " (" << (total / duration.count()) << " Mrps)" << std::endl;
}


size_t coro_total;
bool coro_run = true;
size_t coro_fin;

boost::context::continuation coro(boost::context::continuation&& cont)
{
    while (coro_run)
    {
        coro_total++;
        cont = cont.resume();
    }
    coro_fin++;
    return std::move(cont);
}

static void test_context_switch()
{
    std::cout << "------------------------------------------" << std::endl;
    for (auto num_fibers : num_fibers_tests)
    {
        std::cout << "------------------------------------------" << std::endl;
        std::cout << "Testing context switch for " << num_fibers << " boost contexts" << std::endl;

        coro_total = 0;
        coro_run = true;
        coro_fin = 0;
        std::vector<boost::context::continuation> ctxs(num_fibers);

        auto before = std::chrono::high_resolution_clock::now();
        auto after = before;

        for (size_t i = 0; i < num_fibers; i++)
            ctxs[i] = boost::context::callcc(coro);

        while (true)
        {
            size_t j_lim = 1000000 / num_fibers;
            j_lim = j_lim ? j_lim : 1;
            for (size_t j = 0; j < j_lim; j++)
                for (size_t i = 0; i < num_fibers; i++)
                    ctxs[i] = ctxs[i].resume();

            after = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(after - before);
            if (duration.count() >= 1000000)
                break;
        }

        coro_run = false;
        for (size_t i = 0; i < num_fibers; i++)
            ctxs[i] = ctxs[i].resume();

        assert(coro_fin == num_fibers);

        size_t op_total = coro_total * 2; // 2 switched per coro

        auto duration = std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(after - before);
        std::cout << "Total ops per second: " << op_total / duration.count() << " Mrps)" << std::endl;

    }
}

coro_context main_ctx;

static void my_coro_func(void *smth)
{
    coro_context *this_ctx = (coro_context *)smth;
    while (coro_run)
    {
        coro_total++;
        coro_transfer(this_ctx, &main_ctx);
    }
    coro_fin++;
    coro_transfer(this_ctx, &main_ctx);
}

static void test_coro()
{
    std::cout << "------------------------------------------" << std::endl;
#ifdef CORO_ASM
    std::cout << "CORO ASM!" << std::endl;
#endif

    for (auto num_fibers : num_fibers_tests)
    {
        std::cout << "------------------------------------------" << std::endl;
        std::cout << "Testing context switch for " << num_fibers << " coroutines from tarantool" << std::endl;

        coro_total = 0;
        coro_run = true;
        coro_fin = 0;
        std::vector<coro_context> ctxs(num_fibers);
        std::vector<coro_stack> stacks(num_fibers);

        auto before = std::chrono::high_resolution_clock::now();
        auto after = before;

        for (size_t i = 0; i < num_fibers; i++)
        {
            if (!coro_stack_alloc(&stacks[i], STACK_SIZE))
            {
                std::cout << "coro_stack_alloc failed #" << i << std::endl;
                for (size_t j = 0; j < i; j++)
                    coro_stack_free(&stacks[j]);
                return;
            }
            coro_create(&ctxs[i], my_coro_func, &ctxs[i], stacks[i].sptr, stacks[i].ssze);
        }

        while (true)
        {
            size_t j_lim = 1000000 / num_fibers;
            j_lim = j_lim ? j_lim : 1;
            for (size_t j = 0; j < j_lim; j++)
                for (size_t i = 0; i < num_fibers; i++)
                {
                    coro_transfer(&main_ctx, &ctxs[i]);
                }

            after = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(after - before);
            if (duration.count() >= 1000000)
                break;
        }

        coro_run = false;
        for (size_t i = 0; i < num_fibers; i++)
        {
            coro_transfer(&main_ctx, &ctxs[i]);
            (void)coro_destroy(&ctxs[i]);
            coro_stack_free(&stacks[i]);
        }

        assert(coro_fin == num_fibers);

        size_t op_total = coro_total * 2; // 2 switched per coro

        auto duration = std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(after - before);
        std::cout << "Total ops per second: " << op_total / duration.count() << " Mrps)" << std::endl;

    }
    (void)coro_destroy(&main_ctx);
}

static void* inc_pth_yield(void *arg)
{
    std::pair<size_t, bool*>& targ = *(std::pair<size_t, bool*>*)arg;
    bool& run = *targ.second;
    while (run)
    {
        targ.first++;
        pth_yield(NULL);
    }
    return NULL;
}

static void pth_yield()
{
    if (pth_init() != TRUE)
        std::cout << "FATAL!" << std::endl;
    std::cout << "------------------------------------------" << std::endl;
    for (auto num_fibers : num_fibers_tests)
    {
        std::cout << "------------------------------------------" << std::endl;
        std::cout << "Testing yield for " << num_fibers << " pth fibers" << std::endl;

        std::vector<pth_t > fibers(num_fibers);
        std::vector<std::pair<size_t, bool*>> values(num_fibers);
        bool run = true;

        for (size_t i = 0; i < num_fibers; i++)
        {
            values[i].first = 0;
            values[i].second = &run;
            pth_attr_t attr = pth_attr_new();
            pth_attr_set(attr, PTH_ATTR_STACK_SIZE, STACK_SIZE);
            pth_attr_set(attr, PTH_ATTR_JOINABLE, TRUE);
            fibers[i] = pth_spawn(attr, inc_pth_yield, &values[i]);
        }
        pth_yield(NULL);

        auto before = std::chrono::high_resolution_clock::now();
        pth_nap(pth_time(1, 0));
        run = false;

        size_t total = 0;
        size_t min_value = values[0].first;
        size_t max_value = values[0].first;
        for (size_t i = 0; i < num_fibers; i++)
        {
            pth_join(fibers[i], NULL);
            total += values[i].first;
            if (values[i].first < min_value)
                min_value = values[i].first;
            if (values[i].first > max_value)
                max_value = values[i].first;
        }
        auto after = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(after - before);

        std::cout << "Total ops per second: " << total / duration.count() * 1000000
                  << " (" << (total / duration.count()) << " Mrps)" << std::endl;
        std::cout << "Ops per fiber: min: " << min_value
                  << " max: " << max_value << std::endl;
    }
    pth_kill();
}

pth_uctx_t main_pth_ctx;

static void pth_coro_func(void *smth)
{
    pth_uctx_t *this_ctx = (pth_uctx_t *)smth;
    while (coro_run)
    {
        coro_total++;
        pth_uctx_switch(*this_ctx, main_pth_ctx);
    }
    coro_fin++;
    pth_uctx_switch(*this_ctx, main_pth_ctx);
}

static void test_pth_context()
{
    std::cout << "------------------------------------------" << std::endl;

    pth_uctx_create(&main_pth_ctx);
    pth_uctx_make(main_pth_ctx, nullptr, 0, nullptr, 0, nullptr, nullptr);

    for (auto num_fibers : num_fibers_tests)
    {
        std::cout << "------------------------------------------" << std::endl;
        std::cout << "Testing pth context switch for " << num_fibers << " pth contexts" << std::endl;

        coro_total = 0;
        coro_run = true;
        coro_fin = 0;
        std::vector<pth_uctx_t> ctxs(num_fibers);

        auto before = std::chrono::high_resolution_clock::now();
        auto after = before;

        for (size_t i = 0; i < num_fibers; i++)
        {
            pth_uctx_create(&ctxs[i]);
            pth_uctx_make(ctxs[i], nullptr, STACK_SIZE, nullptr, pth_coro_func, &ctxs[i], nullptr);
            pth_uctx_switch(main_pth_ctx, ctxs[i]);
        }

        while (true)
        {
            size_t j_lim = 1000000 / num_fibers;
            j_lim = j_lim ? j_lim : 1;
            for (size_t j = 0; j < j_lim; j++)
                for (size_t i = 0; i < num_fibers; i++)
                {
                    pth_uctx_switch(main_pth_ctx, ctxs[i]);
                }

            after = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(after - before);
            if (duration.count() >= 1000000)
                break;
        }

        coro_run = false;
        for (size_t i = 0; i < num_fibers; i++)
        {
            pth_uctx_switch(main_pth_ctx, ctxs[i]);
            pth_uctx_destroy(ctxs[i]);
        }

        assert(coro_fin == num_fibers);

        size_t op_total = coro_total * 2; // 2 switched per coro

        auto duration = std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(after - before);
        std::cout << "Total ops per second: " << op_total / duration.count() << " Mrps)" << std::endl;

    }
    pth_uctx_destroy(main_pth_ctx);
}

int main(int n, const char**)
{
#ifdef BOOST_FIBERS_NO_ATOMICS
    std::cout << "BOOST_FIBERS_NO_ATOMICS is set" << std::endl;
#endif

    for (size_t i = 0; i < 1000000000; i++)
        n = n * 13 + 17;
    warmup();

    test_yield();
    test_create();
    test_suspend();
    test_exception();
    test_call();
    pth_yield();
    test_context_switch();
    test_coro();
    test_pth_context();

    return n;
}