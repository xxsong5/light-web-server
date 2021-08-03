# a web server implemented with c++


## features

> * light weight, easy-use, no third-partis
>
> * memory cache controll,  using mmap for larger pages,  new designed small memory allocator,  automatic cache cleaner, multi-thread-access
>
> * using multi-thread mode, for high concurrency performance
>
> * vectorized process funcs, so multi-connections can be processed at one thread almost simutaneously
>
> * less configure
>
> * only support linux currently


## build

`g++ httplightserver.cpp -std=c++11 -lpthread -o httplightserver`
