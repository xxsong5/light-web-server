# a web server implemented with c++


## feature

> * light weight, easy-use
>
> * memory cache controll,  using mmap for larger pages,  new designed small memory allocator,  automatic cache cleaner, multi-thread-access
>
> * using multi-thread mode, for high concurrency performance
>
> * vectorized process funcs, so multi-connections can be processed at one thread almost simutaneously
>
> * less configure


## build

`g++ httplightserver.cpp -std=c++11 -lpthread -o httplightserver`
