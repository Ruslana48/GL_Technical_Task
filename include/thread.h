#ifndef WIN32STDTHREAD_H
#define WIN32STDTHREAD_H

#if !defined(__cplusplus) || (__cplusplus < 201103L)
#error A C++11 compiler is required!
#endif

#include <thread>

#include <cstddef>      
#include <cerrno>       
#include <exception>    
#include <system_error> 
#include <functional>   
#include <tuple>        
#include <chrono>       
#include <memory>       
#include <iosfwd>       
#include <utility>      

#include "mingw.invoke.h"

#if (defined(__MINGW32__) && !defined(__MINGW64_VERSION_MAJOR))

#include <windows.h>    
#else
#include <synchapi.h>   
#include <handleapi.h>  
#include <sysinfoapi.h> 
#include <processthreadsapi.h>  
#endif
#include <process.h>  

#ifndef NDEBUG
#include <cstdio>
#endif


namespace mingw_stdthread
{
namespace detail
{
    template<std::size_t...>
    struct IntSeq {};

    template<std::size_t N, std::size_t... S>
    struct GenIntSeq : GenIntSeq<N-1, N-1, S...> { };

    template<std::size_t... S>
    struct GenIntSeq<0, S...> { typedef IntSeq<S...> type; };


    template<class Func, class T, typename... Args>
    class ThreadFuncCall;
    template<class Func, std::size_t... S, typename... Args>
    class ThreadFuncCall<Func, detail::IntSeq<S...>, Args...>
    {
        static_assert(sizeof...(S) == sizeof...(Args), "Args must match.");
        using Tuple = std::tuple<typename std::decay<Args>::type...>;
        typename std::decay<Func>::type mFunc;
        Tuple mArgs;

    public:
        ThreadFuncCall(Func&& aFunc, Args&&... aArgs)
          : mFunc(std::forward<Func>(aFunc)),
            mArgs(std::forward<Args>(aArgs)...)
        {
        }

        void callFunc()
        {
            detail::invoke(std::move(mFunc), std::move(std::get<S>(mArgs)) ...);
        }
    };

    class ThreadIdTool;
} //  Namespace "detail"

class thread
{
public:
    class id
    {
        DWORD mId = 0;
        friend class thread;
        friend class std::hash<id>;
        friend class detail::ThreadIdTool;
        explicit id(DWORD aId) noexcept : mId(aId){}
    public:
        id (void) noexcept = default;
        friend bool operator==(id x, id y) noexcept {return x.mId == y.mId; }
        friend bool operator!=(id x, id y) noexcept {return x.mId != y.mId; }
        friend bool operator< (id x, id y) noexcept {return x.mId <  y.mId; }
        friend bool operator<=(id x, id y) noexcept {return x.mId <= y.mId; }
        friend bool operator> (id x, id y) noexcept {return x.mId >  y.mId; }
        friend bool operator>=(id x, id y) noexcept {return x.mId >= y.mId; }

        template<class _CharT, class _Traits>
        friend std::basic_ostream<_CharT, _Traits>&
        operator<<(std::basic_ostream<_CharT, _Traits>& __out, id __id)
        {
            if (__id.mId == 0)
            {
                return __out << "(invalid std::thread::id)";
            }
            else
            {
                return __out << __id.mId;
            }
        }
    };
private:
    static constexpr HANDLE kInvalidHandle = nullptr;
    static constexpr DWORD kInfinite = 0xffffffffl;
    HANDLE mHandle;
    id mThreadId;

    template <class Call>
    static unsigned __stdcall threadfunc(void* arg)
    {
        std::unique_ptr<Call> call(static_cast<Call*>(arg));
        call->callFunc();
        return 0;
    }

    static unsigned int _hardware_concurrency_helper() noexcept
    {
        SYSTEM_INFO sysinfo;

#if defined(_WIN32_WINNT) && (_WIN32_WINNT >= 0x0501)
        ::GetNativeSystemInfo(&sysinfo);
#else
        ::GetSystemInfo(&sysinfo);
#endif
        return sysinfo.dwNumberOfProcessors;
    }
public:
    typedef HANDLE native_handle_type;
    id get_id() const noexcept {return mThreadId;}
    native_handle_type native_handle() const {return mHandle;}
    thread(): mHandle(kInvalidHandle), mThreadId(){}

    thread(thread&& other)
    :mHandle(other.mHandle), mThreadId(other.mThreadId)
    {
        other.mHandle = kInvalidHandle;
        other.mThreadId = id{};
    }

    thread(const thread &other)=delete;

    template<class Func, typename... Args>
    explicit thread(Func&& func, Args&&... args) : mHandle(), mThreadId()
    {
        using ArgSequence = typename detail::GenIntSeq<sizeof...(Args)>::type;
        using Call = detail::ThreadFuncCall<Func, ArgSequence, Args...>;
        auto call = new Call(
            std::forward<Func>(func), std::forward<Args>(args)...);
        unsigned id_receiver;
        auto int_handle = _beginthreadex(NULL, 0, threadfunc<Call>,
            static_cast<LPVOID>(call), 0, &id_receiver);
        if (int_handle == 0)
        {
            mHandle = kInvalidHandle;
            int errnum = errno;
            delete call;
            throw std::system_error(errnum, std::generic_category());
        } else {
            mThreadId.mId = id_receiver;
            mHandle = reinterpret_cast<HANDLE>(int_handle);
        }
    }

    bool joinable() const {return mHandle != kInvalidHandle;}


    void join()
    {
        using namespace std;
        if (get_id() == id(GetCurrentThreadId()))
            throw system_error(make_error_code(errc::resource_deadlock_would_occur));
        if (mHandle == kInvalidHandle)
            throw system_error(make_error_code(errc::no_such_process));
        if (!joinable())
            throw system_error(make_error_code(errc::invalid_argument));
        WaitForSingleObject(mHandle, kInfinite);
        CloseHandle(mHandle);
        mHandle = kInvalidHandle;
        mThreadId = id{};
    }

    ~thread()
    {
        if (joinable())
        {
#ifndef NDEBUG
            std::printf("Error: Must join() or detach() a thread before \
destroying it.\n");
#endif
            std::terminate();
        }
    }
    thread& operator=(const thread&) = delete;
    thread& operator=(thread&& other) noexcept
    {
        if (joinable())
        {
#ifndef NDEBUG
            std::printf("Error: Must join() or detach() a thread before \
moving another thread to it.\n");
#endif
            std::terminate();
        }
        swap(std::forward<thread>(other));
        return *this;
    }
    void swap(thread&& other) noexcept
    {
        std::swap(mHandle, other.mHandle);
        std::swap(mThreadId.mId, other.mThreadId.mId);
    }

    static unsigned int hardware_concurrency() noexcept
    {
        static unsigned int cached = _hardware_concurrency_helper();
        return cached;
    }

    void detach()
    {
        if (!joinable())
        {
            using namespace std;
            throw system_error(make_error_code(errc::invalid_argument));
        }
        if (mHandle != kInvalidHandle)
        {
            CloseHandle(mHandle);
            mHandle = kInvalidHandle;
        }
        mThreadId = id{};
    }
};

namespace detail
{
    class ThreadIdTool
    {
    public:
        static thread::id make_id (DWORD base_id) noexcept
        {
            return thread::id(base_id);
        }
    };
} //  Namespace "detail"

namespace this_thread
{
    inline thread::id get_id() noexcept
    {
        return detail::ThreadIdTool::make_id(GetCurrentThreadId());
    }
    inline void yield() noexcept {Sleep(0);}
    template< class Rep, class Period >
    void sleep_for( const std::chrono::duration<Rep,Period>& sleep_duration)
    {
        static constexpr DWORD kInfinite = 0xffffffffl;
        using namespace std::chrono;
        using rep = milliseconds::rep;
        rep ms = duration_cast<milliseconds>(sleep_duration).count();
        while (ms > 0)
        {
            constexpr rep kMaxRep = static_cast<rep>(kInfinite - 1);
            auto sleepTime = (ms < kMaxRep) ? ms : kMaxRep;
            Sleep(static_cast<DWORD>(sleepTime));
            ms -= sleepTime;
        }
    }
    template <class Clock, class Duration>
    void sleep_until(const std::chrono::time_point<Clock,Duration>& sleep_time)
    {
        sleep_for(sleep_time-Clock::now());
    }
}
} //  Namespace mingw_stdthread

namespace std
{

#if defined(__MINGW32__ ) && !defined(_GLIBCXX_HAS_GTHREADS)
using mingw_stdthread::thread;

namespace this_thread
{
using namespace mingw_stdthread::this_thread;
}
#elif !defined(MINGW_STDTHREAD_REDUNDANCY_WARNING)  //  Skip repetition
#define MINGW_STDTHREAD_REDUNDANCY_WARNING
#pragma message "This version of MinGW seems to include a win32 port of\
 pthreads, and probably already has C++11 std threading classes implemented,\
 based on pthreads. These classes, found in namespace std, are not overridden\
 by the mingw-std-thread library. If you would still like to use this\
 implementation (as it is more lightweight), use the classes provided in\
 namespace mingw_stdthread."
#endif


template<>
struct hash<mingw_stdthread::thread::id>
{
    typedef mingw_stdthread::thread::id argument_type;
    typedef size_t result_type;
    size_t operator() (const argument_type & i) const noexcept
    {
        return i.mId;
    }
};
}
#endif // WIN32STDTHREAD_H